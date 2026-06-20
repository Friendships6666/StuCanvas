#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <string>

// 防止编译器将未使用的读取操作优化掉
template <typename T>
void do_not_optimize(T&& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(val) : "memory");
#else
    volatile T dummy = val;
#endif
}

enum class MyEnum : uint32_t {
    VALUE_A
};

// ==========================================
// 堆上对象：内联不同大小数组的 SObject 模板
// ==========================================
template <size_t N>
struct SObject {
    MyEnum type;
    double data[N]; // 顺序紧凑的内联数据区
};

// 保持 300,000 个堆对象，与您之前的测试环境完全一致
const size_t NUM_ELEMENTS = 300'000;
const int RUN_COUNT = 5;

template <size_t N>
void test_all_elements_individually() {
    std::cout << "\n================================================================================" << std::endl;
    std::cout << " 测试规格: SObject<" << N << "> (" << sizeof(SObject<N>) << " 字节/对象)" << std::endl;
    std::cout << " 堆总大小: " << (sizeof(SObject<N>) * NUM_ELEMENTS) / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "正在分配内存并初始化..." << std::endl;

    // 堆分配
    std::vector<SObject<N>*> list(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list[i] = new SObject<N>();
        list[i]->type = MyEnum::VALUE_A;
        for (size_t j = 0; j < N; ++j) {
            list[i]->data[j] = static_cast<double>(i + j);
        }
    }

    std::cout << "测试开始..." << std::endl;
    std::cout << std::left
              << std::setw(20) << "测试读取目标"
              << "30万次遍历总耗时 (ms)" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;

    // 🚀 核心：不求平均数，逐一测试并打印每一个 data[idx] 的独立耗时
    for (size_t idx = 0; idx < N; ++idx) {
        double total_ms = 0.0;
        for (int r = 0; r < RUN_COUNT; ++r) {
            auto start = std::chrono::high_resolution_clock::now();
            double sum = 0.0;

            for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
                sum += list[i]->data[idx]; // 仅读取当前索引的数据
            }

            auto end = std::chrono::high_resolution_clock::now();
            do_not_optimize(sum);
            total_ms += std::chrono::duration<double, std::milli>(end - start).count();
        }

        double avg_ms = total_ms / RUN_COUNT;

        std::cout << std::left
                  << std::setw(20) << ("data[" + std::to_string(idx) + "]")
                  << std::fixed << std::setprecision(4) << avg_ms << " ms" << std::endl;
    }

    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "正在清理内存..." << std::endl;
    for (auto* p : list) {
        delete p;
    }
}

int main() {
    // 依次运行 8, 16, 32, 64 规格的逐个元素详尽测试
    test_all_elements_individually<1>();
    test_all_elements_individually<16>();
    test_all_elements_individually<32>();
    test_all_elements_individually<1024>();
    return 0;
}