#pragma once
#include <vector>
#include <memory>
#include <utility>
#include <cstdint>


namespace StuCanvas::utils
{
    /**
     * @brief 高性能分块双端队列 (内存池)
     * @tparam T 存储的元素类型 (Node)
     * @tparam BlockSize 每个内存块能容纳的元素个数 (建议为 2 的幂，如 1024)
     */
    template <typename T, size_t BlockSize = 1024>
    class BlockDeque
    {
            public:
        // --- 核心改动：增加迭代器结构 ---
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            BlockDeque& container;
            size_t index;

            // 解引用：利用 container 现有的 operator[]
            reference operator*() const { return container[index]; }
            pointer operator->() const { return &container[index]; }

            // 前置自增
            Iterator& operator++() {
                ++index;
                return *this;
            }

            // 后置自增
            Iterator operator++(int) {
                Iterator tmp = *this;
                ++index;
                return tmp;
            }

            bool operator==(const Iterator& other) const { return index == other.index; }
            bool operator!=(const Iterator& other) const { return index != other.index; }
        };

        // 常量迭代器 (用于 const 容器遍历)
        struct ConstIterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = const T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            const BlockDeque& container;
            size_t index;

            reference operator*() const { return container[index]; }
            pointer operator->() const { return &container[index]; }

            ConstIterator& operator++() {
                ++index;
                return *this;
            }

            ConstIterator operator++(int) {
                ConstIterator tmp = *this;
                ++index;
                return tmp;
            }

            bool operator==(const ConstIterator& other) const { return index == other.index; }
            bool operator!=(const ConstIterator& other) const { return index != other.index; }
        };

        // --- 增加 begin/end 成员函数 ---
        Iterator begin() { return {*this, 0}; }
        Iterator end() { return {*this, total_size_}; }

        ConstIterator begin() const { return {*this, 0}; }
        ConstIterator end() const { return {*this, total_size_}; }

        ConstIterator cbegin() const { return {*this, 0}; }
        ConstIterator cend() const { return {*this, total_size_}; }
    private:
        // 使用 unique_ptr 包裹内部 vector，保证外部 blocks_ 扩容时，内部数据地址绝对不变！
        std::vector<std::unique_ptr<std::vector<T>>> blocks_;
        size_t total_size_ = 0;

        void add_new_block()
        {
            auto new_block = std::make_unique<std::vector<T>>();
            // 提前给这个块分配好连续内存，由于我们在代码里严格控制了数量，它永远不会触发扩容
            new_block->reserve(BlockSize);
            blocks_.push_back(std::move(new_block));
        }

    public:
        BlockDeque()
        {
            add_new_block();
        }

        // ==========================================
        // 基本容器接口 (完美替换 std::vector)
        // ==========================================

        [[nodiscard]] size_t size() const noexcept
        {
            return total_size_;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return total_size_ == 0;
        }

        // ==========================================
        // 元素访问 (支持 O(1) 下标访问)
        // 提示：如果 BlockSize 是 2 的幂(如 1024)，编译器会自动把除法优化为极速的位移操作 (>>)
        // ==========================================

        T& operator[](size_t index) noexcept
        {
            const size_t block_idx = index / BlockSize;
            const size_t element_idx = index % BlockSize;
            return (*blocks_[block_idx])[element_idx];
        }

        const T& operator[](size_t index) const noexcept
        {
            const size_t block_idx = index / BlockSize;
            const size_t element_idx = index % BlockSize;
            return (*blocks_[block_idx])[element_idx];
        }

        T& back() noexcept
        {
            return (*this)[total_size_ - 1];
        }

        const T& back() const noexcept
        {
            return (*this)[total_size_ - 1];
        }

        // ==========================================
        // 元素修改
        // ==========================================

        // ==========================================
        // 极速无序抹除（不保持顺序，Swap-and-Pop 极致加速，最适合 DAG 连线）
        // ==========================================
        size_t erase_unordered(const T& value) noexcept
        {
            size_t erased_count = 0;
            for (size_t i = 0; i < total_size_; )
            {
                if ((*this)[i] == value)
                {
                    // 如果不是最后一个元素，则将最后一个元素移到当前位置进行覆盖
                    if (i != total_size_ - 1)
                    {
                        (*this)[i] = std::move((*this)[total_size_ - 1]);
                    }
                    pop_back(); // 直接物理弹出末尾
                    erased_count++;
                    // 注意：因为末尾元素换到了当前位置 i，所以不需要自增 i，继续检查当前位置
                }
                else
                {
                    ++i;
                }
            }
            return erased_count;
        }

        // ==========================================
        // 顺序抹除（保持原有顺序，相当于 std::vector::erase）
        // ==========================================
        size_t erase(const T& value) noexcept
        {
            size_t erased_count = 0;
            for (size_t i = 0; i < total_size_; )
            {
                if ((*this)[i] == value)
                {
                    // 将后续元素依次向前挪动一位（跨块拷贝较慢）
                    for (size_t j = i; j < total_size_ - 1; ++j)
                    {
                        (*this)[j] = std::move((*this)[j + 1]);
                    }
                    pop_back();
                    erased_count++;
                }
                else
                {
                    ++i;
                }
            }
            return erased_count;
        }


        template <typename... Args>
        T& emplace_back(Args&&... args)
        {
            // 如果当前所有块都装满了，就申请一个新块
            const size_t current_block_idx = total_size_ / BlockSize;
            if (current_block_idx >= blocks_.size())
            {
                add_new_block();
            }

            // 在当前块的末尾真实构造对象 (此时才会触发 Node 的构造函数，增加 ID)
            blocks_[current_block_idx]->emplace_back(std::forward<Args>(args)...);
            ++total_size_;

            return blocks_[current_block_idx]->back();
        }
        // 拷贝版本：接受一个已存在的 Node 并复制它
        void push_back(const T& value) {
            // 直接复用 emplace_back 的逻辑，触发 T 的拷贝构造函数
            this->emplace_back(value);
        }

        // 移动版本：接受一个临时 Node (或 std::move 过的 Node) 并转移所有权
        void push_back(T&& value) {
            // 直接复用 emplace_back 的逻辑，触发 T 的移动构造函数
            this->emplace_back(std::move(value));
        }

        // 支持你 Graph 里的 Swap-and-Pop 节点删除法！
        void pop_back() noexcept
        {
            if (total_size_ == 0) return;

            const size_t current_block_idx = (total_size_ - 1) / BlockSize;
            blocks_[current_block_idx]->pop_back();
            --total_size_;

            // 内存池优化：如果删除了大量节点导致空出两个完全空白的块，就释放一个
            // 始终保留一个空余块防止频繁 new/delete 造成抖动
            if (blocks_.size() > 1 && blocks_.back()->empty() && blocks_[blocks_.size() - 2]->empty())
            {
                blocks_.pop_back();
            }
        }
    };
} // namespace StuGeometry::detail
