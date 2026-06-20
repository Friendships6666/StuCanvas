#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace StuCanvas
{
    enum class AssetType : uint32_t {
        APPEARANCE = 0,
        PHYSICS    = 1,
        METADATA   = 2,
        USER_DATA  = 3,
        COUNT      = 4
    };

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

        class FlexBuffer
        {
        private:
            static constexpr size_t MAX_TYPES = 8;

            // 🚀 优化：元数据与偏置表统一前置（共 24 字节，完美对齐）
            struct Header {
                uint16_t capacity;            // 2 字节
                uint16_t size;                // 2 字节
                uint16_t count;               // 2 字节
                uint16_t align_pad;           // 2 字节 (对齐占位)
                uint16_t offsets[MAX_TYPES];  // 16 字节 (前置偏置表)
            }; // sizeof(Header) = 24 字节

            Header* m_ptr = nullptr;

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

            // 🚀 极其简化：前置后，扩容拷贝时偏移量会自动随 Header 一起搬运，无需任何手动平移！
            void reserve(size_t capacity_bytes) {
                uint32_t current_cap = m_ptr ? m_ptr->capacity : 0;
                if (capacity_bytes <= current_cap) return;
                uint32_t current_size = m_ptr ? m_ptr->size : 0;

                size_t old_alloc_size = current_cap == 0 ? 0 : sizeof(Header) + current_cap;
                size_t new_alloc_size = sizeof(Header) + capacity_bytes;

                void* raw = detail::get_fast_allocator().reallocate(m_ptr, old_alloc_size, new_alloc_size);
                if (!raw) [[unlikely]] return;

                m_ptr = static_cast<Header*>(raw);
                m_ptr->capacity = static_cast<uint32_t>(capacity_bytes);

                if (current_size == 0) {
                    m_ptr->size = 0;
                    m_ptr->count = 0;
                }
            }

            template <typename T>
            T* push_back(const T& val) {
                const size_t payload_size = sizeof(T);
                const size_t alignment = alignof(T);
                const uint32_t current_size = m_ptr ? m_ptr->size : 0;
                const uint32_t current_cap  = m_ptr ? m_ptr->capacity : 0;
                const uint32_t current_count = m_ptr ? m_ptr->count : 0;

                const size_t record_start_offset = sizeof(Header) + current_size;
                const size_t payload_offset = (record_start_offset + alignment - 1) & ~(alignment - 1);
                const size_t aligned_size = payload_offset + payload_size - (sizeof(Header) + current_size);
                const uint32_t new_payload_end = current_size + static_cast<uint32_t>(aligned_size);
                const uint32_t required_space = new_payload_end;

                if (required_space > current_cap) {
                    uint32_t new_cap = current_cap == 0 ? 64 : current_cap * 2;
                    while (new_cap < required_space) { new_cap *= 2; }
                    reserve(new_cap);
                }

                uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(m_ptr);

                // 物理复制
                auto* dst_payload = reinterpret_cast<T*>(&byte_ptr[payload_offset]);
                std::memcpy(dst_payload, &val, payload_size);

                // 写入前置偏置表（极其直观，0 偏移计算）
                m_ptr->offsets[current_count] = static_cast<uint16_t>(payload_offset);

                m_ptr->size = new_payload_end;
                m_ptr->count = current_count + 1;

                return dst_payload;
            }

            // 安全版读取 (保留)
            template <typename T>
            [[nodiscard]] inline T* get(size_t index) const noexcept {
                if (!m_ptr || index >= m_ptr->count) [[unlikely]] return nullptr;
                const uint16_t offset = m_ptr->offsets[index];
                return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(m_ptr) + offset);
            }

            // =================================================================
            // 🚀 终极武器：无安全检查的裸读取接口（Unsafe Get）
            // 编译后在 x86 上仅有两行汇编，彻底消灭分支预测惩罚，支持编译器完美向量化！
            // =================================================================
            template <typename T>
            [[nodiscard]] inline T* get_unsafe(size_t index) const noexcept {
                const uint16_t offset = m_ptr->offsets[index];
                return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(m_ptr) + offset);
            }

            [[nodiscard]] size_t size() const noexcept { return m_ptr ? m_ptr->count : 0; }
        };

        static_assert(sizeof(FlexBuffer) == 8, "FlexBuffer size must be exactly 8 bytes!");
    } // namespace utils
} // namespace StuCanvas