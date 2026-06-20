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
#include <new>
#include <utility>
#include <type_traits>
#include "flex_buffer.hpp" // 复用分配器

namespace StuCanvas
{
    namespace utils
    {
        // =====================================================================
        // FlexList 弹性离散列表 (支持原生 emplace_back/push_back 堆原位构造与 O(1) erase)
        // =====================================================================
        class FlexList
        {
        public:
            static constexpr size_t MAX_IDS = 16; // 支持 0 ~ 15 的紧凑编号系统

        private:
            using DeleterFunc = void(*)(void*) noexcept;

            // 每个槽位占用 16 字节
            struct IDPointer {
                void* pointer;
                DeleterFunc deleter;
            };

            // 8 字节头部
            struct Header {
                uint32_t capacity;  // 4 字节
                uint32_t count;     // 4 字节
            };

            Header* m_ptr = nullptr; // 唯一成员变量：8 字节

        public:
            FlexList() noexcept = default;

            // 析构时：自动回收所有挂载在外部的离散堆资产
            ~FlexList() noexcept {
                clear_and_free();
            }

            FlexList(const FlexList&) = delete;
            FlexList& operator=(const FlexList&) = delete;
            FlexList(FlexList&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

            FlexList& operator=(FlexList&& other) noexcept {
                if (this != &other) {
                    clear_and_free();
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

                size_t old_alloc_size = current_cap == 0 ? 0 : sizeof(Header) + current_cap * sizeof(IDPointer);
                size_t new_alloc_size = sizeof(Header) + capacity_count * sizeof(IDPointer);

                void* raw = detail::get_fast_allocator().reallocate(m_ptr, old_alloc_size, new_alloc_size);
                if (!raw) [[unlikely]] return;

                m_ptr = static_cast<Header*>(raw);
                m_ptr->capacity = static_cast<uint32_t>(capacity_count);

                if (current_count == 0) {
                    m_ptr->count = 0;
                }

                // 将新分配的指针槽位初始化为 nullptr 保证物理安全
                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                std::memset(pairs + current_cap, 0, (capacity_count - current_cap) * sizeof(IDPointer));
            }

            // =================================================================
            // 🚀 核心新增：emplace_back 自动堆原位构造 (完美转发)
            // 用户不再需要写 new/malloc，容器在底层自动申请空间并原位初始化对象
            // =================================================================
            template <typename T, typename... Args>
            T* emplace_back(uint32_t id, Args&&... args)
            {
                // 1. 申请该资产类型的物理裸内存
                void* raw_asset = std::malloc(sizeof(T));
                if (!raw_asset) [[unlikely]] return nullptr;

                // 2. 利用 placement new 在堆块上原位执行构造函数
                T* asset_ptr = ::new (raw_asset) T(std::forward<Args>(args)...);

                // 3. 检查偏置表扩容
                const uint32_t current_count = m_ptr ? m_ptr->count : 0;
                const uint32_t current_cap  = m_ptr ? m_ptr->capacity : 0;

                if (id >= current_cap) {
                    uint32_t new_cap = current_cap == 0 ? 8 : current_cap * 2;
                    while (new_cap <= id) { new_cap *= 2; }
                    reserve(new_cap);
                }

                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);

                // 4. 自动捕获特定类型的析构与物理释放链条
                auto deleter_func = [](void* ptr) noexcept {
                    static_cast<T*>(ptr)->~T(); // 显式释放内部成员（如 std::vector)
                    std::free(ptr);             // 释放容器本身物理块
                };

                pairs[id] = IDPointer{ static_cast<void*>(asset_ptr), deleter_func };
                m_ptr->count = current_count + 1;

                return asset_ptr;
            }

            // 🚀 核心新增：标准 push_back 封装 (支持左值与右值)
            template <typename T>
            T* push_back(uint32_t id, const T& val) {
                return emplace_back<T>(id, val);
            }

            template <typename T>
            T* push_back(uint32_t id, T&& val) {
                return emplace_back<T>(id, std::move(val));
            }

            // =================================================================
            // 🚀 核心新增：$O(1)$ 资产主动删除接口 (物理内存零碎片)
            // =================================================================
            void erase(uint32_t id) noexcept
            {
                if (!m_ptr || id >= m_ptr->capacity) [[unlikely]] return;

                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                if (pairs[id].pointer) {
                    // 1. 直接触发代理销毁器（调用析构 + free）
                    pairs[id].deleter(pairs[id].pointer);

                    // 2. 槽位彻底置空，物理计数递减
                    pairs[id].pointer = nullptr;
                    pairs[id].deleter = nullptr;
                    m_ptr->count--;
                }
            }

            // 极速无安全检查读取
            template <typename T>
            [[nodiscard]] inline T* get_unsafe(uint32_t id) const noexcept {
                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                return static_cast<T*>(pairs[id].pointer);
            }

            // 安全版读取
            template <typename T>
            [[nodiscard]] inline T* get(uint32_t id) const noexcept {
                if (!m_ptr || id >= m_ptr->capacity) [[unlikely]] return nullptr;
                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                return static_cast<T*>(pairs[id].pointer);
            }

            void clear() noexcept {
                if (m_ptr) {
                    auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                    const uint32_t cap = m_ptr->capacity;
                    for (uint32_t i = 0; i < cap; ++i) {
                        if (pairs[i].pointer) {
                            pairs[i].deleter(pairs[i].pointer);
                            pairs[i].pointer = nullptr;
                            pairs[i].deleter = nullptr;
                        }
                    }
                    m_ptr->count = 0;
                }
            }

            [[nodiscard]] size_t size() const noexcept { return m_ptr ? m_ptr->count : 0; }
            [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        private:
            void clear_and_free() noexcept {
                if (m_ptr) {
                    auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                    const uint32_t cap = m_ptr->capacity;
                    for (uint32_t i = 0; i < cap; ++i) {
                        if (pairs[i].pointer) {
                            pairs[i].deleter(pairs[i].pointer);
                        }
                    }
                    size_t alloc_size = sizeof(Header) + m_ptr->capacity * sizeof(IDPointer);
                    detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                }
            }
        };

        static_assert(sizeof(FlexList) == 8, "FlexList size must be exactly 8 bytes!");
    } // namespace utils
} // namespace StuCanvas