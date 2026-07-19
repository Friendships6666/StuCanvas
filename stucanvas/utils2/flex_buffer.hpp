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

namespace StuCanvas
{
    namespace utils
    {
        namespace detail
        {
            class ThreadLocalAllocator
            {
            private:
                static constexpr size_t NUM_BUCKETS = 9;
                static constexpr size_t BUCKET_SIZES[NUM_BUCKETS] = { 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

                struct FreeNode { FreeNode* next; };
                FreeNode* m_buckets[NUM_BUCKETS] = { nullptr };

                static int get_bucket_index(size_t size) noexcept {
                    for (int i = 0; i < NUM_BUCKETS; ++i) {
                        if (size <= BUCKET_SIZES[i]) return i;
                    }
                    return -1;
                }

            public:
                ThreadLocalAllocator() noexcept = default;
                ~ThreadLocalAllocator() noexcept {
                    for (int i = 0; i < NUM_BUCKETS; ++i) {
                        FreeNode* curr = m_buckets[i];
                        while (curr != nullptr) {
                            FreeNode* next = curr->next;
                            std::free(curr);
                            curr = next;
                        }
                        m_buckets[i] = nullptr;
                    }
                }

                void* allocate(size_t size) noexcept {
                    int idx = get_bucket_index(size);
                    if (idx == -1) return std::malloc(size);
                    FreeNode* node = m_buckets[idx];
                    if (node != nullptr) {
                        m_buckets[idx] = node->next;
                        return static_cast<void*>(node);
                    }
                    return std::malloc(BUCKET_SIZES[idx]);
                }

                void deallocate(void* ptr, size_t size) noexcept {
                    if (!ptr) return;
                    int idx = get_bucket_index(size);
                    if (idx == -1) {
                        std::free(ptr);
                        return;
                    }
                    auto* node = static_cast<FreeNode*>(ptr);
                    node->next = m_buckets[idx];
                    m_buckets[idx] = node;
                }

                void* reallocate(void* ptr, size_t old_size, size_t new_size) noexcept {
                    if (!ptr) return allocate(new_size);
                    int old_idx = get_bucket_index(old_size);
                    int new_idx = get_bucket_index(new_size);
                    if (old_idx != -1 && old_idx == new_idx) return ptr;
                    void* new_ptr = allocate(new_size);
                    if (!new_ptr) return nullptr;
                    size_t copy_size = old_size < new_size ? old_size : new_size;
                    std::memcpy(new_ptr, ptr, copy_size);
                    deallocate(ptr, old_size);
                    return new_ptr;
                }
            };

            inline ThreadLocalAllocator& get_fast_allocator() noexcept {
                thread_local ThreadLocalAllocator allocator;
                return allocator;
            }
        } // namespace detail

        // =====================================================================
        // FlexBuffer 弹性双端容器 (修复物理冲突检测与扩容偏置平移 BUG)
        // =====================================================================
        class FlexBuffer
        {
        private:
            // 元数据统一升级为 uint32_t，完美对齐 16 字节边界
            struct Header {
                uint32_t capacity;  // 4 字节: 数据区分配的总容量 (不含此 Header 自身)
                uint32_t size;      // 4 字节: 顺序向右写入的 Payload 字节大小 (最大支持 4GB 单元素)
                uint32_t max_id;    // 4 字节: 历史写入的最大 ID 编号 (决定逆向偏置表的最大长度)
                uint32_t align_pad; // 4 字节: 物理对齐占位
            }; // sizeof(Header) = 16 字节

            Header* m_ptr = nullptr; // 唯一成员变量：8 字节

        public:
            FlexBuffer() noexcept = default;

            ~FlexBuffer() noexcept {
                if (m_ptr) {
                    size_t alloc_size = sizeof(Header) + m_ptr->capacity;
                    detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                }
            }

