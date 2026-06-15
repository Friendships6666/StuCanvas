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
#include "tiny_vector.hpp" // 复用我们先前实现的极致跨平台对齐内存分配器

namespace StuCanvas::utils
{
    // ─────────────────────────────────────────────────────────────────────────
    // 1. 通用版 BlockDeque 声明（支持所有类型，安全支持非平凡类对象）
    // ─────────────────────────────────────────────────────────────────────────
    template <typename T, size_t BlockSize = 1024>
    class BlockDeque
    {
        static_assert((BlockSize & (BlockSize - 1)) == 0, "BlockSize must be a power of 2!");

    private:
        struct Header {
            uint32_t capacity;
            uint32_t num_blocks;
            uint64_t total_size;
        };

        static constexpr size_t ArrayAlignment = alignof(T*) > 16 ? alignof(T*) : 16;
        static constexpr size_t HeaderOffset = (sizeof(Header) + ArrayAlignment - 1) & ~(ArrayAlignment - 1);

        static constexpr size_t ElementAlignment = alignof(T) > 16 ? alignof(T) : 16;

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

        T** m_blocks = nullptr;

        // 🚀 极致性能：纯 static 静态复用池，消灭 TLS 段查找延迟
        struct StaticBlockCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~StaticBlockCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    ::StuCanvas::utils::detail::aligned_free_helper(head, ElementAlignment);
                    head = next;
                }
            }
        };

        static inline StaticBlockCache s_block_cache;

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_blocks) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_blocks) - HeaderOffset);
        }

        static T** allocate_blocks_array(uint32_t capacity)
        {
            if (capacity == 0) return nullptr;
            size_t total_bytes = HeaderOffset + static_cast<size_t>(capacity) * sizeof(T*);
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_bytes, ArrayAlignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

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
            ::StuCanvas::utils::detail::aligned_free_helper(raw, ArrayAlignment);
        }

        static T* allocate_element_block()
        {
            if (s_block_cache.head) {
                void* raw = s_block_cache.head;
                s_block_cache.head = s_block_cache.head->next;
                s_block_cache.count--;
                return static_cast<T*>(raw);
            }
            size_t bytes = BlockSize * sizeof(T);
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper(bytes, ElementAlignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            return static_cast<T*>(raw);
        }

        static void deallocate_element_block(T* ptr) noexcept
        {
            if (!ptr) return;
            if (s_block_cache.count < 32) {
                auto* node = reinterpret_cast<StaticBlockCache::Node*>(ptr);
                node->next = s_block_cache.head;
                s_block_cache.head = node;
                s_block_cache.count++;
            } else {
                ::StuCanvas::utils::detail::aligned_free_helper(ptr, ElementAlignment);
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

        STUCANVAS_NOINLINE void grow_and_push(const T& val)
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
            assert(m_blocks != nullptr);
            assert(m_blocks[current_block_idx] != nullptr);
            new (&m_blocks[current_block_idx][element_idx]) T(val);
            h->total_size = total_size + 1;
        }

        STUCANVAS_NOINLINE void grow_and_push(T&& val)
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
            assert(m_blocks != nullptr);
            assert(m_blocks[current_block_idx] != nullptr);
            new (&m_blocks[current_block_idx][element_idx]) T(std::move(val));
            h->total_size = total_size + 1;
        }

    public:
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

        BlockDeque() { add_new_block(); }

        ~BlockDeque() noexcept
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

        BlockDeque(const BlockDeque&) = delete;
        BlockDeque& operator=(const BlockDeque&) = delete;

        BlockDeque(BlockDeque&& other) noexcept : m_blocks(other.m_blocks)
        {
            other.m_blocks = nullptr;
        }

        BlockDeque& operator=(BlockDeque&& other) noexcept
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
            assert(m_blocks != nullptr);
            assert(m_blocks[block_idx] != nullptr);
            return m_blocks[block_idx][element_idx];
        }

        const T& operator[](size_t index) const noexcept
        {
            const size_t block_idx = index >> Shift;
            const size_t element_idx = index & Mask;
            assert(m_blocks != nullptr);
            assert(m_blocks[block_idx] != nullptr);
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

        inline void push_back(const T& val)
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint64_t total_size = h->total_size;
                const size_t element_idx = total_size & Mask;
                if (element_idx != 0) [[likely]] {
                    const size_t current_block_idx = total_size >> Shift;
                    assert(m_blocks != nullptr);
                    assert(m_blocks[current_block_idx] != nullptr);
                    new (&m_blocks[current_block_idx][element_idx]) T(val);
                    h->total_size = total_size + 1;
                    return;
                }
            }
            grow_and_push(val);
        }

        template <typename... Args>
        T& emplace_back(Args&&... args)
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint64_t total_size = h->total_size;
                const size_t element_idx = total_size & Mask;
                if (element_idx != 0) [[likely]] {
                    const size_t current_block_idx = total_size >> Shift;
                    assert(m_blocks != nullptr);
                    assert(m_blocks[current_block_idx] != nullptr);
                    new (&m_blocks[current_block_idx][element_idx]) T(std::forward<Args>(args)...);
                    h->total_size = total_size + 1;
                    return m_blocks[current_block_idx][element_idx];
                }
            }
            grow_and_push(T(std::forward<Args>(args)...));
            return back();
        }

        void pop_back() noexcept
        {
            Header* h = get_header();
            if (!h || h->total_size == 0) return;

            uint64_t total_size = h->total_size;
            const size_t current_block_idx = (total_size - 1) >> Shift;
            const size_t element_idx = (total_size - 1) & Mask;

            if constexpr (!std::is_trivially_destructible_v<T>) {
                m_blocks[current_block_idx][element_idx].~T();
            }
            h->total_size = total_size - 1;

            size_t active_blocks = (h->total_size + BlockSize - 1) >> Shift;
            if (active_blocks == 0) active_blocks = 1;

            if (h->num_blocks > active_blocks + 1) {
                deallocate_element_block(m_blocks[h->num_blocks - 1]);
                h->num_blocks = h->num_blocks - 1;
            }
        }

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

        [[nodiscard]] inline T* const* m_blocks_exposed() const noexcept { return m_blocks; }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 2. C++20/23 针对指针类型的概念偏特化实现（requires std::is_pointer_v<T>）
    // 🚀 核心更正：通过概念来修饰 T，使得 T 在类内部保持原生的 int* 指针类型，彻底解决参数推导与别名冲突
    // ─────────────────────────────────────────────────────────────────────────
    template <typename T, size_t BlockSize>
    requires std::is_pointer_v<T> // C++20 概念偏特化
    class BlockDeque<T, BlockSize>
    {
        static_assert(sizeof(T) == 8, "PtrBlockDeque can only store 8-byte pointer types!");
        static_assert((BlockSize & (BlockSize - 1)) == 0, "BlockSize must be a power of 2!");

    private:
        struct Header {
            uint32_t capacity;
            uint32_t num_blocks;
            uint64_t total_size;
        };

        static constexpr size_t ElementAlignment = 64;
        static constexpr size_t HeaderOffset = (sizeof(Header) + alignof(T) - 1) & ~(alignof(T) - 1);

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

        // 🚀 核心优化：T 为 int*，因此 T** 是完美的 int***
        T** m_blocks = nullptr;

        struct StaticBlockCache {
            struct Node {
                Node* next;
            };
            Node* head = nullptr;
            size_t count = 0;

            ~StaticBlockCache() noexcept {
                while (head) {
                    Node* next = head->next;
                    ::StuCanvas::utils::detail::aligned_free_helper(head, ElementAlignment);
                    head = next;
                }
            }
        };

        static inline StaticBlockCache s_block_cache;

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_blocks) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_blocks) - HeaderOffset);
        }

        static T** allocate_blocks_array(uint32_t capacity)
        {
            if (capacity == 0) return nullptr;
            size_t total_bytes = HeaderOffset + static_cast<size_t>(capacity) * sizeof(T);
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper(total_bytes, alignof(T)); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

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
            ::StuCanvas::utils::detail::aligned_free_helper(raw, alignof(T));
        }

        static T* allocate_element_block()
        {
            if (s_block_cache.head) {
                void* raw = s_block_cache.head;
                s_block_cache.head = s_block_cache.head->next;
                s_block_cache.count--;
                return static_cast<T*>(raw);
            }
            size_t bytes = BlockSize * sizeof(T);
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper(bytes, ElementAlignment); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
            return static_cast<T*>(raw);
        }

        static void deallocate_element_block(T* ptr) noexcept
        {
            if (!ptr) return;
            if (s_block_cache.count < 32) {
                auto* node = reinterpret_cast<StaticBlockCache::Node*>(ptr); // 🚀 移除了多余的 typename
                node->next = s_block_cache.head;
                s_block_cache.head = node;
                s_block_cache.count++;
            } else {
                ::StuCanvas::utils::detail::aligned_free_helper(ptr, ElementAlignment);
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

        STUCANVAS_NOINLINE void grow_and_push(T val) noexcept
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
            assert(m_blocks != nullptr);
            assert(m_blocks[current_block_idx] != nullptr);
            m_blocks[current_block_idx][element_idx] = val;
            h->total_size = total_size + 1;
        }

    public:
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;          // 🚀 此时 T 为 int* 指针类型，完全对齐！
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            T* ptr = nullptr;
            T* block_end = nullptr;
            T** current_block = nullptr;

            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }

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

        // 🚀 C++20 概念偏特化下，类名保持原生 BlockDeque，杜绝一切编译期符号重名冲突
        BlockDeque() { add_new_block(); }

        ~BlockDeque() noexcept
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

        BlockDeque(const BlockDeque&) = delete;
        BlockDeque& operator=(const BlockDeque&) = delete;

        BlockDeque(BlockDeque&& other) noexcept : m_blocks(other.m_blocks)
        {
            other.m_blocks = nullptr;
        }

        BlockDeque& operator=(BlockDeque&& other) noexcept
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
            assert(m_blocks != nullptr);
            assert(m_blocks[block_idx] != nullptr);
            return m_blocks[block_idx][element_idx];
        }

        const T& operator[](size_t index) const noexcept
        {
            const size_t block_idx = index >> Shift;
            const size_t element_idx = index & Mask;
            assert(m_blocks != nullptr);
            assert(m_blocks[block_idx] != nullptr);
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

        inline void push_back(T val) noexcept
        {
            Header* h = get_header();
            if (h) [[likely]] {
                uint64_t total_size = h->total_size;
                const size_t element_idx = total_size & Mask;

                if (element_idx != 0) [[likely]] {
                    const size_t current_block_idx = total_size >> Shift;
                    assert(m_blocks != nullptr);
                    assert(m_blocks[current_block_idx] != nullptr);
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

        [[nodiscard]] inline T* const* m_blocks_exposed() const noexcept { return m_blocks; }
    };
} // namespace StuCanvas::utils