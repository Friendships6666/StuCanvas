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
#include "ptr_block_deque.hpp" // 引入极致指针特化版 PtrBlockDeque

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
    std::cout << "       PtrBlockDeque vs std::vector Benchmark       \n";
    std::cout << "       (Specialized for Pointer Types: int*)        \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：物理开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):  " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(PtrBlockDeque<int*>): " << sizeof(PtrBlockDeque<int*>) << " bytes\n";
    std::cout << "----------------------------------------------------\n\n";

    // 准备用于模拟的对象指针
    int* dummy_node = reinterpret_cast<int*>(0x12345678);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：核心物理特性——扩容时元素地址绝对稳定性验证
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 2] Element Address Stability Verification ---\n";
    std::cout << "Description: Capture 1st element's reference, insert 10,000 more items,\n"
              << "             and check if the reference pointer changes.\n";
    {
        std::vector<int*> vec;
        vec.push_back(dummy_node);
        int** addr_std_before = &vec[0];

        for (int i = 0; i < 10000; ++i) vec.push_back(dummy_node);

        int** addr_std_after = &vec[0];
        std::cout << "  std::vector address:  " << addr_std_before << " -> " << addr_std_after;
        if (addr_std_before != addr_std_after) {
            std::cout << " (地址发生偏移！连线断开！)\n";
        } else {
            std::cout << " (地址未改变)\n";
        }
    }
    {
        PtrBlockDeque<int*, 256> deque;
        deque.push_back(dummy_node);
        int** addr_deque_before = &deque[0];

        for (int i = 0; i < 10000; ++i) deque.push_back(dummy_node);

        int** addr_deque_after = &deque[0];
        std::cout << "  PtrBlockDeque address: " << addr_deque_before << " -> " << addr_deque_after;
        if (addr_deque_before == addr_deque_after) {
            std::cout << " (🚀 地址绝对稳定！指针连线依然有效！)\n";
        } else {
            std::cout << " (地址发生偏移)\n";
        }
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3A：大规模顺序写入（无预分配 —— 动态扩容压力测试）
    // ─────────────────────────────────────────────────────────────────────────
    constexpr int LargeScale = 2'000'000;
    {
        std::cout << "--- [Test 3A] Million-Scale Write (Dynamic Growth - No Reserve) ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially *without* pre-allocation.\n";

        // std::vector (触发大量的重分配与内存搬运)
        {
            Timer t;
            std::vector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrBlockDeque (常数时间分块申请，无任何旧元素拷贝开销)
        {
            Timer t;
            PtrBlockDeque<int*, 1024> deque;
            for (int i = 0; i < LargeScale; ++i) {
                deque.push_back(dummy_node);
            }
            do_not_optimize(&deque);
            std::cout << "  PtrBlockDeque: " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3B：大规模顺序写入（有预分配）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 3B] Million-Scale Write (Pre-allocated - With Reserve) ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially *with* reserve().\n";

        // std::vector
        {
            Timer t;
            std::vector<int*> vec;
            vec.reserve(LargeScale);
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector: " << t.elapsed_ms() << " ms\n";
        }

        // PtrBlockDeque
        {
            Timer t;
            PtrBlockDeque<int*, 1024> deque;
            deque.reserve(LargeScale);
            for (int i = 0; i < LargeScale; ++i) {
                deque.push_back(dummy_node);
            }
            do_not_optimize(&deque);
            std::cout << "  PtrBlockDeque: " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 准备读取测试数据集
    std::vector<int*> std_large;
    PtrBlockDeque<int*, 1024> deque_large;
    std_large.reserve(LargeScale);
    deque_large.reserve(LargeScale);
    for (int i = 0; i < LargeScale; ++i) {
        std_large.push_back(reinterpret_cast<int*>(i * 8));
        deque_large.push_back(reinterpret_cast<int*>(i * 8));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：顺序遍历读取性能对比
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration (Range-Based for) ---\n";
        std::cout << "Description: Traverse " << LargeScale << " pointers and accumulate their addresses.\n";

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : std_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:               " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // B. PtrBlockDeque (标准分段指针迭代器遍历)
        {
            Timer t;
            long long sum = 0;
            for (auto* x : deque_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  PtrBlockDeque (Iterator):  " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // C. PtrBlockDeque (极限优化：双重循环遍历 —— 绕开迭代器边界分支，激活 AVX 矢量化)
        {
            Timer t;
            long long sum = 0;

            size_t total_size = deque_large.size();
            // 暴露只读二级指针
            int* const* raw_blocks = reinterpret_cast<int* const*>(deque_large.begin().current_block);
            size_t num_blocks = (total_size + 1024 - 1) / 1024;

            for (size_t b = 0; b < num_blocks; ++b) {
                int** block = reinterpret_cast<int**>(raw_blocks[b]);
                size_t elements_in_block = (b == num_blocks - 1) ? (total_size & 1023) : 1024;
                if (elements_in_block == 0 && total_size > 0) elements_in_block = 1024;

                // 🚀 内层循环 100% 连续且无条件分支，编译器会直接编译为完美的 AVX 矢量累加指令！
                for (size_t e = 0; e < elements_in_block; ++e) {
                    sum += reinterpret_cast<uintptr_t>(block[e]);
                }
            }
            do_not_optimize(sum);
            std::cout << "  PtrBlockDeque (DoubleLoop): " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：高频极速清空（C++23 特化免析构批量释放 + 无锁 Thread Cache）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 5] Bulk Clear Performance (Specialized Path & Thread Cache) ---\n";
        std::cout << "Description: Create container of size 10,000, call clear() 1,500 times with reserve.\n";

        // A. std::vector
        {
            Timer t;
            for (int i = 0; i < 1500; ++i) {
                std::vector<int*> vec;
                vec.reserve(10000);
                for (int j = 0; j < 10000; ++j) vec.push_back(nullptr);
                vec.clear();
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. PtrBlockDeque
        {
            Timer t;
            for (int i = 0; i < 1500; ++i) {
                PtrBlockDeque<int*, 1024> deque;
                deque.reserve(10000); // 🚀 同样进行预分配
                for (int j = 0; j < 10000; ++j) deque.push_back(nullptr);
                deque.clear();        // 触发 C++23 编译期特化免析构批量释放逻辑 + 物理块 Cache 拦截
                do_not_optimize(&deque);
            }
            std::cout << "  PtrBlockDeque: " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}