            FlexBuffer(const FlexBuffer&) = delete;
            FlexBuffer& operator=(const FlexBuffer&) = delete;
            FlexBuffer(FlexBuffer&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
            FlexBuffer& operator=(FlexBuffer&& other) noexcept {
                if (this != &other) {
                    if (m_ptr) {
                        size_t alloc_size = sizeof(Header) + m_ptr->capacity;
                        detail::get_fast_allocator().deallocate(m_ptr, alloc_size);
                    }
                    m_ptr = other.m_ptr;
                    other.m_ptr = nullptr;
                }
                return *this;
            }

            // 🚀 核心修复 2：扩容时必须将原本贴在旧物理末端的偏置表，整体物理平移到新的物理最末端！
            void reserve(size_t capacity_bytes) {
                uint32_t current_cap = m_ptr ? m_ptr->capacity : 0;
                if (capacity_bytes <= current_cap) return;
                uint32_t current_size = m_ptr ? m_ptr->size : 0;
                uint32_t current_max_id = m_ptr ? m_ptr->max_id : 0;

                size_t old_alloc_size = current_cap == 0 ? 0 : sizeof(Header) + current_cap;
                size_t new_alloc_size = sizeof(Header) + capacity_bytes;

                void* raw = detail::get_fast_allocator().reallocate(m_ptr, old_alloc_size, new_alloc_size);
                if (!raw) [[unlikely]] return;

                m_ptr = static_cast<Header*>(raw);
                m_ptr->capacity = static_cast<uint32_t>(capacity_bytes);

                if (current_size == 0) {
                    m_ptr->size = 0;
                    m_ptr->max_id = 0;
                } else {
                    // 🚀 核心物理平移
                    uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(m_ptr);
                    size_t table_bytes = current_max_id * sizeof(uint32_t);
                    std::memmove(byte_ptr + sizeof(Header) + capacity_bytes - table_bytes,
                                 byte_ptr + sizeof(Header) + current_cap - table_bytes,
                                 table_bytes);
                }
            }

            // 🚀 核心修复 1：碰撞检测必须加上逆向偏置表的最大物理空间！
            template <typename T>
            T* push_back(uint32_t id, const T& val) {
                const size_t payload_size = sizeof(T);
                const size_t alignment = alignof(T);

                const uint32_t current_size = m_ptr ? m_ptr->size : 0;
                const uint32_t current_cap  = m_ptr ? m_ptr->capacity : 0;
                const uint32_t current_max_id = m_ptr ? m_ptr->max_id : 0;

                // 物理边界对齐计算
                const size_t record_start_offset = sizeof(Header) + current_size;
                const size_t payload_offset = (record_start_offset + alignment - 1) & ~(alignment - 1);
                const size_t aligned_size = payload_offset + payload_size - (sizeof(Header) + current_size);
                const uint32_t new_payload_end = current_size + static_cast<uint32_t>(aligned_size);

                const uint32_t target_max_id = (id + 1 > current_max_id) ? (id + 1) : current_max_id;

                // 🚀 物理碰撞检测修正：Payloads 结尾地址 + 逆向偏置表实际物理大小 <= 物理容量
                const uint32_t required_space = new_payload_end + target_max_id * static_cast<uint32_t>(sizeof(uint32_t));

                if (required_space > current_cap) {
                    uint32_t new_cap = current_cap == 0 ? 128 : current_cap * 2;
                    while (new_cap < required_space) { new_cap *= 2; }
                    reserve(new_cap);
                }

                uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(m_ptr);

                // 如果新写入的 ID 大于历史最大 ID，需要将新腾出来的偏置表内存槽位极速清零（0代表未挂载）
                if (id + 1 > current_max_id) {
                    auto* table_ptr = reinterpret_cast<uint32_t*>(byte_ptr + sizeof(Header) + m_ptr->capacity);
                    for (uint32_t i = current_max_id; i < id + 1; ++i) {
                        table_ptr[-(int)(i + 1)] = 0;
                    }
                    m_ptr->max_id = id + 1;
                }

                // 1. 拷贝物理数据
                auto* dst_payload = reinterpret_cast<T*>(&byte_ptr[payload_offset]);
                std::memcpy(dst_payload, &val, payload_size);

                // 2. 逆向偏置表写入该 ID 对应的物理偏置
                auto* table_ptr = reinterpret_cast<uint32_t*>(byte_ptr + sizeof(Header) + m_ptr->capacity);
                table_ptr[-(int)(id + 1)] = static_cast<uint32_t>(payload_offset);

                m_ptr->size = new_payload_end;

                return dst_payload;
            }

            // 安全版读取
            template <typename T>
            [[nodiscard]] inline T* get(uint32_t id) const noexcept {
                if (!m_ptr || id >= m_ptr->max_id) [[unlikely]] return nullptr;
                const auto* table_ptr = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(m_ptr) + sizeof(Header) + m_ptr->capacity);
                uint32_t offset = table_ptr[-(int)(id + 1)];
                if (!offset) [[unlikely]] return nullptr;
                return reinterpret_cast<T*>(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(m_ptr) + offset));
            }

            // =================================================================
            // 无安全检查的裸读取接口（Unsafe Get）
            // =================================================================
            template <typename T>
            [[nodiscard]] inline T* get_unsafe(uint32_t id) const noexcept {
                const auto* table_ptr = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(m_ptr) + sizeof(Header) + m_ptr->capacity);
                uint32_t offset = table_ptr[-(int)(id + 1)];
                return reinterpret_cast<T*>(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(m_ptr) + offset));
            }

            void clear() noexcept {
                if (m_ptr) {
                    m_ptr->size = 0;
                    m_ptr->max_id = 0;
                }
            }

            [[nodiscard]] size_t size() const noexcept { return m_ptr ? m_ptr->max_id : 0; }
            [[nodiscard]] bool empty() const noexcept { return size() == 0; }
            [[nodiscard]] size_t size_bytes() const noexcept { return m_ptr ? m_ptr->size : 0; }

            [[nodiscard]] FlexBuffer clone() const
            {
                FlexBuffer new_pack;
                if (m_ptr && m_ptr->max_id > 0) {
                    size_t alloc_size = sizeof(Header) + m_ptr->capacity;
                    new_pack.m_ptr = static_cast<Header*>(detail::get_fast_allocator().allocate(alloc_size));
                    if (new_pack.m_ptr) {
                        std::memcpy(new_pack.m_ptr, m_ptr, sizeof(Header) + m_ptr->size);
                        new_pack.m_ptr->capacity = m_ptr->capacity;
                        new_pack.m_ptr->max_id = m_ptr->max_id;
                        // 级联复制逆向偏置表
                        size_t table_bytes = m_ptr->max_id * sizeof(uint32_t);
                        std::memcpy(reinterpret_cast<uint8_t*>(new_pack.m_ptr) + sizeof(Header) + m_ptr->capacity - table_bytes,
                                    reinterpret_cast<uint8_t*>(m_ptr) + sizeof(Header) + m_ptr->capacity - table_bytes,
                                    table_bytes);
                    }
                }
                return new_pack;
            }
        };

        static_assert(sizeof(FlexBuffer) == 8, "FlexBuffer size must be exactly 8 bytes!");
    } // namespace utils
} // namespace StuCanvas