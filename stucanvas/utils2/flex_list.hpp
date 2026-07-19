/***************************************************************************
 * Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
 *                                                                          *
 * Distributed under the terms of the MIT License.                          *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#pragma once
#include "flex_buffer.hpp" // 复用分配器
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace StuCanvas {
namespace utils {
// =====================================================================
// 🚀 全局类型注册器：利用 C++23 自动在编译期/运行时分配紧凑且连续的唯一 ID
// =====================================================================
class TypeRegistry {
private:
  inline static uint32_t s_counter = 0; // 保证多翻译单元下的全局唯一计数

public:
  template <typename T> [[nodiscard]] static uint32_t get_id() noexcept {
    // 局部静态变量的初始化是保证多线程安全的（Magic Statics）
    // 第一次调用 get_id<T>() 时会为类型 T 固化一个连续的 ID (0, 1, 2...)
    static const uint32_t id = s_counter++;
    return id;
  }
};

// =====================================================================
// FlexList 弹性离散列表 (全自动独占模式：1 类型 ↔ 1 插槽)
// =====================================================================
class FlexList {
private:
  using DeleterFunc = void (*)(void *) noexcept;

  // 每个插槽占用 16 字节
  struct IDPointer {
    void *pointer;
    DeleterFunc deleter;
  };

  // 8 字节头部
  struct Header {
    uint32_t capacity; // 4 字节
    uint32_t count;    // 4 字节
  };

  Header *m_ptr = nullptr; // 唯一成员变量：8 字节

  // =================================================================
  // 🚀 私有静态模板销毁器（利用其特化地址作为类型标记，并抵抗链接器 ICF 合并）
  // =================================================================
  template <typename T> static void default_deleter(void *ptr) noexcept {
    static const char dummy = 0;
    [[maybe_unused]] static volatile const char *prevent_icf = &dummy;

    static_cast<T *>(ptr)->~T(); // 显式释放内部成员
    std::free(ptr);              // 释放容器本身物理块
  }

  // 预分配指针槽位（私有化，容量扩容转为由系统隐式按需维护）
  void reserve(size_t capacity_count) {
    uint32_t current_cap = m_ptr ? m_ptr->capacity : 0;
    if (capacity_count <= current_cap)
      return;
    uint32_t current_count = m_ptr ? m_ptr->count : 0;

    size_t old_alloc_size =
        current_cap == 0 ? 0 : sizeof(Header) + current_cap * sizeof(IDPointer);
    size_t new_alloc_size = sizeof(Header) + capacity_count * sizeof(IDPointer);

    void *raw = detail::get_fast_allocator().reallocate(m_ptr, old_alloc_size,
                                                        new_alloc_size);
    if (!raw) [[unlikely]]
      return;

    m_ptr = static_cast<Header *>(raw);
    m_ptr->capacity = static_cast<uint32_t>(capacity_count);

    if (current_count == 0) {
      m_ptr->count = 0;
    }

    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
    std::memset(pairs + current_cap, 0,
                (capacity_count - current_cap) * sizeof(IDPointer));
  }

public:
  FlexList() noexcept = default;

  ~FlexList() noexcept { clear_and_free(); }

  FlexList(const FlexList &) = delete;
  FlexList &operator=(const FlexList &) = delete;
  FlexList(FlexList &&other) noexcept : m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;
  }

  FlexList &operator=(FlexList &&other) noexcept {
    if (this != &other) {
      clear_and_free();
      m_ptr = other.m_ptr;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  // =================================================================
  // 🚀 全自动原位构造（若重复插入同类型，将安全地覆盖并销毁旧实例）
  // =================================================================
  template <typename T, typename... Args> T *emplace(Args &&...args) {
    const uint32_t id = TypeRegistry::get_id<T>();
    const uint32_t current_count = m_ptr ? m_ptr->count : 0;
    const uint32_t current_cap = m_ptr ? m_ptr->capacity : 0;

    // 按需动态增长偏置表物理深度
    if (id >= current_cap) {
      uint32_t new_cap = current_cap == 0 ? 8 : current_cap * 2;
      while (new_cap <= id) {
        new_cap *= 2;
      }
      reserve(new_cap);
    }

    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);

    // 独占资产保护：如果此类型已有实例存在，先触发旧实例销毁，实现安全覆盖
    if (pairs[id].pointer != nullptr) {
      pairs[id].deleter(pairs[id].pointer);
    } else {
      m_ptr->count = current_count + 1;
    }

    // 在堆上分配并构造新实例
    void *raw_asset = std::malloc(sizeof(T));
    if (!raw_asset) [[unlikely]]
      return nullptr;

    T *asset_ptr = ::new (raw_asset) T(std::forward<Args>(args)...);

    // 记录新实例指针及其专属销毁器
    pairs[id] = IDPointer{static_cast<void *>(asset_ptr), &default_deleter<T>};

    return asset_ptr;
  }

  // =================================================================
  // 🚀 全自动 push 接口支持
  // =================================================================
  template <typename T> T *push(const T &val) { return emplace<T>(val); }

  template <typename T> T *push(T &&val) { return emplace<T>(std::move(val)); }

  // =================================================================
  // 🚀 全自动 $O(1)$ 资产销毁（自动识别类型并重置物理插槽）
  // =================================================================
  template <typename T> void erase() noexcept {
    if (!m_ptr)
      return;
    const uint32_t id = TypeRegistry::get_id<T>();
    if (id >= m_ptr->capacity)
      return;

    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
    // 只有当指针存在且销毁器严格一致（类型匹配）时才执行销毁
    if (pairs[id].pointer && pairs[id].deleter == &default_deleter<T>) {
      pairs[id].deleter(pairs[id].pointer);
      pairs[id].pointer = nullptr;
      pairs[id].deleter = nullptr;
      m_ptr->count--;
    }
  }

  // =================================================================
  // 🚀 全自动类型存在性校验
  // =================================================================
  template <typename T> [[nodiscard]] bool has() const noexcept {
    if (!m_ptr)
      return false;
    const uint32_t id = TypeRegistry::get_id<T>();
    if (id >= m_ptr->capacity)
      return false;

    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
    return pairs[id].pointer != nullptr &&
           pairs[id].deleter == &default_deleter<T>;
  }

  // =================================================================
  // 🚀 全自动极速无安全检查读取（适合确定存在的内部热循环访问）
  // =================================================================
  template <typename T> [[nodiscard]] inline T *get_unsafe() const noexcept {
    const uint32_t id = TypeRegistry::get_id<T>();
    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
    return static_cast<T *>(pairs[id].pointer);
  }

  // =================================================================
  // 🚀 全自动安全版读取
  // =================================================================
  template <typename T> [[nodiscard]] inline T *get() const noexcept {
    if (!m_ptr)
      return nullptr;
    const uint32_t id = TypeRegistry::get_id<T>();
    if (id >= m_ptr->capacity)
      return nullptr;

    auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
    if (pairs[id].deleter != &default_deleter<T>)
      return nullptr;
    return static_cast<T *>(pairs[id].pointer);
  }

  // 清空所有资产
  void clear() noexcept {
    if (m_ptr) {
      auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
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

  [[nodiscard]] size_t size() const noexcept {
    return m_ptr ? m_ptr->count : 0;
  }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

private:
  void clear_and_free() noexcept {
    if (m_ptr) {
      auto *pairs = reinterpret_cast<IDPointer *>(m_ptr + 1);
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
