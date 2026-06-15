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

// 针对 Windows MSVC 平台和 GCC 平台的极致内联控制与对齐保护
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
        // 跨平台高自适应对齐分配器（完美兼容 MSVC & GCC/Clang）
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

    template <typename T>
    class PtrVector
    {
        // 编译期双重安全自检：非指针类型直接报错、非 64 位（8字节）指针直接报错
        static_assert(std::is_pointer_v<T>, "PtrVector can only store pointer types!");
        static_assert(sizeof(T) == 8, "PtrVector can only store 8-byte (64-bit OS) pointer types!");

    private:
        struct Header
        {
            uint32_t capacity;
            uint32_t size;
        };

        // 🚀 黄金平衡对齐：锁定为 16 字节对齐，既保障 SSE/AVX 满速向量化加载，又将堆上 Padding 压缩到极限的 8 字节
        static constexpr size_t Alignment = 16;
        static constexpr size_t HeaderOffset = 16;

        // 类内唯一的 8 字节物理数值
        uintptr_t m_val = 0;

        struct ThreadCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~ThreadCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    detail::aligned_free_helper(head, Alignment);
                    head = next;
                }
            }
        };

        static inline thread_local ThreadCache t_cache2;
        static inline thread_local ThreadCache t_cache4;

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
            if (capacity == 2 && t_cache2.head) {
                raw = t_cache2.head;
                t_cache2.head = t_cache2.head->next;
                t_cache2.count--;
            } else if (capacity == 4 && t_cache4.head) {
                raw = t_cache4.head;
                t_cache4.head = t_cache4.head->next;
                t_cache4.count--;
            } else {
                raw = detail::aligned_alloc_helper(total_size, Alignment);
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

            if (capacity == 2 && t_cache2.count < 64) {
                auto* node = reinterpret_cast<typename ThreadCache::Node*>(raw);
                node->next = t_cache2.head;
                t_cache2.head = node;
                t_cache2.count++;
            } else if (capacity == 4 && t_cache4.count < 32) {
                auto* node = reinterpret_cast<typename ThreadCache::Node*>(raw);
                node->next = t_cache4.head;
                t_cache4.head = node;
                t_cache4.count++;
            } else {
                detail::aligned_free_helper(raw, Alignment);
            }
        }

        // 🚀 析离冷路径（Outlined Cold Path），彻底释放主循环寄存器
        STUCANVAS_NOINLINE void grow_and_push(T val)
        {
            uint32_t sz = size();
            reserve(sz == 0 ? 2 : static_cast<uint32_t>(sz * 2));
            Header* h = get_header();
            T* array = get_multi_array();
            array[sz] = val;
            h->size = sz + 1;
        }

    public:
        // ─────────────────────────────────────────────────────────────────────
        // 构造与析构
        // ─────────────────────────────────────────────────────────────────────
        PtrVector() noexcept = default;

        ~PtrVector() noexcept { clear(); }

        PtrVector(PtrVector&& other) noexcept : m_val(other.m_val) { other.m_val = 0; }

        PtrVector& operator=(PtrVector&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                m_val = other.m_val;
                other.m_val = 0;
            }
            return *this;
        }

        PtrVector(const PtrVector& other)
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

        PtrVector& operator=(const PtrVector& other)
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

        // ─────────────────────────────────────────────────────────────────────
        // 状态与随机访问
        // ─────────────────────────────────────────────────────────────────────
        [[nodiscard]] inline uint32_t size() const noexcept
        {
            if (is_empty()) return 0;
            if (is_single()) return 1;
            return get_header()->size;
        }

        [[nodiscard]] inline uint32_t capacity() const noexcept
        {
            if (is_empty()) return 0;
            if (is_single()) return 1;
            return get_header()->capacity;
        }

        [[nodiscard]] inline bool empty() const noexcept { return is_empty(); }

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
            Header* old_h = get_header();
            uint32_t sz = old_h->size;

            T* new_array = allocate_heap(new_cap);
            Header* new_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(new_array) - HeaderOffset);
            new_h->size = sz;

            for (uint32_t i = 0; i < sz; ++i) { new_array[i] = old_array[i]; }
            deallocate_heap(old_array);
            m_val = reinterpret_cast<uintptr_t>(new_array) | 1;
        }

        // 🚀 极致优化热路径：5 条汇编，直接写寄存器
        inline void push_back(T val)
        {
            assert(val != nullptr && "Cannot store null pointer into PtrVector");

            if (is_empty()) [[unlikely]]
            {
                m_val = reinterpret_cast<uintptr_t>(val);
                return;
            }

            if (is_single()) [[unlikely]]
            {
                T existing = get_single();
                T* array = allocate_heap(2);
                Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(array) - HeaderOffset);
                array[0] = existing;
                array[1] = val;
                h->size = 2;
                m_val = reinterpret_cast<uintptr_t>(array) | 1;
                return;
            }

            Header* h = get_header();
            if (h) [[likely]] {
                uint32_t sz = h->size;
                if (sz < h->capacity) [[likely]] {
                    T* array = get_multi_array();
                    array[sz] = val;
                    h->size = sz + 1;
                    return;
                }
            }
            grow_and_push(val);
        }

        template <typename... Args>
        T& emplace_back(Args&&... args)
        {
            T val(std::forward<Args>(args)...);
            push_back(val);
            return back();
        }

        inline void pop_back() noexcept
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

        // 🚀 零开销清空：一键抹除指针，0 元素析构循环开销
        inline void clear() noexcept
        {
            if (is_multi()) { deallocate_heap(get_multi_array()); }
            m_val = 0;
        }

        inline void erase_unordered(T val) noexcept
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
                    if (i != sz - 1) { array[i] = array[sz - 1]; }
                    h->size = sz - 1;
                    return;
                }
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // 4. 标准迭代器与物理指针（&m_val 的绝对正确寻址）
        // ─────────────────────────────────────────────────────────────────────
        using iterator = T*;
        using const_iterator = const T*;

        [[nodiscard]] inline T* data() noexcept
        {
            if (is_empty()) return nullptr;

            // 因为单指针模式下 Tag 为 0，低位无标记，物理内存结构与裸指针等价
            // 因此 &m_val 是一个 100% 合法的 T* 迭代器起点，真·零堆寻址！
            if (is_single()) { return reinterpret_cast<T*>(&m_val); }

            return get_multi_array();
        }

        [[nodiscard]] inline const T* data() const noexcept
        {
            if (is_empty()) return nullptr;
            if (is_single()) { return reinterpret_cast<const T*>(&m_val); }
            return get_multi_array();
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