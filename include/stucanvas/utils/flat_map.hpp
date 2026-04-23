#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <iterator>


namespace StuCanvas::utils {

template <typename K, typename V>
class FlatMap {
public:
    enum class State : uint8_t { EMPTY, FILLED, DELETED };

    // 将成员命名为 first/second 以支持类似 std::pair 的访问
    struct Entry {
        K first;   // Key
        V second;  // Value
        State state = State::EMPTY;
    };

private:
    std::vector<Entry> table_;
    size_t size_ = 0;
    size_t mask_ = 0;

    static size_t hash_key(K key) {
        uint64_t x = static_cast<uint64_t>(key);
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = x ^ (x >> 31);
        return static_cast<size_t>(x);
    }

    void rehash(size_t new_capacity) {
        auto old_table = std::move(table_);
        table_.assign(new_capacity, Entry{});
        mask_ = new_capacity - 1;
        size_ = 0;
        for (auto& entry : old_table) {
            if (entry.state == State::FILLED) {
                insert(entry.first, entry.second);
            }
        }
    }

public:
    // --- 迭代器实现 ---
    struct Iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = Entry*;
        using reference = Entry&;

        pointer ptr;
        pointer end_ptr;

        Iterator(pointer p, pointer end) : ptr(p), end_ptr(end) {
            // 初始化时如果当前位置不是 FILLED，自动找下一个
            if (ptr != end_ptr && ptr->state != State::FILLED) {
                ++(*this);
            }
        }

        reference operator*() const { return *ptr; }
        pointer operator->() const { return ptr; }

        Iterator& operator++() {
            do {
                ptr++;
            } while (ptr != end_ptr && ptr->state != State::FILLED);
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const { return ptr == other.ptr; }
        bool operator!=(const Iterator& other) const { return ptr != other.ptr; }
    };

    // --- 容器接口 ---
    FlatMap(size_t initial_capacity = 16) {
        size_t cap = 1;
        while (cap < initial_capacity) cap <<= 1;
        table_.resize(cap);
        mask_ = cap - 1;
    }

    Iterator begin() { return Iterator(table_.data(), table_.data() + table_.size()); }
    Iterator end() { return Iterator(table_.data() + table_.size(), table_.data() + table_.size()); }

    void insert(K key, V value) {
        if (size_ * 10 > table_.size() * 7) rehash(table_.size() * 2);

        size_t h = hash_key(key) & mask_;
        while (table_[h].state == State::FILLED) {
            if (table_[h].first == key) {
                table_[h].second = value;
                return;
            }
            h = (h + 1) & mask_;
        }
        table_[h].first = key;
        table_[h].second = value;
        table_[h].state = State::FILLED;
        size_++;
    }

    // 修改 find 返回迭代器，与标准库对齐
    Iterator find(K key) {
        size_t h = hash_key(key) & mask_;
        size_t start = h;
        while (table_[h].state != State::EMPTY) {
            if (table_[h].state == State::FILLED && table_[h].first == key) {
                return Iterator(&table_[h], table_.data() + table_.size());
            }
            h = (h + 1) & mask_;
            if (h == start) break;
        }
        return end();
    }

    bool erase(K key) {
        size_t h = hash_key(key) & mask_;
        while (table_[h].state != State::EMPTY) {
            if (table_[h].state == State::FILLED && table_[h].first == key) {
                table_[h].state = State::DELETED;
                size_--;
                return true;
            }
            h = (h + 1) & mask_;
        }
        return false;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { table_.assign(table_.size(), Entry{}); size_ = 0; }


    /**
     * @brief 下标操作符
     * 如果 key 不存在，则插入默认构造的 V 并返回引用。
     * 这使得 FlatMap 可以直接用于 --in_degrees[id] 操作。
     */
    V& operator[](const K& key) {
        // 1. 检查是否需要扩容
        if (size_ * 10 > table_.size() * 7) {
            rehash(table_.size() * 2);
        }

        size_t h = hash_key(key) & mask_;

        // 记录第一个遇到的 DELETED 槽位，用于后续优化插入
        int first_deleted = -1;

        while (table_[h].state != State::EMPTY) {
            if (table_[h].state == State::FILLED) {
                if (table_[h].first == key) {
                    return table_[h].second; // 找到已有的 key
                }
            } else if (table_[h].state == State::DELETED) {
                if (first_deleted == -1) first_deleted = static_cast<int>(h);
            }
            h = (h + 1) & mask_;
        }

        // 2. 如果没找到，执行插入逻辑
        size_t insert_pos = (first_deleted != -1) ? static_cast<size_t>(first_deleted) : h;

        table_[insert_pos].first = key;
        table_[insert_pos].second = V{}; // 默认构造值 (对于 size_t 就是 0)
        table_[insert_pos].state = State::FILLED;
        size_++;

        return table_[insert_pos].second;
    }
};

} // namespace detail
// namespace StuGeometry