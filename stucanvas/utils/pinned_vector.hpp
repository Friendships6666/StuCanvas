#pragma once

#include <iostream>
#include <stdexcept>
#include <new>
#include <utility>
#include <algorithm>

// 根据不同平台引入对应的系统调用头文件
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace StuCanvas::utils {
    // 系统级虚拟内存操作封装
    struct SysMem {
        static size_t get_page_size() {
#ifdef _WIN32
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            return sysInfo.dwPageSize;
#else
            return sysconf(_SC_PAGESIZE);
#endif
        }

        static void* reserve(size_t size) {
#ifdef _WIN32
            void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
            return ptr;
#else
            void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) return nullptr;
            return ptr;
#endif
        }

        static bool commit(void* addr, size_t size) {
#ifdef _WIN32
            return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
            return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
#endif
        }

        static void decommit(void* addr, size_t size) {
#ifdef _WIN32
            VirtualFree(addr, size, MEM_DECOMMIT);
#else
            madvise(addr, size, MADV_DONTNEED);
            mprotect(addr, size, PROT_NONE);
#endif
        }

        static void release(void* addr, size_t size) {
            if (!addr) return;
#ifdef _WIN32
            VirtualFree(addr, 0, MEM_RELEASE);
#else
            munmap(addr, size);
#endif
        }
    };
} // namespace StuCanvas::utils

namespace details = StuCanvas::utils;

// PinnedVector 模板参数：
// T: 元素类型
// GbCapacity: 预留的虚拟地址空间大小（单位：GB）
template <typename T, size_t GbCapacity>
class PinnedVector {
    static_assert(GbCapacity > 0, "Capacity must be at least 1 GB");
    static_assert(sizeof(void*) == 8, "PinnedVector is only supported on 64-bit architectures");

public:
    using value_type = T;
    using size_type = size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

private:
    // 控制信息结构体，放置在预留内存的头部
    struct Header {
        size_t size;                   // 当前有效元素个数
        size_t committed_bytes;        // 整个虚拟内存区已提交物理内存的字节数（含 Header 占用）
        size_t page_size;              // 系统的页面大小
        size_t max_committed_elements; // 缓存当前物理内存能容纳的最大元素数（用于优化 emplace_back 快速路径）
    };

    static constexpr size_t GB = 1024ULL * 1024ULL * 1024ULL;
    static constexpr size_t TOTAL_BYTES = GbCapacity * GB;

