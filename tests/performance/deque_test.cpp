/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include <iostream>
#include <vector>
#include <deque>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <random>
#include <cstdint>
#include <cassert>
#include "block_deque.hpp" // 引入物理 8B 极致对齐 BlockDeque

using namespace StuCanvas::utils;

// 全局测试规模
constexpr int LargeScale = 10'000'000;

// 防止编译器死代码消除（DCE）的平台自适应黑科技（完美支持 GCC, Clang & MSVC）
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
    std::cout << "       BlockDeque vs STL vector & deque Benchmark   \n";
    std::cout << "       (Specialized for Pointer Types: int*)        \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态栈空间开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):     " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(std::deque<int*>):      " << sizeof(std::deque<int*>) << " bytes\n";
    std::cout << "sizeof(BlockDeque<int*, 1024>): " << sizeof(BlockDeque<int*, 1024>) << " bytes (Our 8B version)\n";
    std::cout << "----------------------------------------------------\n\n";

    int* dummy_node = reinterpret_cast<int*>(0x12345678);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：核心物理特性——扩容时元素地址绝对稳定性验证（CAD DAG 的正确性底线）
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 2] Element Address Stability Verification ---\n";
    std::cout << "Description: Capture 1st element's reference, insert 10,000 more items,\n"
              << "             and check if the reference pointer changes.\n";
    {
        std::vector<int*> vec;
        vec.push_back(dummy_node);
        int** addr_std_before = &vec[0]; // 🚀 完美修正：vec[0] 为 int*，其地址为 int** 二级指针

        for (int i = 0; i < 10000; ++i) vec.push_back(dummy_node);

        int** addr_std_after = &vec[0]; // 🚀 完美修正
        std::cout << "  std::vector address:  " << addr_std_before << " -> " << addr_std_after;
        if (addr_std_before != addr_std_after) {
            std::cout << " (地址发生偏移！指针连线断开！)\n";
        } else {
            std::cout << " (地址未改变)\n";
        }
    }
    {
        BlockDeque<int*, 256> deque; // 🚀 偏特化类型实例化
        deque.push_back(dummy_node);
        int** addr_deque_before = &deque[0]; // 🚀 完美修正：deque[0] 为 int*，其地址为 int**

        for (int i = 0; i < 10000; ++i) deque.push_back(dummy_node);

        int** addr_deque_after = &deque[0]; // 🚀 完美修正
        std::cout << "  BlockDeque address:   " << addr_deque_before << " -> " << addr_deque_after;
        if (addr_deque_before == addr_deque_after) {
            std::cout << " (🚀 地址绝对稳定！指针连线依然有效！)\n";
        } else {
            std::cout << " (地址发生偏移)\n";
        }
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：大规模顺序写入（1千万级写测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially.\n";

        // A. std::vector (预分配)
        {
            Timer t;
            std::vector<int*> vec;
            vec.reserve(LargeScale);
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector (with reserve): " << t.elapsed_ms() << " ms\n";
        }

        // B. std::vector (动态扩容)
        {
            Timer t;
            std::vector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector (dynamic):      " << t.elapsed_ms() << " ms\n";
        }

        // C. std::deque (STL 双端队列，不支持 reserve)
        {
            Timer t;
            std::deque<int*> deq;
            for (int i = 0; i < LargeScale; ++i) {
                deq.push_back(dummy_node);
            }
            do_not_optimize(&deq);
            std::cout << "  std::deque (STL):           " << t.elapsed_ms() << " ms\n";
        }

        // D. BlockDeque (预分配)
        {
            Timer t;
            BlockDeque<int*, 1024> deque;
            deque.reserve(LargeScale);
            for (int i = 0; i < LargeScale; ++i) {
                deque.push_back(dummy_node);
            }
            do_not_optimize(&deque);
            std::cout << "  BlockDeque (with reserve):  " << t.elapsed_ms() << " ms\n";
        }

        // E. BlockDeque (动态分块分配)
        {
            Timer t;
            BlockDeque<int*, 1024> deque;
            for (int i = 0; i < LargeScale; ++i) {
                deque.push_back(dummy_node);
            }
            do_not_optimize(&deque);
            std::cout << "  BlockDeque (dynamic):       " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 准备读取测试数据集
    std::vector<int*> std_large;
    std::deque<int*> stl_deque_large;
    BlockDeque<int*, 1024> deque_large;

    std_large.reserve(LargeScale);
    deque_large.reserve(LargeScale);

    for (int i = 0; i < LargeScale; ++i) {
        int* val = reinterpret_cast<int*>(static_cast<uintptr_t>(i * 8));
        std_large.push_back(val);
        stl_deque_large.push_back(val);
        deque_large.push_back(val);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：遍历读取速度（缓存局部性与迭代器效率测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration (10,000,000 elements) ---\n";

        // A. std::vector (纯一维连续内存，最快)
        {
            Timer t;
            long long sum = 0;
            for (auto* x : std_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  std::vector (range-for):     " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // B. std::deque (STL 分块结构，迭代器跳转极其繁重)
        {
            Timer t;
            long long sum = 0;
            for (auto* x : stl_deque_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  std::deque (range-for):      " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // C. BlockDeque (我们精心设计的分段指针迭代器遍历)
        {
            Timer t;
            long long sum = 0;
            for (auto* x : deque_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  BlockDeque (Iterator):       " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // D. BlockDeque (双重循环遍历 —— 绕开迭代器自增分支，100% 激活 AVX 向量化)
        {
            Timer t;
            long long sum = 0;

            size_t total_size = deque_large.size();
            // 🚀 物理对齐：m_blocks_exposed() 完美接收三级裸指针 int* const* const*
            int* const* const* raw_blocks = deque_large.m_blocks_exposed();
            size_t num_blocks = (total_size + 1024 - 1) / 1024;

            for (size_t b = 0; b < num_blocks; ++b) {
                // 🚀 终极修正：直接顺应常量指针规则使用 int* const* 指针接收，完全消除了 reinterpret_cast 强转 const 失败的语法冲突
                int* const* block = raw_blocks[b];
                size_t elements_in_block = (b == num_blocks - 1) ? (total_size & 1023) : 1024;
                if (elements_in_block == 0 && total_size > 0) elements_in_block = 1024;

                for (size_t e = 0; e < elements_in_block; ++e) {
                    sum += reinterpret_cast<uintptr_t>(block[e]);
                }
            }
            do_not_optimize(sum);
            std::cout << "  BlockDeque (DoubleLoop):     " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：高频批量清空
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 5] Bulk Clear Performance (Specialized Path & Static Cache) ---\n";
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

        // B. std::deque
        {
            Timer t;
            for (int i = 0; i < 1500; ++i) {
                std::deque<int*> deq;
                for (int j = 0; j < 10000; ++j) deq.push_back(nullptr);
                deq.clear();
                do_not_optimize(&deq);
            }
            std::cout << "  std::deque:   " << t.elapsed_ms() << " ms\n";
        }

        // C. BlockDeque
        {
            Timer t;
            for (int i = 0; i < 1500; ++i) {
                BlockDeque<int*, 1024> deque;
                deque.reserve(10000);
                for (int j = 0; j < 10000; ++j) deque.push_back(nullptr);
                deque.clear();
                do_not_optimize(&deque);
            }
            std::cout << "  BlockDeque:   " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}
