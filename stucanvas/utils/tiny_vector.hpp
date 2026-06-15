/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <cstdint>
#include <new>
#include <utility>
#include <algorithm>
#include <type_traits>
#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <cstring> // 引入 std::memcpy

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
                return ::operator new(size, std::align_val_t(alignment)); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            } else {
                return ::operator new(size); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            }
#else
            if (alignment <= alignof(std::max_align_t)) {
                return ::operator new(size); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
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
                _aligned_free(ptr); // Windows 专属释放
            #else
                std::free(ptr);     // POSIX 专属释放
            #endif
#endif
        }

        // 🚀 跨平台高自适应就地重新分配器
        inline void* aligned_realloc_helper(void* ptr, size_t old_size, size_t new_size, size_t alignment)
        {
#if defined(_MSC_VER)
            void* new_ptr = _aligned_realloc(ptr, new_size, alignment);
            if (!new_ptr) throw std::bad_alloc();
            return new_ptr;
#else
            // 对于标准 16 字节对齐，glibc (Linux) 的 realloc 保证完全对齐
            if (alignment <= alignof(std::max_align_t)) {
                void* new_ptr = std::realloc(ptr, new_size);
                if (!new_ptr) throw std::bad_alloc();
                return new_ptr;
            }
            // 过度对齐降级安全方案
            void* new_ptr = aligned_alloc_helper(new_size, alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            if (ptr) {
                std::memcpy(new_ptr, ptr, old_size);
                aligned_free_helper(ptr, alignment);
            }
            return new_ptr;
#endif
        }
    } // namespace detail

    // ─────────────────────────────────────────────────────────────────────────
    // 1. 通用版 TinyVector 声明（非指针类型）
    // ─────────────────────────────────────────────────────────────────────────
    template <typename T>
    class TinyVector
    {
    private:
        struct Header
        {
            uint32_t capacity;
            uint32_t size;
        };

        // 🚀 16 字节黄金对齐平衡点：堆上仅产生 8 字节 Padding，彻底消除跨缓存行惩罚
        static constexpr size_t Alignment = alignof(T) > 16 ? alignof(T) : 16;
        static constexpr size_t HeaderOffset = (sizeof(Header) + Alignment - 1) & ~(Alignment - 1);

        T* m_data = nullptr;

        struct ThreadCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~ThreadCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    // 🚀 绝对路径定位
                    ::StuCanvas::utils::detail::aligned_free_helper(head, Alignment);
                    head = next;
                }
            }
        };

        static inline ThreadCache s_cache2;
        static inline ThreadCache s_cache4;

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_data) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_data) - HeaderOffset);
        }

        static T* allocate(uint32_t capacity)
        {
            if (capacity == 0) return nullptr;
            size_t total_size = HeaderOffset + static_cast<size_t>(capacity) * sizeof(T);

            void* raw = nullptr;
            if (capacity == 2 && s_cache2.head) {
                raw = s_cache2.head;
                s_cache2.head = s_cache2.head->next;
                s_cache2.count--;
            } else if (capacity == 4 && s_cache4.head) {
                raw = s_cache4.head;
                s_cache4.head = s_cache4.head->next;
                s_cache4.count--;
            } else {
                raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_size, Alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            }

            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->size = 0;

            return reinterpret_cast<T*>(reinterpret_cast<char*>(raw) + HeaderOffset);
        }

        static void deallocate(T* data) noexcept
        {
            if (!data) return;
            void* raw = reinterpret_cast<char*>(data) - HeaderOffset;
            Header* h = reinterpret_cast<Header*>(raw);
            uint32_t capacity = h->capacity;

            if (capacity == 2 && s_cache2.count < 128) {
                auto* node = reinterpret_cast<ThreadCache::Node*>(raw); // 🚀 修复：移除了多余的 typename 关键字
                node->next = s_cache2.head;
                s_cache2.head = node;
                s_cache2.count++;
            } else if (capacity == 4 && s_cache4.count < 64) {
                auto* node = reinterpret_cast<ThreadCache::Node*>(raw); // 🚀 修复：移除了多余的 typename 关键字
                node->next = s_cache4.head;
                s_cache4.head = node;
                s_cache4.count++;
            } else {
                ::StuCanvas::utils::detail::aligned_free_helper(raw, Alignment);
            }
        }

        STUCANVAS_NOINLINE void grow_and_push(const T& val)
        {
            uint32_t sz = size();
            reserve(sz == 0 ? 2 : static_cast<uint32_t>(sz * 2));
            Header* h = get_header();
            assert(m_data != nullptr); // 🚀 修复：增加零成本断言，彻底消除分析器指针可能为空的警报
            new (&m_data[sz]) T(val);
            h->size = sz + 1;
        }

        STUCANVAS_NOINLINE void grow_and_push(T&& val)
        {
            uint32_t sz = size();
            reserve(sz == 0 ? 2 : static_cast<uint32_t>(sz * 2));
            Header* h = get_header();
            assert(m_data != nullptr); // 🚀 修复：增加零成本断言
            new (&m_data[sz]) T(std::move(val));
            h->size = sz + 1;
        }

    public:
        TinyVector() noexcept = default;

        ~TinyVector() noexcept
        {
            clear();
            if (m_data) deallocate(m_data);
        }

        TinyVector(TinyVector&& other) noexcept : m_data(other.m_data) { other.m_data = nullptr; }

        TinyVector& operator=(TinyVector&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                if (m_data) deallocate(m_data);
                m_data = other.m_data;
                other.m_data = nullptr;
            }
            return *this;
        }

        TinyVector(const TinyVector& other)
        {
            if (other.m_data)
            {
                uint32_t cap = other.capacity();
                uint32_t sz = other.size();
                m_data = allocate(cap);
                get_header()->size = sz;
                for (uint32_t i = 0; i < sz; ++i) { new (&m_data[i]) T(other.m_data[i]); }
            }
        }

        TinyVector& operator=(const TinyVector& other)
        {
            if (this != &other)
            {
                clear();
                if (m_data) deallocate(m_data);
                if (other.m_data)
                {
                    uint32_t cap = other.capacity();
                    uint32_t sz = other.size();
                    m_data = allocate(cap);
                    get_header()->size = sz;
                    for (uint32_t i = 0; i < sz; ++i) { new (&m_data[i]) T(other.m_data[i]); }
                }
                else
                {
                    m_data = nullptr;
                }
            }
            return *this;
        }


        // ─────────────────────────────────────────────────────────────────────────
    // 1. 通用版新增成员函数 (通常在 public 区域，clear() 之后添加)
    // ─────────────────────────────────────────────────────────────────────────
    public:
        /**
         * @brief 动态调整容器大小（就地默认构造/析构释放）
         */
        void resize(uint32_t new_size)
        {
            uint32_t cur_size = size();
            if (new_size < cur_size)
            {
                // 1. 缩减大小：安全析构多余元素，不缩减物理容量
                for (uint32_t i = new_size; i < cur_size; ++i) {
                    m_data[i].~T();
                }
                get_header()->size = new_size;
            }
            else if (new_size > cur_size)
            {
                // 2. 扩容大小：利用特化 rehash 物理就地扩容，随后 placement new 默认构造
                reserve(new_size);
                Header* h = get_header();
                assert(m_data != nullptr); // 🚀 消除分析器空指针警告
                for (uint32_t i = cur_size; i < new_size; ++i) {
                    new (&m_data[i]) T();
                }
                h->size = new_size;
            }
        }

        /**
         * @brief 动态调整容器大小，并用 val 填充新槽位
         */
        void resize(uint32_t new_size, const T& val)
        {
            uint32_t cur_size = size();
            if (new_size < cur_size)
            {
                for (uint32_t i = new_size; i < cur_size; ++i) {
                    m_data[i].~T();
                }
                get_header()->size = new_size;
            }
            else if (new_size > cur_size)
            {
                reserve(new_size);
                Header* h = get_header();
                assert(m_data != nullptr);
                for (uint32_t i = cur_size; i < new_size; ++i) {
                    new (&m_data[i]) T(val);
                }
                h->size = new_size;
            }
        }

        /**
         * @brief 将当前内容替换为 count 个拷贝的 val
         */
        void assign(uint32_t count, const T& val)
        {
            clear();
            if (count > 0)
            {
                reserve(count);
                Header* h = get_header();
                assert(m_data != nullptr);
                for (uint32_t i = 0; i < count; ++i) {
                    new (&m_data[i]) T(val);
                }
                h->size = count;
            }
        }

        /**
         * @brief 将当前内容替换为区间 [first, last) 内的数据
         */
        void assign(const T* first, const T* last)
        {
            clear();
            if (first != last)
            {
                uint32_t count = static_cast<uint32_t>(last - first);
                reserve(count);
                Header* h = get_header();
                assert(m_data != nullptr);

                // 🚀 性能压榨：如果是平凡类型，直接通过物理 memcpy 传递，耗时归零！
                if constexpr (std::is_trivially_copyable_v<T>) {
                    std::memcpy(reinterpret_cast<void*>(m_data),
                                reinterpret_cast<const void*>(first),
                                count * sizeof(T));
                } else {
                    for (uint32_t i = 0; i < count; ++i) {
                        new (&m_data[i]) T(first[i]);
                    }
                }
                h->size = count;
            }
        }


        [[nodiscard]] uint32_t size() const noexcept
        {
            auto* h = get_header();
            return h ? h->size : 0;
        }

        [[nodiscard]] uint32_t capacity() const noexcept
        {
            auto* h = get_header();
            return h ? h->capacity : 0;
        }

        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        void reserve(uint32_t new_cap)
        {
            uint32_t cur_cap = capacity();
            if (new_cap <= cur_cap) return;

            uint32_t sz = size();

            if constexpr (std::is_trivially_copyable_v<T>) {
                void* old_raw = m_data ? (reinterpret_cast<char*>(m_data) - HeaderOffset) : nullptr;
                Header* old_h = get_header();
                uint32_t old_cap = old_h ? old_h->capacity : 0;

                if (old_cap <= 4) {
                    T* new_data = allocate(new_cap);
                    if (m_data) {
                        std::memcpy(reinterpret_cast<void*>(new_data),
                                    reinterpret_cast<const void*>(m_data),
                                    sz * sizeof(T));
                        deallocate(m_data);
                    }
                    Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_data) - HeaderOffset);
                    new_h->size = sz;
                    m_data = new_data;
                } else {
                    size_t old_total_size = HeaderOffset + static_cast<size_t>(old_cap) * sizeof(T);
                    size_t new_total_size = HeaderOffset + static_cast<size_t>(new_cap) * sizeof(T);
                    // 🚀 核心增加 NOLINT 注释：告知分析器此处属于自定义偏移内存设计，并非内存泄漏
                    void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper(old_raw, old_total_size, new_total_size, Alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
                    Header* new_h = reinterpret_cast<Header*>(new_raw);
                    new_h->capacity = new_cap;
                    new_h->size = sz;
                    m_data = reinterpret_cast<T*>(reinterpret_cast<char*>(new_raw) + HeaderOffset);
                }
            } else {
                T* new_data = allocate(new_cap);
                if (m_data) {
                    for (uint32_t i = 0; i < sz; ++i) {
                        if constexpr (std::is_move_constructible_v<T>) {
                            new (&new_data[i]) T(std::move(m_data[i]));
                        } else {
                            new (&new_data[i]) T(m_data[i]);
                        }
                        m_data[i].~T();
                    }
                    deallocate(m_data);
                }
                Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_data) - HeaderOffset);
                new_h->size = sz;
                m_data = new_data;
            }
        }

        inline void push_back(const T& val)
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
                    assert(m_data != nullptr); // 🚀 核心增加：消除分析器对空指针的可能警告
                    new (&m_data[sz]) T(val);
                    h->size = sz + 1;
                    return;
                }
            }
            grow_and_push(val);
        }

        inline void push_back(T&& val)
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
                    assert(m_data != nullptr); // 🚀 核心增加：消除空指针警报
                    new (&m_data[sz]) T(std::move(val));
                    h->size = sz + 1;
                    return;
                }
            }
            grow_and_push(std::move(val));
        }

        template <typename... Args>
        T& emplace_back(Args&&... args)
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
                    assert(m_data != nullptr); // 🚀 核心增加：消除空指针警报
                    new (&m_data[sz]) T(std::forward<Args>(args)...);
                    h->size = sz + 1;
                    return m_data[sz];
                }
            }
            T val(std::forward<Args>(args)...);
            grow_and_push(std::move(val));
            return back();
        }

        void pop_back() noexcept
        {
            uint32_t sz = size();
            if (sz > 0)
            {
                m_data[sz - 1].~T();
                get_header()->size = sz - 1;
            }
        }

        void clear() noexcept
        {
            if (m_data)
            {
                uint32_t sz = size();
                for (uint32_t i = 0; i < sz; ++i) { m_data[i].~T(); }
                get_header()->size = 0;
            }
        }

        void erase_unordered(const T& val) noexcept
        {
            uint32_t sz = size();
            for (uint32_t i = 0; i < sz; ++i)
            {
                if (m_data[i] == val)
                {
                    if (i != sz - 1) { m_data[i] = std::move(m_data[sz - 1]); }
                    m_data[sz - 1].~T();
                    get_header()->size = sz - 1;
                    return;
                }
            }
        }

        using iterator = T*;
        using const_iterator = const T*;

        [[nodiscard]] inline T* data() noexcept { return m_data; }
        [[nodiscard]] inline const T* data() const noexcept { return m_data; }

        [[nodiscard]] inline iterator begin() noexcept { return m_data; }
        [[nodiscard]] inline const_iterator begin() const noexcept { return m_data; }
        [[nodiscard]] inline iterator end() noexcept { return m_data + size(); }
        [[nodiscard]] inline const_iterator end() const noexcept { return m_data + size(); }

        [[nodiscard]] inline T& operator[](size_t idx) noexcept { return m_data[idx]; }
        [[nodiscard]] inline const T& operator[](size_t idx) const noexcept { return m_data[idx]; }

        [[nodiscard]] inline T& front() noexcept { return m_data[0]; }
        [[nodiscard]] inline const T& front() const noexcept { return m_data[0]; }
        [[nodiscard]] inline T& back() noexcept { return m_data[size() - 1]; }
        [[nodiscard]] inline const T& back() const noexcept { return m_data[size() - 1]; }

        iterator erase(const_iterator first, const_iterator last)
        {
            iterator f = const_cast<iterator>(first);
            iterator l = const_cast<iterator>(last);
            iterator end_it = end();
            if (f >= begin() && l <= end_it && f <= l)
            {
                size_t num_erased = l - f;
                std::move(l, end_it, f);
                get_header()->size -= static_cast<uint32_t>(num_erased);
            }
            return f;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 2. C++20/23 针对指针类型的偏特化实现（T*）
    // ─────────────────────────────────────────────────────────────────────────
    template <typename T>
    class TinyVector<T*>
    {
    private:
        struct Header
        {
            uint32_t capacity;
            uint32_t size;
        };

        static constexpr size_t Alignment = 16;
        static constexpr size_t HeaderOffset = 16;

        uintptr_t m_val = 0;

        // 🚀 降级为非 TLS 纯静态无锁复用池，释放 TLS 字段查找开销
        struct StaticCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~StaticCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    // 🚀 绝对路径定位
                    ::StuCanvas::utils::detail::aligned_free_helper(head, Alignment);
                    head = next;
                }
            }
        };

        static inline StaticCache s_cache2;
        static inline StaticCache s_cache4;

        [[nodiscard]] inline bool is_empty() const noexcept { return m_val == 0; }
        [[nodiscard]] inline bool is_single() const noexcept { return m_val != 0 && (m_val & 1) == 0; }
        [[nodiscard]] inline bool is_multi() const noexcept { return (m_val & 1) != 0; }

        [[nodiscard]] inline T* get_single() const noexcept
        {
            return reinterpret_cast<T*>(m_val);
        }

        [[nodiscard]] inline T** get_multi_array() const noexcept
        {
            return reinterpret_cast<T**>(m_val & ~static_cast<uintptr_t>(1));
        }

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!is_multi()) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(get_multi_array()) - HeaderOffset);
        }

        static T** allocate_heap(uint32_t capacity)
        {
            size_t total_size = HeaderOffset + static_cast<size_t>(capacity) * sizeof(T*);
            void* raw = nullptr;
            if (capacity == 2 && s_cache2.head) {
                raw = s_cache2.head;
                s_cache2.head = s_cache2.head->next;
                s_cache2.count--;
            } else if (capacity == 4 && s_cache4.head) {
                raw = s_cache4.head;
                s_cache4.head = s_cache4.head->next;
                s_cache4.count--;
            } else {
                raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_size, Alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            }
            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->size = 0;
            return reinterpret_cast<T**>(reinterpret_cast<char*>(raw) + HeaderOffset);
        }

        static void deallocate_heap(T** array) noexcept
        {
            if (!array) return;
            void* raw = reinterpret_cast<char*>(array) - HeaderOffset;
            Header* h = reinterpret_cast<Header*>(raw);
            uint32_t capacity = h->capacity;

            if (capacity == 2 && s_cache2.count < 128) {
                auto* node = reinterpret_cast<StaticCache::Node*>(raw); // 🚀 修复：移除了冗余的 typename
                node->next = s_cache2.head;
                s_cache2.head = node;
                s_cache2.count++;
            } else if (capacity == 4 && s_cache4.count < 64) {
                auto* node = reinterpret_cast<StaticCache::Node*>(raw); // 🚀 修复：移除了冗余的 typename
                node->next = s_cache4.head;
                s_cache4.head = node;
                s_cache4.count++;
            } else {
                ::StuCanvas::utils::detail::aligned_free_helper(raw, Alignment);
            }
        }

        STUCANVAS_NOINLINE void grow_and_push(T* val)
        {
            uint32_t sz = size();
            uint32_t next_cap = sz == 0 ? 2 : sz * 2;
            reserve(next_cap);
            T** array = get_multi_array();
            Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
            array[sz] = val;
            h->size = sz + 1;
        }

    public:
        TinyVector() noexcept = default;

        ~TinyVector() noexcept { clear(); }

        TinyVector(TinyVector&& other) noexcept : m_val(other.m_val) { other.m_val = 0; }

        TinyVector& operator=(TinyVector&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                m_val = other.m_val;
                other.m_val = 0;
            }
            return *this;
        }

        TinyVector(const TinyVector& other)
        {
            if (other.is_single())
            {
                m_val = other.m_val;
            }
            else if (other.is_multi())
            {
                Header* other_h = other.get_header();
                uint32_t cap = other_h->capacity;
                uint32_t sz = other_h->size;
                T** array = allocate_heap(cap);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                h->size = sz;

                T** other_array = other.get_multi_array();
                for (uint32_t i = 0; i < sz; ++i) { array[i] = other_array[i]; }
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
            }
        }

        TinyVector& operator=(const TinyVector& other)
        {
            if (this != &other)
            {
                clear();
                if (other.is_single())
                {
                    m_val = other.m_val;
                }
                else if (other.is_multi())
                {
                    Header* other_h = other.get_header();
                    uint32_t cap = other_h->capacity;
                    uint32_t sz = other_h->size;
                    T** array = allocate_heap(cap);
                    Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                    h->size = sz;

                    T** other_array = other.get_multi_array();
                    for (uint32_t i = 0; i < sz; ++i) { array[i] = other_array[i]; }
                    m_val = reinterpret_cast<uintptr_t>(array) | 1;
                }
            }
            return *this;
        }
