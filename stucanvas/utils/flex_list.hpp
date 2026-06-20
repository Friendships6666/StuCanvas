/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "flex_buffer.hpp" // 复用分配器

namespace StuCanvas
{
    namespace utils
    {
        // =====================================================================
        // FlexList 指针索引表 (物理 8 字节，数据 0 循环，ID 一步寻址直达)
        // =====================================================================
        class FlexList
        {
        private:
            // 8 字节极简头部
            struct Header {
                uint32_t capacity; // 4 字节: 指针槽位的最大分配容量 (支持 uint32_t 级)
                uint32_t count;    // 4 字节: 当前实际分配的资产个数
            }; // sizeof(Header) = 8 字节

            Header* m_ptr = nullptr; // 唯一成员变量：8 字节

        public:
            FlexList() noexcept = default;

            // 析构时：自动回收本线程分配的所有离散资产
            ~FlexList() noexcept {
                if (m_ptr) {
                    void** pointers = reinterpret_cast<void**>(m_ptr + 1);
                    const uint32_t cap = m_ptr->capacity;
                    for (uint32_t i = 0; i < cap; ++i) {
                        if (pointers[i]) {
                            std::free(pointers[i]); // C 风格物理释放
                        }
                    }
                    size_t alloc_size = sizeof(Header) + m_ptr->capacity * sizeof(void*);
                    detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                }
            }

            FlexList(const FlexList&) = delete;
            FlexList& operator=(const FlexList&) = delete;
            FlexList(FlexList&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
            FlexList& operator=(FlexList&& other) noexcept {
                if (this != &other) {
                    if (m_ptr) {
                        size_t alloc_size = sizeof(Header) + m_ptr->capacity * sizeof(void*);
                        detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                    }
                    m_ptr = other.m_ptr;
                    other.m_ptr = nullptr;
                }
                return *this;
            }

            // 预分配指针槽位
            void reserve(size_t capacity_count) {
                uint32_t current_cap = m_ptr ? m_ptr->capacity : 0;
                if (capacity_count <= current_cap) return;
                uint32_t current_count = m_ptr ? m_ptr->count : 0;

                size_t old_alloc_size = current_cap == 0 ? 0 : sizeof(Header) + current_cap * sizeof(void*);
                size_t new_alloc_size = sizeof(Header) + capacity_count * sizeof(void*);

                void* raw = detail::get_fast_allocator().reallocate(m_ptr, old_alloc_size, new_alloc_size);
                if (!raw) [[unlikely]] return;

                m_ptr = static_cast<Header*>(raw);
                m_ptr->capacity = static_cast<uint32_t>(capacity_count);

                if (current_count == 0) {
                    m_ptr->count = 0;
                }

                // 🚀 核心：初始化新腾出来的槽位为 nullptr，保证物理安全
                void** pointers = reinterpret_cast<void**>(m_ptr + 1);
                std::memset(pointers + current_cap, 0, (capacity_count - current_cap) * sizeof(void*));
            }

            // 🚀 写入资产：根据用户指定的 id，直接物理放入对应槽位，重复 ID 为 UB
            template <typename T>
            T* push_back(uint32_t id, T* asset_ptr) {
                const uint32_t current_count = m_ptr ? m_ptr->count : 0;
                const uint32_t current_cap  = m_ptr ? m_ptr->capacity : 0;

                // 如果用户写入的 ID 超出了当前的槽位容量，触发无锁分配器倍增扩容
                if (id >= current_cap) {
                    uint32_t new_cap = current_cap == 0 ? 16 : current_cap * 2;
                    while (new_cap <= id) { new_cap *= 2; }
                    reserve(new_cap);
                }

                void** pointers = reinterpret_cast<void**>(m_ptr + 1);

                // 🚀 绝对 $O(1)$：没有任何循环与去重开销，直插目标槽位
                pointers[id] = static_cast<void*>(asset_ptr);
                m_ptr->count++;

                return asset_ptr;
            }

            // 安全版读取
            template <typename T>
            [[nodiscard]] inline T* get(uint32_t id) const noexcept {
                if (!m_ptr || id >= m_ptr->capacity) [[unlikely]] return nullptr;
                void** pointers = reinterpret_cast<void**>(m_ptr + 1);
                return static_cast<T*>(pointers[id]);
            }

            // =================================================================
            // 🚀 终极武器：基于指针索引表（Pointer Index Table）的一步直达寻址！
            // 0 循环，0 条件分支判断，彻底抹平并超越 pNext 耗时。
            // =================================================================
            template <typename T>
            [[nodiscard]] inline T* get_unsafe(uint32_t id) const noexcept {
                void** pointers = reinterpret_cast<void**>(m_ptr + 1);
                return static_cast<T*>(pointers[id]);
            }

            void clear() noexcept {
                if (m_ptr) {
                    m_ptr->count = 0;
                    std::memset(m_ptr + 1, 0, m_ptr->capacity * sizeof(void*));
                }
            }

            [[nodiscard]] size_t size() const noexcept { return m_ptr ? m_ptr->count : 0; }
            [[nodiscard]] bool empty() const noexcept { return size() == 0; }
        };

        static_assert(sizeof(FlexList) == 8, "FlexList size must be exactly 8 bytes!");
    } // namespace utils
} // namespace StuCanvas