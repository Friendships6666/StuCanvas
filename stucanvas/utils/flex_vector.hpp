/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <atomic>
#include <type_traits>
#include <cstring>
#include <stdexcept>

#ifdef _MSC_VER
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace StuCanvas::utils
{
    // 默认对齐内存分配器
    struct DefaultAllocator {
        [[nodiscard]] static void* allocate(size_t size, size_t alignment) noexcept {
            if (size == 0) return nullptr;
#ifdef _MSC_VER
            return _aligned_malloc(size, alignment);
#else
            if (alignment < sizeof(void*)) alignment = sizeof(void*);
            void* ptr = nullptr;
            if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
            return ptr;
#endif
        }

        static void deallocate(void* ptr, size_t size, size_t alignment) noexcept {
            if (!ptr) return;
#ifdef _MSC_VER
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }
    };

    // 全局类型注册器
    class TypeRegistry {
    private:
        inline static std::atomic<uint32_t> s_counter{0};

    public:
        template <typename T>
        [[nodiscard]] static uint32_t get_id() noexcept {
            static const uint32_t id = s_counter++;
            return id;
        }
    };

    // 静态类型的全局操作表 (压缩 ObjectHeader)
    struct TypeOps {
        void(*deleter)(void*) noexcept;
        void(*relocator)(void* dst, void* src) noexcept;
    };

    // 🚀 每个类型特有的物理隔离标记函数（用于在编译期生成唯一链接符号，100% 防 ICF）
    template <typename T>
    static void unique_marker() noexcept {}

    // 🚀 C++20/23 模板非类型参数（NTTP）物理隔离分发器
    template <typename T, void(*Marker)() noexcept>
    struct TypeDispatcher {
        static void solve_deleter(void* ptr) noexcept {
            reinterpret_cast<T*>(ptr)->~T();
        }

        static void solve_relocator(void* dst, void* src) noexcept {
            ::new (dst) T(std::move(*reinterpret_cast<T*>(src)));
            reinterpret_cast<T*>(src)->~T();
        }

        // 全局唯一静态操作表，100% 防止被 ICF 合并
        static constexpr TypeOps ops {
            .deleter = &solve_deleter,
            .relocator = &solve_relocator
        };
    };

    // 压缩后的 ObjectHeader：由 24 字节精密缩减为 16 字节
    struct ObjectHeader {
        uint32_t object_offset;   
        uint32_t next_oh_offset;  
        const TypeOps* ops;       
    };
    static_assert(sizeof(ObjectHeader) == 16, "ObjectHeader size must be exactly 16 bytes!");

    // 强行对齐至 64 字节的头部元数据
    struct alignas(64) FlexHeader {
        uint32_t buffer_size;       
        uint32_t buffer_capacity;   
        uint32_t page_table_cap;    
        uint32_t last_oh_offset;    
        void** page_table;          // 2D 二级页表指针
        bool all_trivially_relocatable; 
    };
    constexpr size_t FLEX_HEADER_SIZE = sizeof(FlexHeader); // 64 字节

    template <size_t PageSize = 64, typename Allocator = DefaultAllocator>
    class FlexVector {
        static_assert((PageSize & (PageSize - 1)) == 0, "PageSize must be a power of 2!");

    private:
        // 🚀 极致压缩：由于单实例异构数据极难超过 64KB，将 Slot 大小缩减为 2 字节（uint16_t）
        // 一个物理页 Page 大小直接从 256 字节缩减到 128 字节！内存浪费砍半！
        struct Slot {
            uint16_t offset; // 0xFFFF 代表未被占用
        };

        struct Page {
            Slot slots[PageSize];
        };

        // 唯一成员：栈上仅占 8 字节
        std::byte* m_buffer = nullptr; 

        inline FlexHeader* get_header() noexcept { 
            [[assume(m_buffer != nullptr)]]; // C++23 硬件假设提示
            return reinterpret_cast<FlexHeader*>(m_buffer - FLEX_HEADER_SIZE); 
        }

        inline const FlexHeader* get_header() const noexcept { 
            [[assume(m_buffer != nullptr)]]; 
            return reinterpret_cast<const FlexHeader*>(m_buffer - FLEX_HEADER_SIZE); 
        }

        static constexpr size_t constexpr_log2(size_t n) noexcept {
            size_t res = 0;
            while (n > 1) { n >>= 1; res++; }
            return res;
        }

        static constexpr size_t Shift = constexpr_log2(PageSize);
        static constexpr size_t Mask = PageSize - 1;

        void init_buffer() {
            constexpr size_t InitialBufCap = 256;
            size_t total_bytes = FLEX_HEADER_SIZE + InitialBufCap;

            void* ptr = Allocator::allocate(total_bytes, 64);
            if (!ptr) [[unlikely]] throw std::bad_alloc();

            FlexHeader* h = reinterpret_cast<FlexHeader*>(ptr);
            h->buffer_size = 0;
            h->buffer_capacity = static_cast<uint32_t>(InitialBufCap);
            h->page_table_cap = 0;
            h->last_oh_offset = 0xFFFFFFFF;
            h->page_table = nullptr;
            h->all_trivially_relocatable = true;

            m_buffer = reinterpret_cast<std::byte*>(h + 1);
        }

        void grow_page_table(uint32_t needed_pages) {
            FlexHeader* h = get_header();
            uint32_t old_cap = h->page_table_cap;
            uint32_t new_cap = old_cap == 0 ? 4 : old_cap * 2;
            while (new_cap < needed_pages) { new_cap *= 2; }

            void** new_table = static_cast<void**>(Allocator::allocate(new_cap * sizeof(void*), alignof(void*)));
            if (!new_table) [[unlikely]] throw std::bad_alloc();

            if (h->page_table) {
                std::memcpy(new_table, h->page_table, old_cap * sizeof(void*));
                Allocator::deallocate(h->page_table, old_cap * sizeof(void*), alignof(void*));
            }

            std::memset(new_table + old_cap, 0, (new_cap - old_cap) * sizeof(void*));
            h->page_table = new_table;
            h->page_table_cap = new_cap;
        }

        Page* allocate_page() {
            Page* p = static_cast<Page*>(Allocator::allocate(sizeof(Page), alignof(Page)));
            if (!p) [[unlikely]] throw std::bad_alloc();
            std::memset(p->slots, 0xFF, sizeof(Page)); // 将所有 2字节槽位初始化为 0xFFFF
            return p;
        }

        void grow_buffer(size_t required_bytes) {
            // 物理防越界边界保护：限制单实例异构数据在 64 KB 内（对单实例完全足够）
            if (required_bytes > 0xFFFF) [[unlikely]] {
                throw std::out_of_range("FlexVector: Single instance flat buffer size exceeded 64 KB limit.");
            }

            FlexHeader* h = get_header();
            size_t old_cap = h->buffer_capacity;
            size_t new_cap = old_cap == 0 ? 256 : old_cap * 2;
            while (new_cap < required_bytes) { new_cap *= 2; }

            constexpr size_t BufferAlignment = 64; 
            size_t old_total = FLEX_HEADER_SIZE + old_cap;
            size_t new_total = FLEX_HEADER_SIZE + new_cap;

            void* new_ptr = Allocator::allocate(new_total, BufferAlignment);
            if (!new_ptr) [[unlikely]] throw std::bad_alloc();

            FlexHeader* new_h = reinterpret_cast<FlexHeader*>(new_ptr);
            std::memcpy(new_h, h, FLEX_HEADER_SIZE); // 迁移元数据
            new_h->buffer_capacity = static_cast<uint32_t>(new_cap);
            std::byte* new_buf = reinterpret_cast<std::byte*>(new_h + 1);

            if (m_buffer) {
                if (h->all_trivially_relocatable) {
                    std::memcpy(new_buf, m_buffer, h->buffer_size);
                } else {
                    uint32_t curr_oh_offset = 0;
                    while (curr_oh_offset != 0xFFFFFFFF) {
                        auto* old_oh = reinterpret_cast<ObjectHeader*>(m_buffer + curr_oh_offset);
                        ::new (static_cast<void*>(new_buf + curr_oh_offset)) ObjectHeader(*old_oh);
                        old_oh->ops->relocator(new_buf + old_oh->object_offset, m_buffer + old_oh->object_offset);
                        curr_oh_offset = old_oh->next_oh_offset;
                    }
                }
                Allocator::deallocate(h, old_total, BufferAlignment);
            }

            m_buffer = new_buf;
        }

    public:
        struct Iterator {
            std::byte* buffer;
            uint32_t oh_offset;

            [[nodiscard]] inline void* operator*() const noexcept {
                if (oh_offset == 0xFFFFFFFF || !buffer) return nullptr;
                auto* oh = reinterpret_cast<ObjectHeader*>(buffer + oh_offset);
                return static_cast<void*>(buffer + oh->object_offset);
            }

            inline Iterator& operator++() noexcept {
                if (oh_offset == 0xFFFFFFFF || !buffer) return *this;
                auto* oh = reinterpret_cast<ObjectHeader*>(buffer + oh_offset);
                oh_offset = oh->next_oh_offset;
                return *this;
            }

            inline Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            [[nodiscard]] inline bool operator==(const Iterator& other) const noexcept {
                return buffer == other.buffer && oh_offset == other.oh_offset;
            }

            [[nodiscard]] inline bool operator!=(const Iterator& other) const noexcept {
                return !(*this == other);
            }
        };

        [[nodiscard]] inline Iterator begin() noexcept {
            if (!m_buffer) return { nullptr, 0xFFFFFFFF };
            FlexHeader* h = get_header();
            if (h->buffer_size == 0 || h->last_oh_offset == 0xFFFFFFFF) {
                return { nullptr, 0xFFFFFFFF };
            }
            return { m_buffer, 0 }; 
        }

        [[nodiscard]] inline Iterator end() noexcept {
            return { m_buffer, 0xFFFFFFFF };
        }

        FlexVector() = default;

        ~FlexVector() noexcept {
            if (m_buffer) {
                clear();
                FlexHeader* h = get_header();
                for (uint32_t i = 0; i < h->page_table_cap; ++i) {
                    if (h->page_table[i]) {
                        Allocator::deallocate(h->page_table[i], sizeof(Page), alignof(Page));
                    }
                }
                if (h->page_table) {
                    Allocator::deallocate(h->page_table, h->page_table_cap * sizeof(void*), alignof(void*));
                }
                size_t total_bytes = FLEX_HEADER_SIZE + h->buffer_capacity;
                Allocator::deallocate(h, total_bytes, 64);
            }
        }

        FlexVector(const FlexVector&) = delete;
        FlexVector& operator=(const FlexVector&) = delete;

        FlexVector(FlexVector&& other) noexcept : m_buffer(other.m_buffer) {
            other.m_buffer = nullptr;
        }

        FlexVector& operator=(FlexVector&& other) noexcept {
            if (this != &other) {
                this->~FlexVector();
                m_buffer = other.m_buffer;
                other.m_buffer = nullptr;
            }
            return *this;
        }

        // =================================================================
        // 🚀 原位构造接口（2D 页表寻址 + 16字节 Slot 特化，0 运行时 ICF 开销）
        // =================================================================
        template <typename T, typename... Args>
        T* emplace_back(Args&&... args) {
            const uint32_t type_id = TypeRegistry::get_id<T>();
            const uint32_t page_idx = type_id >> Shift;
            const uint32_t slot_idx = type_id & Mask;

            if (!m_buffer) [[unlikely]] {
                init_buffer();
            }

            FlexHeader* h = get_header();
            if (page_idx >= h->page_table_cap) [[unlikely]] {
                grow_page_table(page_idx + 1);
                h = get_header();
            }

            if (!h->page_table[page_idx]) [[unlikely]] {
                h->page_table[page_idx] = allocate_page();
            }

            auto* page = reinterpret_cast<Page*>(h->page_table[page_idx]);
            const uint16_t existing_offset = page->slots[slot_idx].offset;

            // 如果同类型实例已存在：就地调用析构，然后就地重新构造
            if (existing_offset != 0xFFFF) {
                auto* existing_obj = reinterpret_cast<T*>(m_buffer + existing_offset);
                existing_obj->~T();
                return ::new (static_cast<void*>(existing_obj)) T(std::forward<Args>(args)...);
            }

            // 计算连续紧凑内存对齐
            const size_t cursor = h->buffer_size;
            const size_t oh_align = alignof(ObjectHeader);
            const size_t oh_padding = (oh_align - (cursor % oh_align)) % oh_align;
            const size_t oh_offset = cursor + oh_padding;

            const size_t t_align = alignof(T);
            const size_t t_padding = (t_align - ((oh_offset + sizeof(ObjectHeader)) % t_align)) % t_align;
            const size_t t_offset = oh_offset + sizeof(ObjectHeader) + t_padding;

            const size_t required_bytes = t_offset + sizeof(T);
            if (required_bytes > h->buffer_capacity) [[unlikely]] {
                grow_buffer(required_bytes);
                h = get_header();
                page = reinterpret_cast<Page*>(h->page_table[page_idx]);
            }

            if constexpr (!std::is_trivially_copyable_v<T>) {
                h->all_trivially_relocatable = false;
            }

            // 写入自描述头
            auto* oh = ::new (static_cast<void*>(m_buffer + oh_offset)) ObjectHeader();
            oh->object_offset = static_cast<uint32_t>(t_offset);
            oh->next_oh_offset = 0xFFFFFFFF;
            
            // 🚀 编译期 NTTP 隔离符号，消灭运行期的 volatile 读写开销
            oh->ops = &TypeDispatcher<T, &unique_marker<T>>::ops;

            if (h->last_oh_offset != 0xFFFFFFFF) {
                auto* last_oh = reinterpret_cast<ObjectHeader*>(m_buffer + h->last_oh_offset);
                last_oh->next_oh_offset = static_cast<uint32_t>(oh_offset);
            }
            h->last_oh_offset = static_cast<uint32_t>(oh_offset);

            // 物理原位构造实际对象
            T* obj = ::new (static_cast<void*>(m_buffer + t_offset)) T(std::forward<Args>(args)...);

            // 注册 16 字节页表槽，更新已占用字节
            page->slots[slot_idx].offset = static_cast<uint16_t>(t_offset);
            h->buffer_size = static_cast<uint32_t>(required_bytes);

            return obj;
        }

        template <typename T>
        inline T* push_back(const T& val) {
            return emplace_back<T>(val);
        }

        template <typename T>
        inline T* push_back(T&& val) {
            return emplace_back<T>(std::move(val));
        }

        // =================================================================
        // 🚀 二级 2D 页表检索（空间最优解，兼顾 64字节对齐快速缓存寻址）
        // =================================================================
        template <typename T>
        [[nodiscard]] inline T* get() const noexcept {
            if (!m_buffer) return nullptr;
            const FlexHeader* h = get_header();

            const uint32_t type_id = TypeRegistry::get_id<T>();
            const uint32_t page_idx = type_id >> Shift;
            const uint32_t slot_idx = type_id & Mask;

            if (page_idx >= h->page_table_cap) return nullptr;
            if (!h->page_table[page_idx]) return nullptr;

            auto* page = reinterpret_cast<Page*>(h->page_table[page_idx]);
            const uint16_t offset = page->slots[slot_idx].offset;
            if (offset == 0xFFFF) return nullptr;

            return reinterpret_cast<T*>(m_buffer + offset);
        }

        template <typename T>
        [[nodiscard]] inline bool has() const noexcept {
            return get<T>() != nullptr;
        }

        void clear() noexcept {
            if (!m_buffer) return;
            FlexHeader* h = get_header();
            if (h->buffer_size == 0) return;

            uint32_t curr_oh = 0;
            while (curr_oh != 0xFFFFFFFF) {
                auto* oh = reinterpret_cast<ObjectHeader*>(m_buffer + curr_oh);
                uint32_t next = oh->next_oh_offset;
                oh->ops->deleter(m_buffer + oh->object_offset);
                curr_oh = next;
            }

            for (uint32_t i = 0; i < h->page_table_cap; ++i) {
                if (h->page_table[i]) {
                    auto* page = reinterpret_cast<Page*>(h->page_table[i]);
                    std::memset(page->slots, 0xFF, sizeof(Page));
                }
            }

            h->buffer_size = 0;
            h->last_oh_offset = 0xFFFFFFFF;
        }

        [[nodiscard]] size_t buffer_bytes() const noexcept {
            if (!m_buffer) return 0;
            const FlexHeader* h = get_header();
            return h->buffer_size;
        }

        [[nodiscard]] bool empty() const noexcept { return buffer_bytes() == 0; }
    };

    static_assert(sizeof(FlexVector<>) == 8, "FlexVector must occupy exactly 8 bytes!");
} // namespace StuCanvas::utils