    static constexpr size_t align_up_constexpr(size_t size, size_t alignment) noexcept {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    static constexpr size_t HEADER_OFFSET = align_up_constexpr(sizeof(Header), alignof(T));
    static constexpr size_t RESERVED_BYTES = TOTAL_BYTES + HEADER_OFFSET;
    static constexpr size_t MAX_ELEMENTS = TOTAL_BYTES / sizeof(T);

    // 唯一的 8 字节成员指针，指向数据区域首地址
    T* m_data = nullptr;

    static inline size_t align_up(size_t size, size_t alignment) noexcept {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    // 利用 C++20 属性优化非空指针的静态分支预测
    inline Header* get_header() noexcept {
        if (m_data) [[likely]] {
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(m_data) - HEADER_OFFSET);
        }
        return nullptr;
    }

    inline const Header* get_header() const noexcept {
        if (m_data) [[likely]] {
            return reinterpret_cast<const Header*>(reinterpret_cast<const char*>(m_data) - HEADER_OFFSET);
        }
        return nullptr;
    }

    // 内部方法：提交物理内存至指定字节数（相对于整个保留区的大小）
    void commit_up_to(size_t required_user_bytes) {
        Header* header = get_header();
        if (!header) return;

        size_t required_sys_bytes = HEADER_OFFSET + required_user_bytes;
        if (required_sys_bytes <= header->committed_bytes) return;

        required_sys_bytes = align_up(required_sys_bytes, header->page_size);
        if (required_sys_bytes > RESERVED_BYTES) {
            required_sys_bytes = RESERVED_BYTES;
        }

        size_t diff = required_sys_bytes - header->committed_bytes;
        if (diff == 0) return;

        void* commit_addr = reinterpret_cast<char*>(header) + header->committed_bytes;
        if (!details::SysMem::commit(commit_addr, diff)) {
            throw std::bad_alloc();
        }

        header->committed_bytes = required_sys_bytes;
        // 在慢路径中更新缓存值，避免快速路径重复计算
        header->max_committed_elements = (required_sys_bytes - HEADER_OFFSET) / sizeof(T);
    }

    // 彻底分离扩容的冷路径（Cold Path），禁止内联，保证 emplace_back 的紧凑性
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#elif defined(_MSC_VER)
    __declspec(noinline)
#endif
    void grow_capacity_to(size_t required_elements) {
        Header* header = get_header();
        if (!header) return;

        size_t required_bytes = required_elements * sizeof(T);
        size_t current_user_committed = header->committed_bytes - HEADER_OFFSET;

        // 维持指数级扩容策略
        size_t next_commit = current_user_committed == 0 ? header->page_size : current_user_committed * 2;
        next_commit = std::max(next_commit, required_bytes);
        commit_up_to(next_commit);
    }

public:
    PinnedVector() {
        size_t page_size = details::SysMem::get_page_size();
        if (page_size == 0) {
            throw std::runtime_error("Failed to query system page size");
        }

        void* addr = details::SysMem::reserve(RESERVED_BYTES);
        if (!addr) {
            throw std::bad_alloc();
        }
        Header* header = reinterpret_cast<Header*>(addr);

        size_t initial_commit = align_up(HEADER_OFFSET, page_size);
        if (!details::SysMem::commit(header, initial_commit)) {
            details::SysMem::release(header, RESERVED_BYTES);
            throw std::bad_alloc();
        }

        header->size = 0;
        header->committed_bytes = initial_commit;
        header->page_size = page_size;
        header->max_committed_elements = (initial_commit - HEADER_OFFSET) / sizeof(T);

        m_data = reinterpret_cast<T*>(reinterpret_cast<char*>(header) + HEADER_OFFSET);
    }

    ~PinnedVector() {
        clear();
        Header* header = get_header();
        if (header) {
            details::SysMem::release(header, RESERVED_BYTES);
        }
    }

    PinnedVector(const PinnedVector&) = delete;
    PinnedVector& operator=(const PinnedVector&) = delete;

    PinnedVector(PinnedVector&& other) noexcept
        : m_data(other.m_data) {
        other.m_data = nullptr;
    }

    PinnedVector& operator=(PinnedVector&& other) noexcept {
        if (this != &other) {
            clear();
            Header* header = get_header();
            if (header) {
                details::SysMem::release(header, RESERVED_BYTES);
            }
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        return *this;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        Header* header = get_header();
        if (!header) [[unlikely]] {
            throw std::out_of_range("PinnedVector is uninitialized or moved from");
        }
        if (header->size >= MAX_ELEMENTS) [[unlikely]] {
            throw std::out_of_range("PinnedVector capacity limit reached");
        }

        // 快速路径极其清爽：仅进行一次寄存器级别的比较
        if (header->size >= header->max_committed_elements) [[unlikely]] {
            grow_capacity_to(header->size + 1);
        }

        T* target = m_data + header->size;
        new (target) T(std::forward<Args>(args)...);
        header->size++;
        return *target;
    }

    void push_back(const T& value) {
        emplace_back(value);
    }

    void push_back(T&& value) {
        emplace_back(std::move(value));
    }

    void pop_back() {
        Header* header = get_header();
        if (!header || header->size == 0) return;
        m_data[header->size - 1].~T();
        header->size--;
    }

    void clear() noexcept {
        Header* header = get_header();
        if (!header) return;
        for (size_t i = 0; i < header->size; ++i) {
            m_data[i].~T();
        }
        header->size = 0;
    }

    void shrink_to_fit() {
        Header* header = get_header();
        if (!header) return;

        size_t min_commit = align_up(HEADER_OFFSET, header->page_size);

        if (header->size == 0) {
            if (header->committed_bytes > min_commit) {
                size_t excess_bytes = header->committed_bytes - min_commit;
                void* decommit_addr = reinterpret_cast<char*>(header) + min_commit;
                details::SysMem::decommit(decommit_addr, excess_bytes);
                header->committed_bytes = min_commit;
                header->max_committed_elements = (min_commit - HEADER_OFFSET) / sizeof(T);
            }
            return;
        }

        size_t needed_sys_bytes = align_up(HEADER_OFFSET + header->size * sizeof(T), header->page_size);
        if (needed_sys_bytes < header->committed_bytes) {
            size_t excess_bytes = header->committed_bytes - needed_sys_bytes;
            void* decommit_addr = reinterpret_cast<char*>(header) + needed_sys_bytes;
            details::SysMem::decommit(decommit_addr, excess_bytes);
            header->committed_bytes = needed_sys_bytes;
            header->max_committed_elements = (needed_sys_bytes - HEADER_OFFSET) / sizeof(T);
        }
    }

    void reserve(size_t new_capacity) {
        Header* header = get_header();
        if (!header) {
            throw std::out_of_range("PinnedVector is uninitialized or moved from");
        }
        if (new_capacity > MAX_ELEMENTS) {
            throw std::out_of_range("Requested reserve size exceeds virtual capacity limits");
        }
        size_t required_bytes = new_capacity * sizeof(T);
        commit_up_to(required_bytes);
    }

    void resize(size_t new_size) {
        Header* header = get_header();
        if (!header) {
            throw std::out_of_range("PinnedVector is uninitialized or moved from");
        }
        if (new_size > MAX_ELEMENTS) {
            throw std::out_of_range("Requested size exceeds capacity limits");
        }
        if (new_size > header->size) {
            size_t required_bytes = new_size * sizeof(T);
            commit_up_to(required_bytes);
            for (size_t i = header->size; i < new_size; ++i) {
                new (m_data + i) T();
            }
        } else if (new_size < header->size) {
            for (size_t i = new_size; i < header->size; ++i) {
                m_data[i].~T();
            }
        }
        header->size = new_size;
    }

    inline reference operator[](size_t index) noexcept { return m_data[index]; }
    inline const_reference operator[](size_t index) const noexcept { return m_data[index]; }

    reference at(size_t index) {
        Header* header = get_header();
        if (!header || index >= header->size) throw std::out_of_range("Index out of bounds");
        return m_data[index];
    }

    const_reference at(size_t index) const {
        const Header* header = get_header();
        if (!header || index >= header->size) throw std::out_of_range("Index out of bounds");
        return m_data[index];
    }

    inline T* data() noexcept { return m_data; }
    inline const T* data() const noexcept { return m_data; }

    inline size_t size() const noexcept {
        const Header* header = get_header();
        return header ? header->size : 0;
    }

    inline size_t capacity() const noexcept { return MAX_ELEMENTS; }
    inline bool empty() const noexcept { return size() == 0; }

    inline T* begin() noexcept { return m_data; }
    inline const T* begin() const noexcept { return m_data; }
    inline const T* cbegin() const noexcept { return m_data; }
    inline T* end() noexcept { return m_data + size(); }
    inline const T* end() const noexcept { return m_data + size(); }
    inline const T* cend() const noexcept { return m_data + size(); }
};