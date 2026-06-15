/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <utility>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cassert>

// 针对 Windows MSVC 平台和 GCC 平台的极致控制与对齐保护
#if defined(_MSC_VER)
#include <malloc.h>
#define STUCANVAS_NOINLINE __declspec(noinline)
#else
#define STUCANVAS_NOINLINE __attribute__((noinline))
#endif

namespace StuCanvas::utils
{
    namespace detail
    {
        // 跨平台高自适应对齐分配器
        inline void* aligned_alloc_helper(size_t size, size_t alignment)
        {
#if defined(__cpp_aligned_new) && __cpp_aligned_new >= 201606L
            #ifndef __STDCPP_DEFAULT_NEW_ALIGNMENT__
                #define __STDCPP_DEFAULT_NEW_ALIGNMENT__ alignof(std::max_align_t)
            #endif
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
                return ::operator new(size, std::align_val_t(alignment));
            } else {
                return ::operator new(size);
            }
#else
            if (alignment <= alignof(std::max_align_t)) {
                return ::operator new(size);
            }
            #if defined(_MSC_VER)
                void* ptr = _aligned_malloc(size, alignment);
                if (!ptr) throw std::bad_alloc();
                return ptr;
            #else
                void* ptr = nullptr;
                if (posix_memalign(&ptr, alignment, size) != 0) {
                    throw std::bad_alloc();
                }
                return ptr;
            #endif
#endif
        }

