#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include "../../stucanvas/utils/tiny_vector.hpp"

using namespace StuCanvas::utils;

// ─────────────────────────────────────────────────────────────────────────
// 定义 256 字节的高性能对齐结构体
// ─────────────────────────────────────────────────────────────────────────
struct alignas(16) LargeData {
    uint8_t bytes[256];

    // 默认构造函数：零初始化整个缓冲区
    LargeData() {
        std::memset(bytes, 0, sizeof(bytes));
    }

    // 带参构造函数：将整型值安全地复制到头部 4 字节，用于测试时校验数据
    explicit LargeData(int val) {
        std::memset(bytes, 0, sizeof(bytes));
        std::memcpy(bytes, &val, sizeof(int));
    }

    // 重载等于操作符：用于查找和擦除测试
    bool operator==(const LargeData& other) const {
        int val1, val2;
        std::memcpy(&val1, bytes, sizeof(int));
        std::memcpy(&val2, other.bytes, sizeof(int));
        return val1 == val2;
    }

    // 辅助求和：读取前 4 字节并转化为数值，用于防止编译器死代码消除
    long long value() const {
        int val;
        std::memcpy(&val, bytes, sizeof(int));
        return static_cast<long long>(val);
    }
};

// 阻止编译器死代码消除（Dead Code Elimination）的黑科技
template <typename T>
void do_not_optimize(T&& val) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : "+r" (val));
#else
    volatile auto sink = val;
    (void)sink;
#endif
}

// 计时器辅助类
class Timer {
    std::chrono::high_resolution_clock::time_point start_time;
public:
    Timer() { start_time = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
};

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "====================================================\n";
    std::cout << "  TinyVector vs std::vector Performance Test (256B)  \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态体积检查
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(LargeData):                " << sizeof(LargeData) << " bytes\n";
    std::cout << "sizeof(std::vector<LargeData>):   " << sizeof(std::vector<LargeData>) << " bytes\n";
    std::cout << "sizeof(TinyVector<LargeData>):    " << sizeof(TinyVector<LargeData>) << " bytes\n";
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：小对象高频创建/销毁模拟（CAD DAG 典型场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2] High-Frequency Small Allocations (CAD DAG Simulation) ---\n";
        std::cout << "Description: Create 1,000,000 instances, push 2 LargeData objects, destroy.\n";
        
        LargeData dummy_val1(123456);
        LargeData dummy_val2(789123); // 修改为安全的十进制整型

        // A. std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<LargeData> vec;
                vec.push_back(dummy_val1);
                vec.push_back(dummy_val2);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                TinyVector<LargeData> vec;
                vec.push_back(dummy_val1);
                vec.push_back(dummy_val2);
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：大规模顺序写入（由于对象较大，调整为 1,000,000 个元素）
    // ─────────────────────────────────────────────────────────────────────────
    constexpr int LargeScale = 1'000'000;
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write (256B per element) ---\n";
        std::cout << "Description: Push " << LargeScale << " LargeData elements sequentially.\n";

        // A. std::vector
        {
            Timer t;
            std::vector<LargeData> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(LargeData(i));
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            TinyVector<LargeData> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(LargeData(i));
            }
            do_not_optimize(vec.data());
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 准备大型数据集用于读取测试
    std::vector<LargeData> std_large;
    TinyVector<LargeData> tiny_large;
    std_large.reserve(LargeScale);
    tiny_large.reserve(LargeScale);
    for (int i = 0; i < LargeScale; ++i) {
        std_large.push_back(LargeData(i));
        tiny_large.push_back(LargeData(i));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：顺序读取/迭代器遍历（缓存局部性测试，使用引用避免无谓拷贝）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration (by const reference) ---\n";
        std::cout << "Description: Traverse " << LargeScale << " elements using range-based for.\n";

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (const auto& x : std_large) {
                sum += x.value();
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // B. TinyVector
        {
            Timer t;
            long long sum = 0;
            for (const auto& x : tiny_large) {
                sum += x.value();
            }
            do_not_optimize(sum);
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：随机索引读取（微调为 2,000,000 次随机读取）
    // ─────────────────────────────────────────────────────────────────────────
    {
        constexpr int RandomReadCount = 2'000'000;
        std::cout << "--- [Test 5] Random Access Read ---\n";
        std::cout << "Description: Perform " << RandomReadCount << " random operator[] reads.\n";

        std::vector<int> random_indices(RandomReadCount);
        std::mt19937 rng(1337);
        std::uniform_int_distribution<int> dist(0, LargeScale - 1);
        for (int i = 0; i < RandomReadCount; ++i) {
            random_indices[i] = dist(rng);
        }

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += std_large[idx].value();
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += tiny_large[idx].value();
            }
            do_not_optimize(sum);
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 6：CAD 特例——无序删除操作
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 6] CAD Specific: Unordered Erase ---\n";
        std::cout << "Description: Erase elements from middle of vectors of size 1,000 (15,000 times).\n";

        constexpr int EraseLoops = 15'000;
        constexpr int VectorSize = 1'000;

        // A. std::vector (使用 std::remove 配合 erase)
        {
            Timer t;
            for (int i = 0; i < EraseLoops; ++i) {
                std::vector<LargeData> vec;
                vec.reserve(VectorSize);
                for (int j = 0; j < VectorSize; ++j) {
                    vec.push_back(LargeData(j));
                }
                // 删除头部存放数值为 500 的 256 字节元素
                vec.erase(std::remove(vec.begin(), vec.end(), LargeData(500)), vec.end());
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector (remove+erase):  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector (使用定制的 erase_unordered)
        {
            Timer t;
            for (int i = 0; i < EraseLoops; ++i) {
                TinyVector<LargeData> vec;
                vec.reserve(VectorSize);
                for (int j = 0; j < VectorSize; ++j) {
                    vec.push_back(LargeData(j));
                }
                // 极速无序擦除
                vec.erase_unordered(LargeData(500));
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector (erase_unordered): " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}
