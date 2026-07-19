#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// 防止编译器将未使用的读取操作优化掉
template <typename T> void do_not_optimize(T &&val) {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(val) : "memory");
#else
  volatile T dummy = val;
#endif
}

enum class MyEnum : uint32_t { VALUE_A };

// 对应 8128 字节 (大约 8KB) 的冷数据
const size_t COLD_SIZE = 2000;

// ==========================================
// 1. 不展开 (Unexpanded)：结构体本身极小 (96 字节)
// ==========================================
struct SObjectUnexpanded {
  MyEnum type;
  double hot_data[8];            // 64 字节 (内联热数据)
  std::vector<double> cold_data; // 24 字节 (指向外部堆的冷数据 vector)
};


// ==========================================
// 2. 展开 (Expanded)：结构体本身巨大 (8200 字节)
// ==========================================
struct SObjectExpanded {
  MyEnum type;
  double hot_data[8];          // 64 字节 (内联热数据)
  double cold_data[COLD_SIZE]; // 8128 字节 (冷数据，直接内联展开)
};

// 为了让测试稳定且不超出常规系统的 contiguous memory 申请上限
// 我们将数量设定为极其稳定的 100,000 个（10万个对象）
const size_t NUM_ELEMENTS = 100'00;
const int RUN_COUNT = 3;

int main() {
  std::cout << "==============================================================="
               "================="
            << std::endl;
  std::cout << "      [对象装入 vector] 展开 (Expanded) vs 不展开 (Unexpanded) "
               "性能对比"
            << std::endl;
  std::cout << "      测试内容：只读取前 8 个内联 double (hot_data)"
            << std::endl;
  std::cout << "==============================================================="
               "================="
            << std::endl;
  std::cout << "正在初始化连续对象容器中...\n" << std::endl;

  // 🚀 A. 直接分配对象容器：堆分配器一次性划定 100,000 * 96 字节 = 9.6 MB
  // 的纯净、无污染连续空间！
  std::vector<SObjectUnexpanded> list_unexpanded(NUM_ELEMENTS);
  for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
    list_unexpanded[i].type = MyEnum::VALUE_A;
    for (int j = 0; j < 8; ++j) {
      list_unexpanded[i].hot_data[j] = static_cast<double>(i + j);
    }
    // 外部大数组分配去远端堆，由于堡垒已成，这 8KB 绝无可能插入到结构体之间
    list_unexpanded[i].cold_data.resize(COLD_SIZE, 1.0);
  }

  // 🚀 B. 直接分配对象容器：一次性划定 100,000 * 8200 字节 = 820 MB
  // 的超大连续空间
  std::vector<SObjectExpanded> list_expanded(NUM_ELEMENTS);
  for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
    list_expanded[i].type = MyEnum::VALUE_A;
    for (int j = 0; j < 8; ++j) {
      list_expanded[i].hot_data[j] = static_cast<double>(i + j);
    }
    for (size_t j = 0; j < COLD_SIZE; ++j) {
      list_expanded[i].cold_data[j] = 1.0;
    }
  }
  std::cout << "结构体物理大小：" << std::endl;
  std::cout << "不展开 (Unexpanded) 结构体大小: " << sizeof(SObjectUnexpanded)
            << " 字节" << std::endl;
  std::cout << "展开 (Expanded) 结构体大小: " << sizeof(SObjectExpanded)
            << " 字节\n"
            << std::endl;

  std::cout << std::left << std::setw(15) << "测试读取目标" << std::setw(28)
            << "不展开 (96B 实体) ms" << std::setw(28) << "展开 (8200B 实体) ms"
            << "提升倍数 (不展开/展开)" << std::endl;
  std::cout << "---------------------------------------------------------------"
               "-----------------"
            << std::endl;

  for (size_t idx = 0; idx < 8; ++idx) {

    // 测试不展开的耗时
    double t_unexp = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
      auto start = std::chrono::high_resolution_clock::now();
      double sum = 0.0;
      for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 直接使用 [] 连续读取物理热通道
        sum += list_unexpanded[i].hot_data[idx];
      }
      auto end = std::chrono::high_resolution_clock::now();
      do_not_optimize(sum);
      t_unexp += std::chrono::duration<double, std::milli>(end - start).count();
    }
    t_unexp /= RUN_COUNT;

    // 测试展开的耗时
    double t_exp = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
      auto start = std::chrono::high_resolution_clock::now();
      double sum = 0.0;
      for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        sum += list_expanded[i].hot_data[idx];
      }
      auto end = std::chrono::high_resolution_clock::now();
      do_not_optimize(sum);
      t_exp += std::chrono::duration<double, std::milli>(end - start).count();
    }
    t_exp /= RUN_COUNT;

    double ratio = t_exp / t_unexp;

    std::cout << std::left << std::setw(15)
              << ("hot_data[" + std::to_string(idx) + "]") << std::setw(28)
              << std::fixed << std::setprecision(4) << t_unexp << std::setw(28)
              << std::fixed << std::setprecision(4) << t_exp << std::fixed
              << std::setprecision(2) << ratio << " x" << std::endl;
  }

  std::cout << "---------------------------------------------------------------"
               "-----------------"
            << std::endl;
  return 0;
}