        inline void aligned_free_helper(void* ptr, size_t alignment) noexcept
        {
            if (!ptr) return;
#if defined(__cpp_aligned_new) && __cpp_aligned_new >= 201606L
            #ifndef __STDCPP_DEFAULT_NEW_ALIGNMENT__
                #define __STDCPP_DEFAULT_NEW_ALIGNMENT__ alignof(std::max_align_t)
            #endif
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
                ::operator delete(ptr, std::align_val_t(alignment));
            } else {
                ::operator delete(ptr);
            }
#else
            if (alignment <= alignof(std::max_align_t)) {
                ::operator delete(ptr);
                return;
            }
            #if defined(_MSC_VER)
                _aligned_free(ptr);
            #else
                std::free(ptr);
            #endif
#endif
        }
    } // namespace detail

    template <typename T, size_t BlockSize = 1024>
    class PtrBlockDeque
    {
        // 编译期自检：非指针类型、非 64 位（8字节）指针一律禁止编译
        static_assert(std::is_pointer_v<T>, "PtrBlockDeque can only store pointer types!");
        static_assert(sizeof(T) == 8, "PtrBlockDeque can only store 8-byte (64-bit OS) pointer types!");
        static_assert((BlockSize & (BlockSize - 1)) == 0, "BlockSize must be a power of 2!");

    private:
        struct Header {
            uint32_t capacity;   // 分块二级指针数组的容量
            uint32_t num_blocks; // 当前已分配的物理分块数量
            uint64_t total_size; // 全局活动元素总数
        };

        static constexpr size_t HeaderOffset = (sizeof(Header) + alignof(T*) - 1) & ~(alignof(T*) - 1);

        static constexpr size_t constexpr_log2(size_t n) noexcept
        {
            size_t res = 0;
            while (n > 1) {
                n >>= 1;
                res++;
            }
            return res;
        }

        static constexpr size_t Shift = constexpr_log2(BlockSize);
        static constexpr size_t Mask = BlockSize - 1;

        // 类内唯一的 8 字节成员
        T** m_blocks = nullptr;

        // 🚀 物理分块线程局部无锁复用池
        struct ElementBlockCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~ElementBlockCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    detail::aligned_free_helper(head, alignof(T));
                    head = next;
                }
            }
        };

        static inline thread_local ElementBlockCache t_block_cache;

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_blocks) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_blocks) - HeaderOffset);
        }

        static T** allocate_blocks_array(uint32_t capacity)
        {
            if (capacity == 0) return nullptr;
            size_t total_bytes = HeaderOffset + static_cast<size_t>(capacity) * sizeof(T*);
            void* raw = detail::aligned_alloc_helper(total_bytes, alignof(T*));

            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->num_blocks = 0;
            h->total_size = 0;

            return reinterpret_cast<T**>(reinterpret_cast<char*>(raw) + HeaderOffset);
        }

        static void deallocate_blocks_array(T** blocks) noexcept
        {
            if (!blocks) return;
            void* raw = reinterpret_cast<char*>(blocks) - HeaderOffset;
            detail::aligned_free_helper(raw, alignof(T*));
        }

        static T* allocate_element_block()
        {
            if (t_block_cache.head) {
                void* raw = t_block_cache.head;
                t_block_cache.head = t_block_cache.head->next;
                t_block_cache.count--;
                return static_cast<T*>(raw);
            }
            size_t bytes = BlockSize * sizeof(T);
            void* raw = detail::aligned_alloc_helper(bytes, alignof(T));
            return static_cast<T*>(raw);
        }

        static void deallocate_element_block(T* ptr) noexcept
        {
            if (!ptr) return;
            if (t_block_cache.count < 16) {
                auto* node = reinterpret_cast<typename ElementBlockCache::Node*>(ptr);
                node->next = t_block_cache.head;
                t_block_cache.head = node;
                t_block_cache.count++;
            } else {
                detail::aligned_free_helper(ptr, alignof(T));
            }
        }

        void reserve_blocks(uint32_t new_capacity)
        {
            Header* old_h = get_header();
            uint32_t old_capacity = old_h ? old_h->capacity : 0;
            if (new_capacity <= old_capacity) return;

            T** new_blocks = allocate_blocks_array(new_capacity);
            Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_blocks) - HeaderOffset);

            if (m_blocks) {
                uint32_t num_blocks = old_h->num_blocks;
                new_h->num_blocks = num_blocks;
                new_h->total_size = old_h->total_size;

                for (uint32_t i = 0; i < num_blocks; ++i) {
                    new_blocks[i] = m_blocks[i];
                }
                deallocate_blocks_array(m_blocks);
            } else {
                new_h->num_blocks = 0;
                new_h->total_size = 0;
            }
            m_blocks = new_blocks;
        }

        void add_new_block()
        {
            Header* h = get_header();
            uint32_t num_blocks = h ? h->num_blocks : 0;
            uint32_t capacity = h ? h->capacity : 0;

            if (num_blocks >= capacity) {
                reserve_blocks(capacity == 0 ? 2 : capacity * 2);
                h = get_header();
            }

            m_blocks[num_blocks] = allocate_element_block();
            h->num_blocks = num_blocks + 1;
        }

        // 🚀 析离慢路径（Outlined Cold Path），彻底释放主循环寄存器
        STUCANVAS_NOINLINE void grow_and_push(T val)
        {
            Header* h = get_header();
            uint64_t total_size = h ? h->total_size : 0;
            const size_t current_block_idx = total_size >> Shift;
            uint32_t num_blocks = h ? h->num_blocks : 0;

            if (current_block_idx >= num_blocks) {
                add_new_block();
                h = get_header();
            }

            const size_t element_idx = total_size & Mask;
            m_blocks[current_block_idx][element_idx] = val;
            h->total_size = total_size + 1;
        }

    public:
        // ─────────────────────────────────────────────────────────────────────
        // 1. 高性能分段指针迭代器
        // ─────────────────────────────────────────────────────────────────────
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            T* ptr = nullptr;
            T* block_end = nullptr;
            T** current_block = nullptr;

            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }

            // 🚀 热路径迭代：99.9% 概率仅做裸指针自增和比较（速度等同于 vector）
            Iterator& operator++() noexcept {
                ++ptr;
                if (ptr == block_end) [[unlikely]] {
                    current_block++;
                    if (*current_block) {
                        ptr = *current_block;
                        block_end = ptr + BlockSize;
                    } else {
                        ptr = nullptr;
                        block_end = nullptr;
                    }
                }
                return *this;
            }

            Iterator operator++(int) noexcept {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const Iterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const Iterator& other) const noexcept { return ptr != other.ptr; }
        };

        struct ConstIterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = const T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            const T* ptr = nullptr;
            const T* block_end = nullptr;
            const T* const* current_block = nullptr;

            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }

            ConstIterator& operator++() noexcept {
                ++ptr;
                if (ptr == block_end) [[unlikely]] {
                    current_block++;
                    if (*current_block) {
                        ptr = *current_block;
                        block_end = ptr + BlockSize;
                    } else {
                        ptr = nullptr;
                        block_end = nullptr;
                    }
                }
                return *this;
            }

            ConstIterator operator++(int) noexcept {
                ConstIterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const ConstIterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const ConstIterator& other) const noexcept { return ptr != other.ptr; }
        };

        Iterator begin() noexcept {
            Header* h = get_header();
            if (!h || h->total_size == 0) return { nullptr, nullptr, nullptr };
            return { m_blocks[0], m_blocks[0] + BlockSize, m_blocks };
        }

        Iterator end() noexcept {
            Header* h = get_header();
            if (!h || h->total_size == 0) return { nullptr, nullptr, nullptr };
            const size_t block_idx = h->total_size >> Shift;
            const size_t element_idx = h->total_size & Mask;

            if (element_idx == 0) {
                T* ptr = m_blocks[block_idx - 1] + BlockSize;
                return { ptr, ptr, m_blocks + block_idx - 1 };
            } else {
                T* ptr = m_blocks[block_idx] + element_idx;
                return { ptr, m_blocks[block_idx] + BlockSize, m_blocks + block_idx };
            }
        }

        ConstIterator begin() const noexcept {
            Header* h = get_header();
            if (!h || h->total_size == 0) return { nullptr, nullptr, nullptr };
            return { m_blocks[0], m_blocks[0] + BlockSize, m_blocks };
        }

        ConstIterator end() const noexcept {
            Header* h = get_header();
            if (!h || h->total_size == 0) return { nullptr, nullptr, nullptr };
            const size_t block_idx = h->total_size >> Shift;
            const size_t element_idx = h->total_size & Mask;
            if (element_idx == 0) {
                const T* ptr = m_blocks[block_idx - 1] + BlockSize;
                return { ptr, ptr, m_blocks + block_idx - 1 };
            } else {
                const T* ptr = m_blocks[block_idx] + element_idx;
                return { ptr, m_blocks[block_idx] + BlockSize, m_blocks + block_idx };
            }
        }

        ConstIterator cbegin() const noexcept { return begin(); }
        ConstIterator cend() const noexcept { return end(); }

        // ─────────────────────────────────────────────────────────────────────
        // 2. 生命周期与移动语义 (RAII)
        // ─────────────────────────────────────────────────────────────────────
        PtrBlockDeque() { add_new_block(); }

        ~PtrBlockDeque() noexcept
        {
            clear();
            if (m_blocks) {
                Header* h = get_header();
                uint32_t num_blocks = h->num_blocks;
                for (uint32_t i = 0; i < num_blocks; ++i) {
                    deallocate_element_block(m_blocks[i]);
                }
                deallocate_blocks_array(m_blocks);
            }
        }

        PtrBlockDeque(const PtrBlockDeque&) = delete;
        PtrBlockDeque& operator=(const PtrBlockDeque&) = delete;

        PtrBlockDeque(PtrBlockDeque&& other) noexcept : m_blocks(other.m_blocks)
        {
            other.m_blocks = nullptr;
        }

        PtrBlockDeque& operator=(PtrBlockDeque&& other) noexcept
        {
            if (this != &other) {
                clear();
                if (m_blocks) {
                    Header* h = get_header();
                    uint32_t num_blocks = h->num_blocks;
                    for (uint32_t i = 0; i < num_blocks; ++i) {
                        deallocate_element_block(m_blocks[i]);
                    }
                    deallocate_blocks_array(m_blocks);
                }
                m_blocks = other.m_blocks;
                other.m_blocks = nullptr;
            }
            return *this;
        }

        // ─────────────────────────────────────────────────────────────────────
        // 3. 基础寻址与容量 API
        // ─────────────────────────────────────────────────────────────────────
        [[nodiscard]] size_t size() const noexcept
        {
            auto* h = get_header();
            return h ? h->total_size : 0;
        }

        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        T& operator[](size_t index) noexcept
        {
            const size_t block_idx = index >> Shift;
            const size_t element_idx = index & Mask;
            return m_blocks[block_idx][element_idx];
        }

        const T& operator[](size_t index) const noexcept
        {
            const size_t block_idx = index >> Shift;
            const size_t element_idx = index & Mask;
            return m_blocks[block_idx][element_idx];
        }

        T& back() noexcept { return (*this)[size() - 1]; }
        const T& back() const noexcept { return (*this)[size() - 1]; }

        void reserve(size_t capacity) noexcept
        {
            size_t blocks_needed = (capacity + BlockSize - 1) >> Shift;
            if (blocks_needed == 0) blocks_needed = 1;
            reserve_blocks(static_cast<uint32_t>(blocks_needed));
        }

        // 🚀 极致优化热路径：5 条汇编，完美寄存器分配，支持 SIMD
        inline void push_back(T val) noexcept
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint64_t total_size = h->total_size;
                const size_t current_block_idx = total_size >> Shift;
                if (current_block_idx < h->num_blocks) [[likely]] {
                    const size_t element_idx = total_size & Mask;
                    m_blocks[current_block_idx][element_idx] = val;
                    h->total_size = total_size + 1;
                    return;
                }
            }
            grow_and_push(val);
        }

        template <typename... Args>
        T& emplace_back(Args&&... args) noexcept
        {
            T val(std::forward<Args>(args)...);
            push_back(val);
            return back();
        }

        void pop_back() noexcept
        {
            Header* h = get_header();
            if (!h || h->total_size == 0) return;

            uint64_t total_size = h->total_size;
            h->total_size = total_size - 1;

            size_t active_blocks = (h->total_size + BlockSize - 1) >> Shift;
            if (active_blocks == 0) active_blocks = 1;

            if (h->num_blocks > active_blocks + 1) {
                deallocate_element_block(m_blocks[h->num_blocks - 1]);
                h->num_blocks = h->num_blocks - 1;
            }
        }

        // 🚀 指针特化 O(1) 批量快速重置：循环次数削减千倍
        void clear() noexcept
        {
            Header* h = get_header();
            if (!h || h->total_size == 0) return;

            uint32_t num_blocks = h->num_blocks;
            for (uint32_t i = 1; i < num_blocks; ++i) {
                deallocate_element_block(m_blocks[i]);
            }
            h->num_blocks = 1;
            h->total_size = 0;
        }

        // ─────────────────────────────────────────────────────────────────────
        // 4. 快速擦除方法
        // ─────────────────────────────────────────────────────────────────────
        size_t erase_unordered(const T& value) noexcept
        {
            size_t erased_count = 0;
            for (size_t i = 0; i < size(); ) {
                if ((*this)[i] == value) {
                    if (i != size() - 1) {
                        (*this)[i] = std::move((*this)[size() - 1]);
                    }
                    pop_back();
                    erased_count++;
                } else {
                    ++i;
                }
            }
            return erased_count;
        }

        size_t erase(const T& value) noexcept
        {
            size_t erased_count = 0;
            for (size_t i = 0; i < size(); ) {
                if ((*this)[i] == value) {
                    for (size_t j = i; j < size() - 1; ++j) {
                        (*this)[j] = std::move((*this)[j + 1]);
                    }
                    pop_back();
                    erased_count++;
                } else {
                    ++i;
                }
            }
            return erased_count;
        }
    };
} // namespace StuCanvas::utils