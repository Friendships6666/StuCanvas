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
#include <functional>
#include "tiny_vector.hpp" // 复用我们先前实现的极致跨平台对齐内存分配器

namespace StuCanvas::utils
{
    // ─────────────────────────────────────────────────────────────────────────
    // 1. 通用版 FlatMap 模版（非指针类型走这个默认模版）
    // ─────────────────────────────────────────────────────────────────────────
    template <typename K, typename V>
    class FlatMap
    {
    public:
        enum class State : uint8_t { EMPTY, FILLED, DELETED };

        struct Entry {
            K first;
            V second;
            State state = State::EMPTY;
        };

    private:
        struct Header {
            uint32_t capacity;
            uint32_t size;
        };

        static constexpr size_t Alignment = alignof(Entry) > alignof(std::max_align_t)
                                            ? alignof(Entry)
                                            : alignof(std::max_align_t);
        static constexpr size_t HeaderOffset = (sizeof(Header) + Alignment - 1) & ~(Alignment - 1);

        Entry* m_table = nullptr;

        static size_t hash_key(const K& key) noexcept {
            uint64_t x = static_cast<uint64_t>(std::hash<K>{}(key));
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            x = x ^ (x >> 31);
            return static_cast<size_t>(x);
        }

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_table) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_table) - HeaderOffset);
        }

        static Entry* allocate_table(uint32_t capacity)
        {
            size_t total_size = HeaderOffset + static_cast<size_t>(capacity) * sizeof(Entry);
            void* raw = detail::aligned_alloc_helper(total_size, Alignment);

            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->size = 0;

            Entry* table = reinterpret_cast<Entry*>(reinterpret_cast<char*>(raw) + HeaderOffset);
            for (uint32_t i = 0; i < capacity; ++i) {
                new (&table[i]) Entry();
            }
            return table;
        }

        static void deallocate_table(Entry* table) noexcept
        {
            if (!table) return;
            Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(table) - HeaderOffset);
            uint32_t capacity = h->capacity;
            for (uint32_t i = 0; i < capacity; ++i) {
                table[i].~Entry();
            }
            void* raw = reinterpret_cast<char*>(table) - HeaderOffset;
            detail::aligned_free_helper(raw, Alignment);
        }

        void rehash(size_t new_capacity)
        {
            Header* old_h = get_header();
            uint32_t old_cap = old_h ? old_h->capacity : 0;
            Entry* old_table = m_table;

            m_table = allocate_table(static_cast<uint32_t>(new_capacity));

            if (old_table) {
                for (uint32_t i = 0; i < old_cap; ++i) {
                    if (old_table[i].state == State::FILLED) {
                        insert(old_table[i].first, std::move(old_table[i].second));
                    }
                }
                deallocate_table(old_table);
            }
        }

    public:
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entry;
            using difference_type = std::ptrdiff_t;
            using pointer = Entry*;
            using reference = Entry&;

            pointer ptr;
            pointer end_ptr;

            Iterator(pointer p, pointer end) noexcept : ptr(p), end_ptr(end) {
                if (ptr != end_ptr && ptr->state != State::FILLED) { ++(*this); }
            }
            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }
            Iterator& operator++() noexcept {
                do { ptr++; } while (ptr != end_ptr && ptr->state != State::FILLED);
                return *this;
            }
            Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }
            bool operator==(const Iterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const Iterator& other) const noexcept { return ptr != other.ptr; }
        };

        struct ConstIterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = const Entry;
            using difference_type = std::ptrdiff_t;
            using pointer = const Entry*;
            using reference = const Entry&; // 🚀 修复：修正引用别名为正确的 const Entry&

            pointer ptr;
            pointer end_ptr;

            ConstIterator(pointer p, pointer end) noexcept : ptr(p), end_ptr(end) {
                if (ptr != end_ptr && ptr->state != State::FILLED) { ++(*this); }
            }
            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }
            ConstIterator& operator++() noexcept {
                do { ptr++; } while (ptr != end_ptr && ptr->state != State::FILLED);
                return *this;
            }
            ConstIterator operator++(int) noexcept { ConstIterator tmp = *this; ++(*this); return tmp; }
            bool operator==(const ConstIterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const ConstIterator& other) const noexcept { return ptr != other.ptr; }
        };

        // 🚀 修复：将 std 容器标准的迭代器别名指向正确的自定义包装类，而非裸指针
        using iterator = Iterator;
        using const_iterator = ConstIterator;

        FlatMap(size_t initial_capacity = 16)
        {
            size_t cap = 1;
            while (cap < initial_capacity) cap <<= 1;
            m_table = allocate_table(static_cast<uint32_t>(cap));
        }

        ~FlatMap() noexcept { if (m_table) deallocate_table(m_table); }

        FlatMap(FlatMap&& other) noexcept : m_table(other.m_table) { other.m_table = nullptr; }

        FlatMap& operator=(FlatMap&& other) noexcept
        {
            if (this != &other) {
                if (m_table) deallocate_table(m_table);
                m_table = other.m_table;
                other.m_table = nullptr;
            }
            return *this;
        }

        FlatMap(const FlatMap&) = delete;
        FlatMap& operator=(const FlatMap&) = delete;

        iterator begin() noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return iterator(m_table, m_table + cap);
        }
        iterator end() noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return iterator(m_table + cap, m_table + cap);
        }

        const_iterator begin() const noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return const_iterator(m_table, m_table + cap);
        }
        const_iterator end() const noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return const_iterator(m_table + cap, m_table + cap);
        }

        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        void insert(K key, V value)
        {
            Header* h = get_header();
            uint32_t capacity = h ? h->capacity : 0;
            uint32_t size = h ? h->size : 0;

            if (capacity == 0) {
                rehash(16);
                h = get_header();
                capacity = h->capacity;
            } else if (size * 10 > capacity * 7) {
                rehash(capacity * 2);
                h = get_header();
                capacity = h->capacity;
            }

            size_t mask = capacity - 1;
            size_t slot = hash_key(key) & mask;
            while (m_table[slot].state == State::FILLED) {
                if (m_table[slot].first == key) {
                    m_table[slot].second = std::move(value);
                    return;
                }
                slot = (slot + 1) & mask;
            }
            m_table[slot].first = key;
            m_table[slot].second = std::move(value);
            m_table[slot].state = State::FILLED;
            h->size++;
        }

        iterator find(K key) noexcept
        {
            if (!m_table) return end();
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].state != State::EMPTY) {
                if (m_table[slot].state == State::FILLED && m_table[slot].first == key) {
                    return iterator(&m_table[slot], m_table + h->capacity);
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return end();
        }

        const_iterator find(K key) const noexcept
        {
            if (!m_table) return end();
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].state != State::EMPTY) {
                if (m_table[slot].state == State::FILLED && m_table[slot].first == key) {
                    return const_iterator(&m_table[slot], m_table + h->capacity);
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return end();
        }

        bool contains(K key) const noexcept
        {
            if (!m_table) return false;
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].state != State::EMPTY) {
                if (m_table[slot].state == State::FILLED && m_table[slot].first == key) {
                    return true;
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return false;
        }

        bool erase(K key) noexcept
        {
            if (!m_table) return false;
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            while (m_table[slot].state != State::EMPTY) {
                if (m_table[slot].state == State::FILLED && m_table[slot].first == key) {
                    m_table[slot].state = State::DELETED;
                    m_table[slot].second.~V();
                    new (&m_table[slot].second) V();
                    h->size--;
                    return true;
                }
                slot = (slot + 1) & mask;
            }
            return false;
        }

        // 🚀 新增：支持通过迭代器快速擦除，并返回下一个有效槽迭代器
        iterator erase(const_iterator pos) noexcept
        {
            if (pos.ptr == nullptr || !m_table) return end();
            Header* h = get_header();
            Entry* end_ptr = m_table + h->capacity;
            if (pos.ptr >= end_ptr || pos.ptr < m_table) return end();

            Entry* p = const_cast<Entry*>(pos.ptr);
            if (p->state == State::FILLED) {
                p->state = State::DELETED;
                p->second.~V();
                new (&p->second) V();
                h->size--;
            }

            // 顺延寻找下一个非空槽并返回
            do {
                p++;
            } while (p != end_ptr && p->state != State::FILLED);
            return iterator(p, end_ptr);
        }

        [[nodiscard]] size_t size() const noexcept
        {
            auto* h = get_header();
            return h ? h->size : 0;
        }
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        void clear() noexcept {
            if (m_table) {
                Header* h = get_header();
                uint32_t capacity = h->capacity;
                for (uint32_t i = 0; i < capacity; ++i) {
                    m_table[i].~Entry();
                    new (&m_table[i]) Entry();
                }
                h->size = 0;
            }
        }

        V& operator[](const K& key)
        {
            Header* h = get_header();
            uint32_t capacity = h ? h->capacity : 0;
            uint32_t size = h ? h->size : 0;

            if (capacity == 0) {
                rehash(16);
                h = get_header();
                capacity = h->capacity;
            } else if (size * 10 > capacity * 7) {
                rehash(capacity * 2);
                h = get_header();
                capacity = h->capacity;
            }

            size_t mask = capacity - 1;
            size_t slot = hash_key(key) & mask;
            int first_deleted = -1;
            while (m_table[slot].state != State::EMPTY) {
                if (m_table[slot].state == State::FILLED) {
                    if (m_table[slot].first == key) return m_table[slot].second;
                } else if (m_table[slot].state == State::DELETED) {
                    if (first_deleted == -1) first_deleted = static_cast<int>(slot);
                }
                slot = (slot + 1) & mask;
            }
            size_t insert_pos = (first_deleted != -1) ? static_cast<size_t>(first_deleted) : slot;
            m_table[insert_pos].first = key;
            new (&m_table[insert_pos].second) V();
            m_table[insert_pos].state = State::FILLED;
            h->size++;
            return m_table[insert_pos].second;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 2. C++23 针对指针类型键（K = Pointer）的特化实现（真·哨兵压缩极速版）
    // ─────────────────────────────────────────────────────────────────────────
    template <typename K, typename V>
    requires std::is_pointer_v<K>
    class FlatMap<K, V>
    {
    public:
        struct Entry {
            K first = nullptr;
            V second{};
        };

    private:
        struct Header {
            uint32_t capacity;
            uint32_t size;
        };

        static inline const K Tombstone = reinterpret_cast<K>(static_cast<uintptr_t>(1));

        static constexpr size_t Alignment = alignof(Entry) > alignof(std::max_align_t)
                                            ? alignof(Entry)
                                            : alignof(std::max_align_t);
        static constexpr size_t HeaderOffset = (sizeof(Header) + Alignment - 1) & ~(Alignment - 1);

        Entry* m_table = nullptr;

        static size_t hash_key(K key) noexcept {
            uint64_t x = reinterpret_cast<uint64_t>(key);
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            x = x ^ (x >> 31);
            return static_cast<size_t>(x);
        }

        [[nodiscard]] inline Header* get_header() const noexcept
        {
            if (!m_table) return nullptr;
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_table) - HeaderOffset);
        }

        static Entry* allocate_table(uint32_t capacity)
        {
            size_t total_size = HeaderOffset + static_cast<size_t>(capacity) * sizeof(Entry);
            void* raw = detail::aligned_alloc_helper(total_size, Alignment);

            Header* h = reinterpret_cast<Header*>(raw);
            h->capacity = capacity;
            h->size = 0;

            Entry* table = reinterpret_cast<Entry*>(reinterpret_cast<char*>(raw) + HeaderOffset);
            for (uint32_t i = 0; i < capacity; ++i) {
                new (&table[i]) Entry();
            }
            return table;
        }

        static void deallocate_table(Entry* table) noexcept
        {
            if (!table) return;
            Header* h = reinterpret_cast<Header*>(reinterpret_cast<char*>(table) - HeaderOffset);
            uint32_t capacity = h->capacity;
            for (uint32_t i = 0; i < capacity; ++i) {
                table[i].~Entry();
            }
            void* raw = reinterpret_cast<char*>(table) - HeaderOffset;
            detail::aligned_free_helper(raw, Alignment);
        }

        void rehash(size_t new_capacity)
        {
            Header* old_h = get_header();
            uint32_t old_cap = old_h ? old_h->capacity : 0;
            Entry* old_table = m_table;

            m_table = allocate_table(static_cast<uint32_t>(new_capacity));

            if (old_table) {
                for (uint32_t i = 0; i < old_cap; ++i) {
                    K k = old_table[i].first;
                    if (k != nullptr && k != Tombstone) {
                        insert(k, std::move(old_table[i].second));
                    }
                }
                deallocate_table(old_table);
            }
        }

    public:
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entry;
            using difference_type = std::ptrdiff_t;
            using pointer = Entry*;
            using reference = Entry&;

            pointer ptr;
            pointer end_ptr;

            Iterator(pointer p, pointer end) noexcept : ptr(p), end_ptr(end) {
                if (ptr != end_ptr && (ptr->first == nullptr || ptr->first == Tombstone)) { ++(*this); }
            }
            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }
            Iterator& operator++() noexcept {
                do { ptr++; } while (ptr != end_ptr && (ptr->first == nullptr || ptr->first == Tombstone));
                return *this;
            }
            Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }
            bool operator==(const Iterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const Iterator& other) const noexcept { return ptr != other.ptr; }
        };

        struct ConstIterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = const Entry;
            using difference_type = std::ptrdiff_t;
            using pointer = const Entry*;
            using reference = const Entry&; // 🚀 修复：修正引用别名为正确的 const Entry&

            pointer ptr;
            pointer end_ptr;

            ConstIterator(pointer p, pointer end) noexcept : ptr(p), end_ptr(end) {
                if (ptr != end_ptr && (ptr->first == nullptr || ptr->first == Tombstone)) { ++(*this); }
            }
            reference operator*() const noexcept { return *ptr; }
            pointer operator->() const noexcept { return ptr; }
            ConstIterator& operator++() noexcept {
                do { ptr++; } while (ptr != end_ptr && (ptr->first == nullptr || ptr->first == Tombstone));
                return *this;
            }
            ConstIterator operator++(int) noexcept { ConstIterator tmp = *this; ++(*this); return tmp; }
            bool operator==(const ConstIterator& other) const noexcept { return ptr == other.ptr; }
            bool operator!=(const ConstIterator& other) const noexcept { return ptr != other.ptr; }
        };

        // 🚀 修复：将 std 容器标准的迭代器别名指向正确的自定义包装类，而非裸指针
        using iterator = Iterator;
        using const_iterator = ConstIterator;

        FlatMap(size_t initial_capacity = 16)
        {
            size_t cap = 1;
            while (cap < initial_capacity) cap <<= 1;
            m_table = allocate_table(static_cast<uint32_t>(cap));
        }

        ~FlatMap() noexcept { if (m_table) deallocate_table(m_table); }

        FlatMap(FlatMap&& other) noexcept : m_table(other.m_table) { other.m_table = nullptr; }

        FlatMap& operator=(FlatMap&& other) noexcept
        {
            if (this != &other) {
                if (m_table) deallocate_table(m_table);
                m_table = other.m_table;
                other.m_table = nullptr;
            }
            return *this;
        }

        FlatMap(const FlatMap&) = delete;
        FlatMap& operator=(const FlatMap&) = delete;

        iterator begin() noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return iterator(m_table, m_table + cap);
        }
        iterator end() noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return iterator(m_table + cap, m_table + cap);
        }

        const_iterator begin() const noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return const_iterator(m_table, m_table + cap);
        }
        const_iterator end() const noexcept {
            auto* h = get_header();
            uint32_t cap = h ? h->capacity : 0;
            return const_iterator(m_table + cap, m_table + cap);
        }

        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        void insert(K key, V value)
        {
            Header* h = get_header();
            uint32_t capacity = h ? h->capacity : 0;
            uint32_t size = h ? h->size : 0;

            if (capacity == 0) {
                rehash(16);
                h = get_header();
                capacity = h->capacity;
            } else if (size * 10 > capacity * 7) {
                rehash(capacity * 2);
                h = get_header();
                capacity = h->capacity;
            }

            size_t mask = capacity - 1;
            size_t slot = hash_key(key) & mask;
            while (m_table[slot].first != nullptr && m_table[slot].first != Tombstone) {
                if (m_table[slot].first == key) {
                    m_table[slot].second = std::move(value);
                    return;
                }
                slot = (slot + 1) & mask;
            }
            m_table[slot].first = key;
            m_table[slot].second = std::move(value);
            h->size++;
        }

        iterator find(K key) noexcept
        {
            if (!m_table) return end();
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].first != nullptr) {
                if (m_table[slot].first == key) {
                    return iterator(&m_table[slot], m_table + h->capacity);
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return end();
        }

        const_iterator find(K key) const noexcept
        {
            if (!m_table) return end();
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].first != nullptr) {
                if (m_table[slot].first == key) {
                    return const_iterator(&m_table[slot], m_table + h->capacity);
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return end();
        }

        bool contains(K key) const noexcept
        {
            if (!m_table) return false;
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].first != nullptr) {
                if (m_table[slot].first == key) {
                    return true;
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return false;
        }

        bool erase(K key) noexcept
        {
            if (!m_table) return false;
            Header* h = get_header();
            size_t mask = h->capacity - 1;
            size_t slot = hash_key(key) & mask;
            size_t start = slot;
            while (m_table[slot].first != nullptr) {
                if (m_table[slot].first == key) {
                    m_table[slot].first = Tombstone;
                    m_table[slot].second.~V();
                    new (&m_table[slot].second) V();
                    h->size--;
                    return true;
                }
                slot = (slot + 1) & mask;
                if (slot == start) [[unlikely]] break;
            }
            return false;
        }

        // 🚀 新增：支持通过迭代器快速擦除，并返回下一个有效槽迭代器
        iterator erase(const_iterator pos) noexcept
        {
            if (pos.ptr == nullptr || !m_table) return end();
            Header* h = get_header();
            Entry* end_ptr = m_table + h->capacity;
            if (pos.ptr >= end_ptr || pos.ptr < m_table) return end();

            Entry* p = const_cast<Entry*>(pos.ptr);
            if (p->first != nullptr && p->first != Tombstone) {
                p->first = Tombstone;
                p->second.~V();
                new (&p->second) V();
                h->size--;
            }

            // 顺延寻找下一个非空槽并返回
            do {
                p++;
            } while (p != end_ptr && (p->first == nullptr || p->first == Tombstone));
            return iterator(p, end_ptr);
        }

        [[nodiscard]] size_t size() const noexcept
        {
            auto* h = get_header();
            return h ? h->size : 0;
        }
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        void clear() noexcept {
            if (m_table) {
                Header* h = get_header();
                uint32_t capacity = h->capacity;
                for (uint32_t i = 0; i < capacity; ++i) {
                    m_table[i].~Entry();
                    new (&m_table[i]) Entry();
                }
                h->size = 0;
            }
        }

        V& operator[](K key)
        {
            Header* h = get_header();
            uint32_t capacity = h ? h->capacity : 0;
            uint32_t size = h ? h->size : 0;

            if (capacity == 0) {
                rehash(16);
                h = get_header();
                capacity = h->capacity;
            } else if (size * 10 > capacity * 7) {
                rehash(capacity * 2);
                h = get_header();
                capacity = h->capacity;
            }

            size_t mask = capacity - 1;
            size_t slot = hash_key(key) & mask;
            int first_deleted = -1;
            while (m_table[slot].first != nullptr) {
                if (m_table[slot].first == Tombstone) {
                    if (first_deleted == -1) first_deleted = static_cast<int>(slot);
                } else if (m_table[slot].first == key) {
                    return m_table[slot].second;
                }
                slot = (slot + 1) & mask;
            }
            size_t insert_pos = (first_deleted != -1) ? static_cast<size_t>(first_deleted) : slot;
            m_table[insert_pos].first = key;
            new (&m_table[insert_pos].second) V();
            h->size++;
            return m_table[insert_pos].second;
        }
    };
} // namespace StuCanvas::utils