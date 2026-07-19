#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include "tiny_vector.hpp"

using namespace StuCanvas;
// 1. 编译期计算 log2
constexpr size_t compile_time_log2(size_t n) {
  return (n <= 1) ? 0 : 1 + compile_time_log2(n / 2);
}

// 2. 16 字节对齐的 128 字节结构体
struct alignas(16) DAGObject {
  uint32_t id;
  volatile float value;
  uint32_t
      padding[30]; // 4字节(id) + 4字节(value) + 120字节(padding) = 128 字节

  void update() { value += 1.0f; }
};

// 静态断言验证结构体大小与对齐方式
static_assert(sizeof(DAGObject) == 128,
              "DAGObject size must be exactly 128 bytes");
static_assert(alignof(DAGObject) == 16,
              "DAGObject alignment must be exactly 16 bytes");

// 3. 线程局部基地址
inline thread_local char *g_active_pool_base = nullptr;

// 4. 支持对齐移位的 4 字节压缩指针（最大寻址空间：AlignBytes * 4GB）
template <typename T, size_t AlignBytes = alignof(T)>
struct CompactPtr32Aligned {
  static_assert((AlignBytes & (AlignBytes - 1)) == 0,
                "Alignment must be a power of 2");
  static constexpr size_t Shift = compile_time_log2(AlignBytes);

  uint32_t aligned_offset;

  CompactPtr32Aligned() : aligned_offset(0xFFFFFFFF) {}

  CompactPtr32Aligned(T *ptr) {
    if (ptr == nullptr) {
      aligned_offset = 0xFFFFFFFF;
    } else {
      size_t byte_offset = reinterpret_cast<char *>(ptr) - g_active_pool_base;
      // 安全断言：确保偏移地址确实满足对齐要求
      assert((byte_offset & (AlignBytes - 1)) == 0 &&
             "Pointer offset alignment mismatch!");

      // 压缩：右移抹去低位的 0 占位
      aligned_offset = static_cast<uint32_t>(byte_offset >> Shift);
    }
  }

  inline T *get() const {
    if (aligned_offset == 0xFFFFFFFF) [[unlikely]]
      return nullptr;
    // 解压：左移补回 0 占位并加上基地址
    return reinterpret_cast<T *>(
        g_active_pool_base + (static_cast<size_t>(aligned_offset) << Shift));
  }

  inline T *operator->() const { return get(); }
  inline T &operator*() const { return *get(); }
};

int main() {
  constexpr size_t test_iterations = 5;

  std::cout << "=== CONFIGURATION INFO ===" << std::endl;
  std::cout << "DAGObject Size      : " << sizeof(DAGObject) << " bytes"
            << std::endl;
  std::cout << "DAGObject Alignment : " << alignof(DAGObject) << " bytes"
            << std::endl;
  std::cout << "Bit Shift Offset    : " << CompactPtr32Aligned<DAGObject>::Shift
            << " bits" << std::endl;
  std::cout << "Max Addressing Space: "
            << (4ULL * 1024 * 1024 * 1024 * alignof(DAGObject)) /
                   (1024 * 1024 * 1024)
            << " GB" << std::endl;
  std::cout << "==========================" << std::endl;

  // ====================================================
  // 测试场景 A：单一对象极端循环（测试纯 CPU 解码移位开销）
  // ====================================================
  {
    constexpr size_t single_loop_count = 100000000; // 1 亿次
    utils::TinyVector<DAGObject> single_pool(1);
    single_pool[0].id = 0;
    single_pool[0].value = 0.0f;
    g_active_pool_base = reinterpret_cast<char *>(single_pool.data());

    utils::TinyVector<DAGObject *> raw_single;
    raw_single.push_back(&single_pool[0]);

    utils::TinyVector<CompactPtr32Aligned<DAGObject>> comp32_single;
    comp32_single.push_back(CompactPtr32Aligned<DAGObject>(&single_pool[0]));

    std::cout
        << "\n=== SCENARIO A: Single-Object Pure CPU Overhead (100M loops) ==="
        << std::endl;

    // 1. 原始 8 字节指针
    double raw_ms = 0;
    for (size_t iter = 0; iter < test_iterations; ++iter) {
      single_pool[0].value = 0.0f;
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < single_loop_count; ++i) {
        raw_single[0]->update();
#if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : : "memory");
#endif
      }
      auto end = std::chrono::high_resolution_clock::now();
      raw_ms += std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::cout << ">> Raw Pointers (8B)             : "
              << (raw_ms / test_iterations) << " ms" << std::endl;

    // 2. 4 字节对齐压缩指针
    double comp32_ms = 0;
    for (size_t iter = 0; iter < test_iterations; ++iter) {
      single_pool[0].value = 0.0f;
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < single_loop_count; ++i) {
        comp32_single[0]->update();
#if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : : "memory");
#endif
      }
      auto end = std::chrono::high_resolution_clock::now();
      comp32_ms +=
          std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::cout << ">> CompactPtr32 Aligned (4B)     : "
              << (comp32_ms / test_iterations) << " ms" << std::endl;
  }

  // ====================================================
  // 测试场景 B：多对象（60,000个，占约 7.6MB 内存）乱序访问
  // ====================================================
  {
    constexpr size_t multi_pool_size = 60000;
    utils::TinyVector<DAGObject> multi_pool(multi_pool_size);
    for (size_t i = 0; i < multi_pool_size; ++i) {
      multi_pool[i].id = i;
      multi_pool[i].value = static_cast<float>(i);
    }
    g_active_pool_base = reinterpret_cast<char *>(multi_pool.data());

    // 生成乱序随机索引
    utils::TinyVector<size_t> indices(multi_pool_size);
    std::iota(indices.begin(), indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(indices.begin(), indices.end(), g);

    utils::TinyVector<DAGObject *> raw_multi;
    utils::TinyVector<CompactPtr32Aligned<DAGObject>> comp32_multi;

    for (size_t idx : indices) {
      raw_multi.push_back(&multi_pool[idx]);
      comp32_multi.push_back(CompactPtr32Aligned<DAGObject>(&multi_pool[idx]));
    }

    std::cout
        << "\n=== SCENARIO B: Multi-Object (60,000 nodes) Shuffled Access ==="
        << std::endl;
    std::cout << "Raw Pointer Vector Size          : "
              << (raw_multi.size() * sizeof(DAGObject *) / 1024.0) << " KB"
              << std::endl;
    std::cout << "CompactPtr32 Aligned Vector Size : "
              << (comp32_multi.size() * sizeof(CompactPtr32Aligned<DAGObject>) /
                  1024.0)
              << " KB (Saved 50%)" << std::endl;
    std::cout << "--------------------------------------------------------"
              << std::endl;

    // 1. 原始 8 字节指针
    double raw_ms = 0;
    for (size_t iter = 0; iter < test_iterations; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < raw_multi.size(); ++i) {
        raw_multi[i]->update();
      }
      auto end = std::chrono::high_resolution_clock::now();
      raw_ms += std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::cout << ">> Raw Pointers (8B)             : "
              << (raw_ms / test_iterations) << " ms" << std::endl;

    // 2. 4 字节对齐压缩指针
    double comp32_ms = 0;
    for (size_t iter = 0; iter < test_iterations; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < comp32_multi.size(); ++i) {
        comp32_multi[i]->update();
      }
      auto end = std::chrono::high_resolution_clock::now();
      comp32_ms +=
          std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::cout << ">> CompactPtr32 Aligned (4B)     : "
              << (comp32_ms / test_iterations) << " ms" << std::endl;
  }

  return 0;
}
