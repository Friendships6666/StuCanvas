// simple_vector.hpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator> // 用于标准反向迭代器支持
#include <new>
#include <stdexcept>
#include <type_traits>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_POPULATE
#define MAP_POPULATE 0x08000
#endif
#endif

namespace StuCanvas::utils {

// 1. 强行对齐至 64 字节的头部元数据
struct alignas(64) SimpleHeader {
  size_t size;     // 8 字节 (uint64_t)
  size_t capacity; // 8 字节 (uint64_t)
                   // 48 字节填充，完美保持数据区 64 字节对齐
};

constexpr size_t HEADER_SIZE = sizeof(SimpleHeader); // 64 字节
constexpr size_t PAGE_THRESHOLD = 4096;

static inline size_t align_up_page(size_t size, size_t alignment) noexcept {
  return (size + alignment - 1) & ~(alignment - 1);
}

// C++20 Concept 约束：强制要求类型 T 必须是 Trivially Copyable 且 Trivially
// Destructible
template <typename T>
  requires std::is_trivially_copyable_v<T> &&
           std::is_trivially_destructible_v<T>
class SimpleVector {
private:
  T *m_data = nullptr; // 栈上仅 8 字节

  inline SimpleHeader *get_header() const noexcept {
    return reinterpret_cast<SimpleHeader *>(reinterpret_cast<char *>(m_data) -
                                          HEADER_SIZE);
  }

