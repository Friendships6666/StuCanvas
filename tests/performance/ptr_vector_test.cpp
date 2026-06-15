/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <random>
#include <cassert>
#include "ptr_vector.hpp" // 引入我们刚刚重写的极致指针特化版 PtrVector

using namespace StuCanvas::utils;

// 阻止编译器死代码消除（DCE）的平台自适应黑科技（完美支持 GCC & MSVC）
template <typename T>
void do_not_optimize(T&& val) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : "+r" (val));
#else
    volatile auto sink = val;
    (void)sink;
#endif
}

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
    std::cout << "       PtrVector vs std::vector Performance Test    \n";
    std::cout << "       (Specialized for Pointer Types: int*)        \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：物理开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):  " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(PtrVector<int*>):   " << sizeof(PtrVector<int*>) << " bytes\n";
    std::cout << "----------------------------------------------------\n\n";

    // 准备用于模拟的对象指针
    int* dummy_node1 = reinterpret_cast<int*>(0x100000);
    int* dummy_node2 = reinterpret_cast<int*>(0x200000);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2A：高频单指针写入与销毁（最常见的单父/单子连线场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2A] Small Scale: Push 1 Pointer & Destroy (1,000,000 times) ---\n";
        std::cout << "Description: Simulates single-parent relationships (Pure Tag 0, Zero-Heap Alloc).\n";

        // std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<int*> vec;
                vec.push_back(dummy_node1);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                PtrVector<int*> vec;
                vec.push_back(dummy_node1);
                do_not_optimize(vec.data());
            }
            std::cout << "  PtrVector:  " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2B：高频双指针写入与销毁（中点、距离等双亲关系场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2B] Small Scale: Push 2 Pointers & Destroy (1,000,000 times) ---\n";
        std::cout << "Description: Simulates dual-parent relationships (Tag 1 + Thread Cache).\n";

        // std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<int*> vec;
                vec.push_back(dummy_node1);
                vec.push_back(dummy_node2);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                PtrVector<int*> vec;
                vec.push_back(dummy_node1);
                vec.push_back(dummy_node2);
                do_not_optimize(vec.data());
            }
            std::cout << "  PtrVector:  " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：超大规模顺序写入能力对比（1千万级写测试）
    // ─────────────────────────────────────────────────────────────────────────
    constexpr int LargeScale = 10'000'000;
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially (Triggering expansion).\n";

        // std::vector
        {
            Timer t;
            std::vector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node1);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrVector
        {
            Timer t;
            PtrVector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node1);
            }
            do_not_optimize(vec.data());
            std::cout << "  PtrVector:  " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 为读取测试构建大型数据集
    std::vector<int*> std_large;
    PtrVector<int*> ptr_large;
    std_large.reserve(LargeScale);
    ptr_large.reserve(LargeScale);
    for (int i = 0; i < LargeScale; ++i) {
        std_large.push_back(reinterpret_cast<int*>(i * 8));
        ptr_large.push_back(reinterpret_cast<int*>(i * 8));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：顺序读取与遍历（缓存友好度与矢量化对齐测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration (10,000,000 elements) ---\n";
        std::cout << "Description: Traverse elements using range-based for and accumulate values.\n";

        // std::vector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : std_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // PtrVector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : ptr_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  PtrVector:  " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：随机索引访问速度对比（operator[] 寻址性能测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 5] Random Access Read ---\n";
        std::cout << "Description: Perform 5,000,000 random operator[] reads.\n";

        std::vector<int> random_indices(5'000'000);
        std::mt19937 rng(1337);
        std::uniform_int_distribution<int> dist(0, LargeScale - 1);
        for (int i = 0; i < 5'000'000; ++i) {
            random_indices[i] = dist(rng);
        }

        // std::vector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += reinterpret_cast<uintptr_t>(std_large[idx]);
            }
            do_not_optimize(sum);
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrVector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += reinterpret_cast<uintptr_t>(ptr_large[idx]);
            }
            do_not_optimize(sum);
            std::cout << "  PtrVector:  " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}