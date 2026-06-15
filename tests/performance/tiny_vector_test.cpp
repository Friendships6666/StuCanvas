#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <iomanip>
#include "../../stucanvas/utils/tiny_vector.hpp"

using namespace StuCanvas::utils;

// 阻止编译器死代码消除（Dead Code Elimination）的黑科技
template <typename T>
void do_not_optimize(T&& val) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : "+r" (val));
#else
    // 兼容其他编译器的降级方案
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
    std::cout << "       TinyVector vs std::vector Performance Test    \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态体积检查
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):  " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(TinyVector<int*>):   " << sizeof(TinyVector<int*>) << " bytes\n";
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：小对象高频创建/销毁模拟（CAD DAG 典型场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2] High-Frequency Small Allocations (CAD DAG Simulation) ---\n";
        std::cout << "Description: Create 1,000,000 instances, push 2 pointers, destroy.\n";
        
        int* dummy_ptr1 = reinterpret_cast<int*>(0x123456);
        int* dummy_ptr2 = reinterpret_cast<int*>(0x789abc);

        // A. std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<int*> vec;
                vec.push_back(dummy_ptr1);
                vec.push_back(dummy_ptr2);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                TinyVector<int*> vec;
                vec.push_back(dummy_ptr1);
                vec.push_back(dummy_ptr2);
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：大规模顺序写入（扩容压力测试）
    // ─────────────────────────────────────────────────────────────────────────
    constexpr int LargeScale = 10'000'000;
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write ---\n";
        std::cout << "Description: Push " << LargeScale << " elements sequentially.\n";

        // A. std::vector
        {
            Timer t;
            std::vector<int> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(i);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            TinyVector<int> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(i);
            }
            do_not_optimize(vec.data());
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 准备大型数据集用于读取测试
    std::vector<int> std_large;
    TinyVector<int> tiny_large;
    std_large.reserve(LargeScale);
    tiny_large.reserve(LargeScale);
    for (int i = 0; i < LargeScale; ++i) {
        std_large.push_back(i);
        tiny_large.push_back(i);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：顺序读取/迭代器遍历（缓存局部性测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration ---\n";
        std::cout << "Description: Traverse " << LargeScale << " elements using range-based for.\n";

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (auto x : std_large) {
                sum += x;
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // B. TinyVector
        {
            Timer t;
            long long sum = 0;
            for (auto x : tiny_large) {
                sum += x;
            }
            do_not_optimize(sum);
            std::cout << "  TinyVector:   " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：随机索引读取
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 5] Random Access Read ---\n";
        std::cout << "Description: Perform 5,000,000 random operator[] reads.\n";

        // 预先生成随机索引，排除随机数生成本身的时间
        std::vector<int> random_indices(5'000'000);
        std::mt19937 rng(1337);
        std::uniform_int_distribution<int> dist(0, LargeScale - 1);
        for (int i = 0; i < 5'000'000; ++i) {
            random_indices[i] = dist(rng);
        }

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += std_large[idx];
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += tiny_large[idx];
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
                std::vector<int> vec(VectorSize);
                std::iota(vec.begin(), vec.end(), 0);
                // 每次删除位于中间位置的元素 500
                vec.erase(std::remove(vec.begin(), vec.end(), 500), vec.end());
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector (remove+erase):  " << t.elapsed_ms() << " ms\n";
        }

        // B. TinyVector (使用定制的 erase_unordered)
        {
            Timer t;
            for (int i = 0; i < EraseLoops; ++i) {
                TinyVector<int> vec;
                vec.reserve(VectorSize);
                for (int j = 0; j < VectorSize; ++j) vec.push_back(j);
                // 极速无序擦除
                vec.erase_unordered(500);
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector (erase_unordered): " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}