  static void *allocate_block(size_t capacity) {
    size_t total_bytes = HEADER_SIZE + capacity * sizeof(T);
    if (total_bytes <= PAGE_THRESHOLD) {
#if defined(_MSC_VER)
      void *ptr = ::_aligned_malloc(total_bytes, 64);
#else
      void *ptr = nullptr;
      if (::posix_memalign(&ptr, 64, total_bytes) != 0)
        ptr = nullptr;
#endif
      if (!ptr) [[unlikely]]
        throw std::bad_alloc();
      return ptr;
    } else {
#ifdef __linux__
      size_t page_size = sysconf(_SC_PAGESIZE);
      size_t mapped_bytes = align_up_page(total_bytes, page_size);
      void *ptr = ::mmap(nullptr, mapped_bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
      if (ptr == MAP_FAILED) [[unlikely]]
        throw std::bad_alloc();
      return ptr;
#else
#if defined(_MSC_VER)
      void *ptr = ::_aligned_malloc(total_bytes, 64);
#else
      void *ptr = nullptr;
      if (::posix_memalign(&ptr, 64, total_bytes) != 0)
        ptr = nullptr;
#endif
      if (!ptr) [[unlikely]]
        throw std::bad_alloc();
      return ptr;
#endif
    }
  }

  static void deallocate_block(void *ptr, size_t capacity) noexcept {
    if (!ptr)
      return;
    size_t total_bytes = HEADER_SIZE + capacity * sizeof(T);
    if (total_bytes <= PAGE_THRESHOLD) {
#if defined(_MSC_VER)
      ::_aligned_free(ptr);
#else
      ::free(ptr);
#endif
    } else {
#ifdef __linux__
      size_t page_size = sysconf(_SC_PAGESIZE);
      size_t mapped_bytes = align_up_page(total_bytes, page_size);
      ::munmap(ptr, mapped_bytes);
#else
#if defined(_MSC_VER)
      ::_aligned_free(ptr);
#else
      ::free(ptr);
#endif
#endif
    }
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((noinline))
#elif defined(_MSC_VER)
  __declspec(noinline)
#endif
  void grow_capacity() {
    SimpleHeader *h = get_header();
    size_t old_cap = h->capacity;
    size_t new_cap = old_cap == 0 ? 16 : old_cap * 2;
    reserve(new_cap);
  }

public:
  // 🚀 STL 兼容类型定义
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;

  // 🚀 0 开销裸指针迭代器特化（实现极限 SIMD 向量化铺平）
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  SimpleVector() noexcept : m_data(nullptr) {}

  ~SimpleVector() noexcept {
    if (m_data) {
      SimpleHeader *h = get_header();
      deallocate_block(h, h->capacity);
    }
  }

  SimpleVector(const SimpleVector &) = delete;
  SimpleVector &operator=(const SimpleVector &) = delete;

  SimpleVector(SimpleVector &&other) noexcept : m_data(other.m_data) {
    other.m_data = nullptr;
  }

  SimpleVector &operator=(SimpleVector &&other) noexcept {
    if (this != &other) {
      if (m_data) {
        SimpleHeader *h = get_header();
        deallocate_block(h, h->capacity);
      }
      m_data = other.m_data;
      other.m_data = nullptr;
    }
    return *this;
  }

  void reserve(size_t new_cap) {
    if (!m_data) {
      void *ptr = allocate_block(new_cap);
      SimpleHeader *h = reinterpret_cast<SimpleHeader *>(ptr);
      h->size = 0;
      h->capacity = new_cap;
      m_data = reinterpret_cast<T *>(h + 1);
      return;
    }

    SimpleHeader *h = get_header();
    if (new_cap <= h->capacity)
      return;

    size_t old_cap = h->capacity;
    size_t old_total = HEADER_SIZE + old_cap * sizeof(T);
    size_t new_total = HEADER_SIZE + new_cap * sizeof(T);

    if (old_total <= PAGE_THRESHOLD) {
      void *new_ptr = allocate_block(new_cap);
      SimpleHeader *new_h = reinterpret_cast<SimpleHeader *>(new_ptr);
      new_h->size = h->size;
      new_h->capacity = new_cap;

      T *new_data = reinterpret_cast<T *>(new_h + 1);
      std::copy_n(m_data, h->size, new_data);

      deallocate_block(h, old_cap);
      m_data = new_data;
      return;
    }

#ifdef __linux__
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t old_mapped = align_up_page(old_total, page_size);
    size_t new_mapped = align_up_page(new_total, page_size);

    if (old_mapped == new_mapped) {
      h->capacity = new_cap;
      return;
    }

    void *new_ptr = ::mremap(h, old_mapped, new_mapped, MREMAP_MAYMOVE);
    if (new_ptr == MAP_FAILED) [[unlikely]]
      throw std::bad_alloc();

    SimpleHeader *new_h = reinterpret_cast<SimpleHeader *>(new_ptr);
    new_h->capacity = new_cap;
    m_data = reinterpret_cast<T *>(new_h + 1);
#else
    void *new_ptr = allocate_block(new_cap);
    Header *new_h = reinterpret_cast<Header *>(new_ptr);
    new_h->size = h->size;
    new_h->capacity = new_cap;

    T *new_data = reinterpret_cast<T *>(new_h + 1);
    std::copy_n(m_data, h->size, new_data);

    deallocate_block(h, old_cap);
    m_data = new_data;
#endif
  }

  inline void push_back(const T &val) {
    if (!m_data) [[unlikely]] {
      reserve(16);
    }
    SimpleHeader *h = get_header();
    if (h->size >= h->capacity) [[unlikely]] {
      grow_capacity();
      h = get_header();
    }
    m_data[h->size++] = val;
  }

  inline void push_back_unchecked(const T &val) noexcept {
    SimpleHeader *h = get_header();
    m_data[h->size++] = val;
  }

  // =========================================================
  // 🚀 核心函数：物理级无多态高效删除（erase）
  // 利用 std::copy 在 Trivially Copyable 下退化为极致的底层 memmove
  // =========================================================
  iterator erase(const_iterator pos) noexcept {
    SimpleHeader *h = get_header();
    iterator p = const_cast<iterator>(pos);
    if (p < m_data || p >= m_data + h->size) [[unlikely]]
      return end();

    size_t idx = p - m_data;
    if (idx + 1 < h->size) {
      // 编译器会自动将 std::copy 替换为极速 SIMD 数据块平移 [1.1.1, 1.2.7]
      std::copy(p + 1, m_data + h->size, p);
    }
    h->size--;
    return p;
  }

  iterator erase(const_iterator first, const_iterator last) noexcept {
    SimpleHeader *h = get_header();
    iterator f = const_cast<iterator>(first);
    iterator l = const_cast<iterator>(last);

    if (f < m_data || l > m_data + h->size || f > l) [[unlikely]]
      return end();

    size_t num_to_remove = l - f;
    if (num_to_remove == 0)
      return f;

    if (l < m_data + h->size) {
      std::copy(l, m_data + h->size, f);
    }
    h->size -= num_to_remove;
    return f;
  }

  // =========================================================
  // 🚀 核心函数：迭代器接口获取
  // =========================================================
  inline iterator begin() noexcept { return m_data; }
  inline const_iterator begin() const noexcept { return m_data; }
  inline const_iterator cbegin() const noexcept { return m_data; }

  inline iterator end() noexcept {
    return m_data ? m_data + get_header()->size : nullptr;
  }
  inline const_iterator end() const noexcept {
    return m_data ? m_data + get_header()->size : nullptr;
  }
  inline const_iterator cend() const noexcept {
    return m_data ? m_data + get_header()->size : nullptr;
  }

  inline reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  inline const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  inline const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(cend());
  }

  inline reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  inline const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }
  inline const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(cbegin());
  }

  // =========================================================
  // 🚀 核心控制接口
  // =========================================================
  inline void clear() noexcept {
    if (m_data)
      get_header()->size = 0;
  }
  inline bool empty() const noexcept { return size() == 0; }

  inline T &operator[](size_t idx) noexcept { return m_data[idx]; }
  inline const T &operator[](size_t idx) const noexcept { return m_data[idx]; }
  inline size_t size() const noexcept {
    return m_data ? get_header()->size : 0;
  }
  inline size_t capacity() const noexcept {
    return m_data ? get_header()->capacity : 0;
  }
  inline T *data() noexcept { return m_data; }
  inline const T *data() const noexcept { return m_data; }
};
} // namespace StuCanvas::utils