// ─────────────────────────────────────────────────────────────────────────
    // 2. 指针偏特化版新增成员函数 (在 public 区域，clear() 之后添加)
    // ─────────────────────────────────────────────────────────────────────────
    public:
        /**
         * @brief 动态调整指针数组大小（哨兵状态智能转换）
         */
        void resize(uint32_t new_size)
        {
            uint32_t cur_size = size();
            if (new_size == cur_size) return;

            // 1. 大小缩减为 0，一键 clear 清空堆
            if (new_size == 0) {
                clear();
                return;
            }

            // 2. 极限压缩：大小缩减为 1，直接回收多元素堆空间，强行收缩为单元素（Tag 0）寄存器状态！
            if (new_size == 1) {
                if (is_multi()) {
                    T val = get_multi_array()[0];
                    deallocate_heap(get_multi_array());
                    m_val = reinterpret_cast<uintptr_t>(val);
                }
                return;
            }

            // 3. 多元素扩容/缩减
            if (new_size < cur_size) {
                // 缩减：指针为平凡析构类型，不需要循环调用 ~T()，直接修改 Header 尺寸状态
                Header* h = get_header();
                h->size = new_size;
            } else {
                // 扩容：就地 realloc，随后将新槽位极速清零（指针默认值为 nullptr）
                reserve(new_size);
                Header* h = get_header();
                T* array = get_multi_array();
                assert(array != nullptr); // 🚀 消除分析器空指针警告
                std::memset(reinterpret_cast<void*>(array + cur_size),
                            0,
                            (new_size - cur_size) * sizeof(T));
                h->size = new_size;
            }
        }

        /**
         * @brief 动态调整指针数组大小，并用 val 填充新槽位
         */
        void resize(uint32_t new_size, T val)
        {
            assert(val != nullptr && "Cannot store null pointer into TinyVector");
            uint32_t cur_size = size();
            if (new_size == cur_size) return;

            if (new_size == 0) {
                clear();
                return;
            }
            if (new_size == 1) {
                if (is_multi()) { deallocate_heap(get_multi_array()); }
                m_val = reinterpret_cast<uintptr_t>(val);
                return;
            }

            if (new_size < cur_size) {
                Header* h = get_header();
                h->size = new_size;
            } else {
                reserve(new_size);
                Header* h = get_header();
                T* array = get_multi_array();
                assert(array != nullptr);
                for (uint32_t i = cur_size; i < new_size; ++i) {
                    array[i] = val;
                }
                h->size = new_size;
            }
        }

        /**
         * @brief 将当前指针数组内容替换为 count 个拷贝的 val
         */
        void assign(uint32_t count, T val)
        {
            assert(val != nullptr && "Cannot store null pointer into TinyVector");
            clear();
            if (count == 1)
            {
                m_val = reinterpret_cast<uintptr_t>(val);
            }
            else if (count > 1)
            {
                T* array = allocate_heap(count);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                for (uint32_t i = 0; i < count; ++i) {
                    array[i] = val;
                }
                h->size = count;
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
            }
        }

        /**
         * @brief 将当前内容替换为区间 [first, last) 内的数据指针
         */
        void assign(const T* first, const T* last)
        {
            clear();
            if (first != last)
            {
                uint32_t count = static_cast<uint32_t>(last - first);
                if (count == 1)
                {
                    assert(first[0] != nullptr && "Cannot store null pointer into TinyVector");
                    m_val = reinterpret_cast<uintptr_t>(first[0]);
                }
                else
                {
                    T* array = allocate_heap(count);
                    Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                    // 🚀 指针特化：直接调用系统级极致优化 memcpy 传递指针数组
                    std::memcpy(reinterpret_cast<void*>(array),
                                reinterpret_cast<const void*>(first),
                                count * sizeof(T));
                    h->size = count;
                    m_val = reinterpret_cast<uintptr_t>(array) | 1;
                }
            }
        }

        [[nodiscard]] uint32_t size() const noexcept
        {
            if (is_empty()) return 0;
            if (is_single()) return 1;
            return get_header()->size;
        }

        [[nodiscard]] uint32_t capacity() const noexcept
        {
            if (is_empty()) return 0;
            if (is_single()) return 1;
            return get_header()->capacity;
        }

        [[nodiscard]] bool empty() const noexcept { return is_empty(); }

        void reserve(uint32_t new_cap)
        {
            uint32_t cur_cap = capacity();
            if (new_cap <= cur_cap) return;

            if (is_empty())
            {
                T** array = allocate_heap(new_cap);
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
                return;
            }
            if (is_single())
            {
                T* existing = get_single();
                T** array = allocate_heap(new_cap);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                array[0] = existing;
                h->size = 1;
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
                return;
            }

            T** old_array = get_multi_array();
            void* old_raw = reinterpret_cast<char*>(old_array) - HeaderOffset;
            Header* old_h = reinterpret_cast<Header*>(old_raw);
            uint32_t old_cap = old_h->capacity;
            uint32_t sz = old_h->size;

            if (old_cap <= 4) {
                T** new_array = allocate_heap(new_cap);
                Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_array) - HeaderOffset);
                new_h->size = sz;

                std::memcpy(new_array, old_array, sz * sizeof(T*));
                deallocate_heap(old_array);
                m_val = reinterpret_cast<uintptr_t>(new_array) | 1;
            } else {
                size_t old_total_size = HeaderOffset + static_cast<size_t>(old_cap) * sizeof(T*);
                size_t new_total_size = HeaderOffset + static_cast<size_t>(new_cap) * sizeof(T*);
                // 🚀 增加 NOLINT 抑制：告诉 Clang-Tidy 静态分析器，此处地址偏置不属于内存泄漏
                void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper(old_raw, old_total_size, new_total_size, Alignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

                Header* new_h = reinterpret_cast<Header*>(new_raw);
                new_h->capacity = new_cap;
                new_h->size = sz;

                T** new_array = reinterpret_cast<T**>(reinterpret_cast<char*>(new_raw) + HeaderOffset);
                m_val = reinterpret_cast<uintptr_t>(new_array) | 1;
            }
        }

        inline void push_back(T* val)
        {
            assert(val != nullptr && "Cannot store null pointer into TinyVector");

            uintptr_t v = m_val;
            if ((v & 1) != 0) [[likely]] {
                T** array = reinterpret_cast<T**>(v & ~static_cast<uintptr_t>(1));
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
                    assert(array != nullptr); // 🚀 核心增加：防止可能为空指针的误报
                    array[sz] = val;
                    h->size = sz + 1;
                    return;
                }
                grow_and_push(val);
                return;
            }
            if (v == 0) [[unlikely]] {
                m_val = reinterpret_cast<uintptr_t>(val);
                return;
            }
            T* existing = get_single();
            T** array = allocate_heap(2);
            Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
            array[0] = existing;
            array[1] = val;
            h->size = 2;
            m_val = reinterpret_cast<uintptr_t>(array) | 1;
        }

        template <typename... Args>
        T*& emplace_back(Args&&... args)
        {
            T* val(std::forward<Args>(args)...);
            push_back(val);
            return back();
        }

        void pop_back() noexcept
        {
            if (is_empty()) return;
            if (is_single())
            {
                m_val = 0;
                return;
            }
            Header* h = get_header();
            uint32_t sz = h->size;
            if (sz > 0) { h->size = sz - 1; }
        }

        void clear() noexcept
        {
            if (is_multi()) { deallocate_heap(get_multi_array()); }
            m_val = 0;
        }

        void erase_unordered(T* val) noexcept
        {
            if (is_empty()) return;
            if (is_single())
            {
                if (get_single() == val) { m_val = 0; }
                return;
            }

            Header* h = get_header();
            uint32_t sz = h->size;
            T** array = get_multi_array();
            for (uint32_t i = 0; i < sz; ++i)
            {
                if (array[i] == val)
                {
                    if (i != sz - 1) { array[i] = std::move(array[sz - 1]); }
                    h->size = sz - 1;
                    return;
                }
            }
        }

        using iterator = T**;
        using const_iterator = const T* const*;

        [[nodiscard]] inline T** data() noexcept
        {
            uintptr_t v = m_val;
            if (v == 0) return nullptr;
            if ((v & 1) == 0) { return reinterpret_cast<T**>(&m_val); }
            return get_multi_array();
        }

        [[nodiscard]] inline const T* const* data() const noexcept
        {
            uintptr_t v = m_val;
            if (v == 0) return nullptr;
            if ((v & 1) == 0) { return reinterpret_cast<const T* const*>(&m_val); }
            return reinterpret_cast<const T* const*>(v & ~static_cast<uintptr_t>(1));
        }

        [[nodiscard]] inline iterator begin() noexcept { return data(); }
        [[nodiscard]] inline const_iterator begin() const noexcept { return data(); }
        [[nodiscard]] inline iterator end() noexcept
        {
            T** d = data();
            return d ? d + size() : nullptr;
        }
        [[nodiscard]] inline const_iterator end() const noexcept
        {
            const T* const* d = data();
            return d ? d + size() : nullptr;
        }

        [[nodiscard]] inline T*& operator[](size_t idx) noexcept
        {
            assert(idx < size() && "Index out of bounds");
            return data()[idx];
        }

        [[nodiscard]] inline T* const& operator[](size_t idx) const noexcept
        {
            assert(idx < size() && "Index out of bounds");
            return data()[idx];
        }

        [[nodiscard]] inline T*& front() noexcept { return data()[0]; }
        [[nodiscard]] inline T* const& front() const noexcept { return data()[0]; }
        [[nodiscard]] inline T*& back() noexcept { return data()[size() - 1]; }
        [[nodiscard]] inline T* const& back() const noexcept { return data()[size() - 1]; }

        iterator erase(const_iterator first, const_iterator last)
        {
            iterator f = const_cast<iterator>(first);
            iterator l = const_cast<iterator>(last);
            iterator end_it = end();
            if (f >= begin() && l <= end_it && f <= l)
            {
                size_t num_erased = l - f;
                std::move(l, end_it, f);
                get_header()->size -= static_cast<uint32_t>(num_erased);
            }
            return f;
        }
    };
} // namespace StuCanvas::utils