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
            _aligned_free(ptr); // Windows 专属释放
#else
            std::free(ptr);     // POSIX 专属释放
#endif
#endif
        }
        // 🚀 修复版：接受旧尺寸参数，杜绝 std::memcpy 越界读取
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
            void* new_ptr = aligned_alloc_helper(new_size, alignment);
            if (ptr) {
                // 🚀 核心修复：仅拷贝合法旧数据尺寸，安全消除 SIGSEGV 段错误！
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

        // 🚀 锁定为标准 max_align_t（16 字节对齐），消灭堆空隙的同时提供完美的 STL 级对齐
        static constexpr size_t Alignment = 16;
        static constexpr size_t HeaderOffset = 16;

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
                raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_size, Alignment);
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
                auto* node = reinterpret_cast<typename ThreadCache::Node*>(raw);
                node->next = s_cache2.head;
                s_cache2.head = node;
                s_cache2.count++;
            } else if (capacity == 4 && s_cache4.count < 64) {
                auto* node = reinterpret_cast<typename ThreadCache::Node*>(raw);
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
            m_data[sz] = val;
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
                    void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper(old_raw, old_total_size, new_total_size, Alignment);
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
                    m_data[sz] = val;
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
                    m_data[sz] = std::move(val);
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
    // 2. C++23 针对指针类型键（K = Pointer）的特化实现
    // ─────────────────────────────────────────────────────────────────────────
    template <typename T>
    requires std::is_pointer_v<T>
    class TinyVector<T>
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

        struct StaticCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~StaticCache() noexcept {
                while (head) {
                    Node* next = head->next;
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

        [[nodiscard]] inline T get_single() const noexcept
        {
            return reinterpret_cast<T>(m_val);
        }

        [[nodiscard]] inline T* get_multi_array() const noexcept
        {
            return reinterpret_cast<T*>(m_val & ~static_cast<uintptr_t>(1));
        }

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!is_multi()) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(get_multi_array()) - HeaderOffset);
        }

        static T* allocate_heap(uint32_t capacity)
        {
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
                raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_size, Alignment);
            }
            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->size = 0;
            return reinterpret_cast<T*>(reinterpret_cast<char*>(raw) + HeaderOffset);
        }

        static void deallocate_heap(T* array) noexcept
        {
            if (!array) return;
            void* raw = reinterpret_cast<char*>(array) - HeaderOffset;
            Header* h = reinterpret_cast<Header*>(raw);
            uint32_t capacity = h->capacity;

            if (capacity == 2 && s_cache2.count < 128) {
                auto* node = reinterpret_cast<typename StaticCache::Node*>(raw);
                node->next = s_cache2.head;
                s_cache2.head = node;
                s_cache2.count++;
            } else if (capacity == 4 && s_cache4.count < 64) {
                auto* node = reinterpret_cast<typename StaticCache::Node*>(raw);
                node->next = s_cache4.head;
                s_cache4.head = node;
                s_cache4.count++;
            } else {
                ::StuCanvas::utils::detail::aligned_free_helper(raw, Alignment);
            }
        }

        STUCANVAS_NOINLINE void grow_and_push(T val)
        {
            uint32_t sz = size();
            uint32_t next_cap = sz == 0 ? 2 : sz * 2;
            reserve(next_cap);
            T* array = get_multi_array();
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
                T* array = allocate_heap(cap);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                h->size = sz;

                T* other_array = other.get_multi_array();
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
                    T* array = allocate_heap(cap);
                    Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                    h->size = sz;

                    T* other_array = other.get_multi_array();
                    for (uint32_t i = 0; i < sz; ++i) { array[i] = other_array[i]; }
                    m_val = reinterpret_cast<uintptr_t>(array) | 1;
                }
            }
            return *this;
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
                T* array = allocate_heap(new_cap);
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
                return;
            }
            if (is_single())
            {
                T existing = get_single();
                T* array = allocate_heap(new_cap);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                array[0] = existing;
                h->size = 1;
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
                return;
            }

            T* old_array = get_multi_array();
            void* old_raw = reinterpret_cast<char*>(old_array) - HeaderOffset;
            Header* old_h = reinterpret_cast<Header*>(old_raw);
            uint32_t old_cap = old_h->capacity;
            uint32_t sz = old_h->size;

            if (old_cap <= 4) {
                T* new_array = allocate_heap(new_cap);
                Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_array) - HeaderOffset);
                new_h->size = sz;

                std::memcpy(new_array, old_array, sz * sizeof(T));
                deallocate_heap(old_array);
                m_val = reinterpret_cast<uintptr_t>(new_array) | 1;
            } else {
                size_t old_total_size = HeaderOffset + static_cast<size_t>(old_cap) * sizeof(T);
                size_t new_total_size = HeaderOffset + static_cast<size_t>(new_cap) * sizeof(T);
                void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper(old_raw, old_total_size, new_total_size, Alignment);

                Header* new_h = reinterpret_cast<Header*>(new_raw);
                new_h->capacity = new_cap;
                new_h->size = sz;

                T* new_array = reinterpret_cast<T*>(reinterpret_cast<char*>(new_raw) + HeaderOffset);
                m_val = reinterpret_cast<uintptr_t>(new_array) | 1;
            }
        }

        inline void push_back(T val)
        {
            assert(val != nullptr && "Cannot store null pointer into TinyVector");

            uintptr_t v = m_val;
            if ((v & 1) != 0) [[likely]] {
                T* array = reinterpret_cast<T*>(v & ~static_cast<uintptr_t>(1));
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
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
            T existing = get_single();
            T* array = allocate_heap(2);
            Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
            array[0] = existing;
            array[1] = val;
            h->size = 2;
            m_val = reinterpret_cast<uintptr_t>(array) | 1;
        }

        template <typename... Args>
        T& emplace_back(Args&&... args)
        {
            T val(std::forward<Args>(args)...);
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

        void erase_unordered(T val) noexcept
        {
            if (is_empty()) return;
            if (is_single())
            {
                if (get_single() == val) { m_val = 0; }
                return;
            }

            Header* h = get_header();
            uint32_t sz = h->size;
            T* array = get_multi_array();
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

        using iterator = T*;
        using const_iterator = const T*;

        [[nodiscard]] inline T* data() noexcept
        {
            uintptr_t v = m_val;
            if (v == 0) return nullptr;
            if ((v & 1) == 0) { return reinterpret_cast<T*>(&m_val); }
            return reinterpret_cast<T*>(v & ~static_cast<uintptr_t>(1));
        }

        [[nodiscard]] inline const T* data() const noexcept
        {
            uintptr_t v = m_val;
            if (v == 0) return nullptr;
            if ((v & 1) == 0) { return reinterpret_cast<const T*>(&m_val); }
            return reinterpret_cast<const T*>(v & ~static_cast<uintptr_t>(1));
        }

        [[nodiscard]] inline iterator begin() noexcept { return data(); }
        [[nodiscard]] inline const_iterator begin() const noexcept { return data(); }
        [[nodiscard]] inline iterator end() noexcept
        {
            T* d = data();
            return d ? d + size() : nullptr;
        }
        [[nodiscard]] inline const_iterator end() const noexcept
        {
            const T* d = data();
            return d ? d + size() : nullptr;
        }

        [[nodiscard]] inline T& operator[](size_t idx) noexcept
        {
            assert(idx < size() && "Index out of bounds");
            return data()[idx];
        }

        [[nodiscard]] inline const T& operator[](size_t idx) const noexcept
        {
            assert(idx < size() && "Index out of bounds");
            return data()[idx];
        }

        [[nodiscard]] inline T& front() noexcept { return data()[0]; }
        [[nodiscard]] inline const T& front() const noexcept { return data()[0]; }
        [[nodiscard]] inline T& back() noexcept { return data()[size() - 1]; }
        [[nodiscard]] inline const T& back() const noexcept { return data()[size() - 1]; }

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