// main.cpp
#include "simple_vector.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <random>
#include <vector>

template <typename T> void do_not_optimize(const T &val) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(&val) : "memory");
#else
  const volatile void *p = static_cast<const volatile void *>(&val);
  (void)p;
#endif
}

struct Timer {
  std::string name;
  std::chrono::high_resolution_clock::time_point start;
  Timer(std::string n)
      : name(std::move(n)), start(std::chrono::high_resolution_clock::now()) {}
  ~Timer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "  " << std::left << std::setw(50) << name << " : "
              << std::right << std::setw(10) << std::fixed
              << std::setprecision(3) << duration << " ms" << std::endl;
  }
};

int main() {
  std::cout
      << "=================================================================="
      << std::endl;
  std::cout
      << "               Read Performance Benchmarks (10M Items)            "
      << std::endl;
  std::cout
      << "=================================================================="
      << std::endl;

  constexpr size_t N = 10'000'000; // 1000 万数据量（40MB）
  constexpr size_t ITERATIONS = 10;

  // 1. 数据初始化（预先填充）
  std::vector<uint32_t> std_vec(N);
  std::iota(std_vec.begin(), std_vec.end(), 1); // 填充 1, 2, 3 ... N

  std::vector<char> pmr_buffer(N * sizeof(uint32_t) + 1024);
  std::pmr::monotonic_buffer_resource mem_pool(pmr_buffer.data(),
                                               pmr_buffer.size());
  std::pmr::vector<uint32_t> pmr_vec(&mem_pool);
  pmr_vec.assign(std_vec.begin(), std_vec.end());

  // 泛型化的 SimpleVector<uint32_t> [1.1.7]
  custom::SimpleVector<uint32_t> s_vec;
svec_block: {
  s_vec.reserve(N);
  for (uint32_t i = 0; i < N; ++i)
    s_vec.push_back_unchecked(i + 1);
}

  // 2. 生成随机访问乱序下标
  std::vector<size_t> random_indices(N);
  std::iota(random_indices.begin(), random_indices.end(), 0);
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(random_indices.begin(), random_indices.end(), g);

  // =========================================================================
  // 🚀 读场景一：顺序读取并求和（Sequential Read & Accumulate）
  // =========================================================================
  std::cout << "\n[1] Sequential Read & Accumulate (Average of " << ITERATIONS
            << " runs):" << std::endl;

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("A. std::vector Sequential Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += std_vec[i];
    }
    do_not_optimize(sum);
  }

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("B. std::pmr::vector Sequential Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += pmr_vec[i];
    }
    do_not_optimize(sum);
  }

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("C. custom::SimpleVector Sequential Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += s_vec[i]; // 💡 完美命中 64-byte 缓存行的指针寻址
    }
    do_not_optimize(sum);
  }

  // =========================================================================
  // 🚀 读场景二：完全乱序随机读取（Random Read & Accumulate）
  // =========================================================================
  std::cout << "\n[2] Random Read & Accumulate (Average of " << ITERATIONS
            << " runs):" << std::endl;

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("A. std::vector Random Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += std_vec[random_indices[i]];
    }
    do_not_optimize(sum);
  }

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("B. std::pmr::vector Random Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += pmr_vec[random_indices[i]];
    }
    do_not_optimize(sum);
  }

  for (size_t iter = 0; iter < ITERATIONS; ++iter) {
    Timer t("C. custom::SimpleVector Random Read");
    uint64_t sum = 0;
    for (size_t i = 0; i < N; ++i) {
      sum += s_vec[random_indices[i]];
    }
    do_not_optimize(sum);
  }

  std::cout
      << "=================================================================="
      << std::endl;
  return 0;
}
