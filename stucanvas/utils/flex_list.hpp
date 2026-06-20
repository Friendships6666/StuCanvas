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
#include <atomic>
#include <type_traits>
#include "flex_buffer.hpp" // 复用分配器

namespace StuCanvas
{
    namespace utils
    {
        // =====================================================================
        // 0. 自动类型 ID 生成器 (线程安全初始化，热路径 0 运行时开销)
        // =====================================================================
        struct TypeIdGenerator {
            inline static std::atomic<uint32_t> s_counter{0};
            static uint32_t next_id() noexcept {
                return s_counter.fetch_add(1, std::memory_order_relaxed);
            }
        };

        template <typename T>
        struct TypeId {
            static uint32_t get() noexcept {
                // 🚀 仅在全系统首次接触 T 类型时初始化一次，后续直接返回常量
                static const uint32_t id = TypeIdGenerator::next_id();
                return id;
            }
        };

        // =====================================================================
        // FlexList 指针索引表 (编译期自动类型推导，RAII 自动级联析构)
        // =====================================================================
        class FlexList
        {
        private:
            using DeleterFunc = void(*)(void*) noexcept;

            struct IDPointer {
                void* pointer;
                DeleterFunc deleter;
            };

            // 8 字节头部
            struct Header {
                uint32_t capacity;  // 4 字节: 当前分配的指针槽位个数 (随 ID 大小自适应动态扩容)
                uint32_t count;     // 4 字节: 实际挂载的离散资产个数
            };

            Header* m_ptr = nullptr; // 唯一成员变量：8 字节

        public:
            FlexList() noexcept = default;

            ~FlexList() noexcept {
                if (m_ptr) {
                    auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                    const uint32_t cap = m_ptr->capacity;
                    for (uint32_t i = 0; i < cap; ++i) {
                        if (pairs[i].pointer) {
                            pairs[i].deleter(pairs[i].pointer); // 级联强类型销毁
                        }
                    }
                    size_t alloc_size = sizeof(Header) + m_ptr->capacity * sizeof(IDPointer);
                    detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                }
            }

            FlexList(const FlexList&) = delete;
            FlexList& operator=(const FlexList&) = delete;
            FlexList(FlexList&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
            FlexList& operator=(FlexList&& other) noexcept {
                if (this != &other) {
                    if (m_ptr) {
                        size_t alloc_size = sizeof(Header) + m_ptr->capacity * sizeof(IDPointer);
                        detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                    }
                    m_ptr = other.m_ptr;
                    other.m_ptr = nullptr;
                }
                return *this;
            }

            // 弹性扩容
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

            // 🚀 极速挂载：自动推导 T 的编译期 ID 并就地写入，0 运行时 ID 分配
            template <typename T>
            T* push_back(T* asset_ptr) {
                const uint32_t id = TypeId<T>::get(); // 🚀 自动获取该类型的唯一编号

                const uint32_t current_count = m_ptr ? m_ptr->count : 0;
                const uint32_t current_cap  = m_ptr ? m_ptr->capacity : 0;

                // 如果新资产的类型 ID 超出了当前容量，触发自适应扩容
                if (id >= current_cap) {
                    uint32_t new_cap = current_cap == 0 ? 8 : current_cap * 2;
                    while (new_cap <= id) { new_cap *= 2; }
                    reserve(new_cap);
                }

                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);

                // 自动捕获特定类型的析构与释放链条
                auto deleter_func = [](void* ptr) noexcept {
                    static_cast<T*>(ptr)->~T();
                    std::free(ptr);
                };

                pairs[id] = IDPointer{ static_cast<void*>(asset_ptr), deleter_func };
                m_ptr->count = current_count + 1;

                return asset_ptr;
            }

            // 安全版读取
            template <typename T>
            [[nodiscard]] inline T* get() const noexcept {
                if (!m_ptr) [[unlikely]] return nullptr;
                const uint32_t id = TypeId<T>::get();
                if (id >= m_ptr->capacity) [[unlikely]] return nullptr;
                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                return static_cast<T*>(pairs[id].pointer);
            }

            // =================================================================
            // 🚀 终极武器：基于编译期自增 ID 偏置表的 0 循环、0 分支直连获取！
            // 没有任何运行参数，直接传入类型作为模板，在汇编级实现 1 步寻址直达。
            // =================================================================
            template <typename T>
            [[nodiscard]] inline T* get_unsafe() const noexcept {
                const uint32_t id = TypeId<T>::get(); // 编译期静态常量
                auto* pairs = reinterpret_cast<IDPointer*>(m_ptr + 1);
                return static_cast<T*>(pairs[id].pointer);
            }

            void clear() noexcept {
                if (m_ptr) {
                    m_ptr->count = 0;
                    std::memset(m_ptr + 1, 0, m_ptr->capacity * sizeof(IDPointer));
                }
            }

            [[nodiscard]] size_t size() const noexcept { return m_ptr ? m_ptr->count : 0; }
            [[nodiscard]] bool empty() const noexcept { return size() == 0; }
        };

        static_assert(sizeof(FlexList) == 8, "FlexList size must be exactly 8 bytes!");
    } // namespace utils
} // namespace StuCanvas