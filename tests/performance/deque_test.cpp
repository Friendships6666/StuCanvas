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
#include "block_deque.hpp" // 引入我们重写的极致 8 字节 BlockDeque

using namespace StuCanvas::utils;

// 阻止编译器死代码消除（DCE）的平台自适应黑科技
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
    std::cout << "       BlockDeque vs std::vector Performance Test   \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态体积检查
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):  " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(BlockDeque<int*>):   " << sizeof(BlockDeque<int*>) << " bytes\n";
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：核心物理特性——扩容时元素地址绝对稳定性验证
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 2] Element Address Stability Verification ---\n";
    std::cout << "Description: Capture 1st element's reference, insert 10,000 more items,\n"
              << "             and check if the reference pointer changes.\n";
    {
        std::vector<int> vec;
        vec.push_back(100);
        int* addr_std_before = &vec[0];

        // 持续 push_back 迫使其多次触发内存重分配与元素拷贝
        for (int i = 0; i < 10000; ++i) vec.push_back(i);

        int* addr_std_after = &vec[0];
        std::cout << "  std::vector address:  " << addr_std_before << " -> " << addr_std_after;
        if (addr_std_before != addr_std_after) {
            std::cout << " (地址发生偏移！指针断开连线！)\n";
        } else {
            std::cout << " (地址未改变)\n";
        }
    }
    {
        BlockDeque<int, 256> deque;
        deque.push_back(100);
        int* addr_deque_before = &deque[0];

        // 持续 push_back 迫使其分配新分块
        for (int i = 0; i < 10000; ++i) deque.push_back(i);

        int* addr_deque_after = &deque[0];
        std::cout << "  BlockDeque address:   " << addr_deque_before << " -> " << addr_deque_after;
        if (addr_deque_before == addr_deque_after) {
            std::cout << " (🚀 地址绝对稳定！指针连线依然有效！)\n";
        } else {
            std::cout << " (地址发生偏移)\n";
        }
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：大规模顺序写入能力对比（写性能测试）
    // ─────────────────────────────────────────────────────────────────────────
    constexpr int LargeScale = 1'000'000;
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially.\n";

        // A. std::vector
        {
            Timer t;
            std::vector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(nullptr);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector:  " << t.elapsed_ms() << " ms\n";
        }

        // B. BlockDeque
        {
            Timer t;
            BlockDeque<int*, 1024> deque;
            for (int i = 0; i < LargeScale; ++i) {
                deque.push_back(nullptr);
            }
            do_not_optimize(&deque);
            std::cout << "  BlockDeque:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：顺序遍历读取能力对比（读性能与高速缓存友好度测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration ---\n";
        std::cout << "Description: Traverse " << LargeScale << " elements using range-based for.\n";

        std::vector<int> std_large(LargeScale);
        std::iota(std_large.begin(), std_large.end(), 0);

        BlockDeque<int, 1024> deque_large;
        for (int i = 0; i < LargeScale; ++i) deque_large.push_back(i);

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

        // B. BlockDeque
        {
            Timer t;
            long long sum = 0;
            for (auto x : deque_large) {
                sum += x;
            }
            do_not_optimize(sum);
            std::cout << "  BlockDeque:   " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：高频极速清空对比（C++23 特化优化测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 5] Bulk Clear Performance (C++23 Specialized Path) ---\n";
        std::cout << "Description: Create container of size 10,000, call clear() 1,500 times.\n";

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

        // B. BlockDeque
        {
            Timer t;
            for (int i = 0; i < 1500; ++i) {
                BlockDeque<int*, 1024> deque;
                deque.reserve(10000); // 🚀 同样进行预分配，确保对比完全公平！
                for (int j = 0; j < 10000; ++j) deque.push_back(nullptr);
                deque.clear(); // 触发 C++23 编译期特化免析构批量释放逻辑
                do_not_optimize(&deque);
            }
            std::cout << "  BlockDeque:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}