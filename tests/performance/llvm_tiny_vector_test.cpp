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

// 导入 LLVM 官方 ADT 指针特化容器头文件
#include <llvm/ADT/TinyPtrVector.h>

// 导入我们定制的 C++23 Ptr 特化版 TinyVector
#include "tiny_vector.hpp"

using namespace StuCanvas::utils;

// 🚀 将基准测试数据集规模定义为全局编译期常量，彻底规避 IDE 作用域符号解析偏差
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
    std::cout << " std::vector vs llvm::TinyPtrVector vs TinyVector \n";
    std::cout << "       (Specialized for Pointer Types: int*)        \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：物理静态开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::vector<int*>):       " << sizeof(std::vector<int*>) << " bytes\n";
    std::cout << "sizeof(llvm::TinyPtrVector<int*>): " << sizeof(llvm::TinyPtrVector<int*>) << " bytes\n";
    std::cout << "sizeof(TinyVector<int*>):        " << sizeof(TinyVector<int*>) << " bytes (Our Specialized version)\n";
    std::cout << "----------------------------------------------------\n\n";

    int* dummy_node1 = reinterpret_cast<int*>(0x100000);
    int* dummy_node2 = reinterpret_cast<int*>(0x200000);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2A：单指针高频写入与销毁（极轻量 CAD 连线场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2A] Small Scale: Push 1 Pointer & Destroy (1,000,000 times) ---\n";
        std::cout << "Description: Simulates single-parent relationships (Tag 0, Pure Stack-Storage).\n";

        // std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<int*> vec;
                vec.push_back(dummy_node1);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector:          " << t.elapsed_ms() << " ms\n";
        }

        // llvm::TinyPtrVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                llvm::TinyPtrVector<int*> vec;
                vec.push_back(dummy_node1);
                do_not_optimize(vec.data());
            }
            std::cout << "  llvm::TinyPtrVector:  " << t.elapsed_ms() << " ms\n";
        }

        // Our TinyVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                TinyVector<int*> vec;
                vec.push_back(dummy_node1);
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector (Ours):    " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2B：双指针高频写入与销毁（多依赖节点关系场景）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2B] Small Scale: Push 2 Pointers & Destroy (1,000,000 times) ---\n";
        std::cout << "Description: Simulates dual-parent relationships (Tag 1 + Memory Allocation).\n";

        // std::vector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                std::vector<int*> vec;
                vec.push_back(dummy_node1);
                vec.push_back(dummy_node2);
                do_not_optimize(vec.data());
            }
            std::cout << "  std::vector:          " << t.elapsed_ms() << " ms\n";
        }

        // llvm::TinyPtrVector
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                llvm::TinyPtrVector<int*> vec;
                vec.push_back(dummy_node1);
                vec.push_back(dummy_node2);
                do_not_optimize(vec.data());
            }
            std::cout << "  llvm::TinyPtrVector:  " << t.elapsed_ms() << " ms\n";
        }

        // Our TinyVector (结合了无锁 Thread-Local 物理块复用池)
        {
            Timer t;
            for (int i = 0; i < 1'000'000; ++i) {
                TinyVector<int*> vec;
                vec.push_back(dummy_node1);
                vec.push_back(dummy_node2);
                do_not_optimize(vec.data());
            }
            std::cout << "  TinyVector (Ours):    " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：大规模顺序写（扩容与热路径指令效率测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 3] Million-Scale Sequential Write ---\n";
        std::cout << "Description: Push " << LargeScale << " pointers sequentially.\n";

        // std::vector
        {
            Timer t;
            std::vector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node1);
            }
            do_not_optimize(vec.data());
            std::cout << "  std::vector:          " << t.elapsed_ms() << " ms\n";
        }

        // llvm::TinyPtrVector
        {
            Timer t;
            llvm::TinyPtrVector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node1);
            }
            do_not_optimize(vec.data());
            std::cout << "  llvm::TinyPtrVector:  " << t.elapsed_ms() << " ms\n";
        }

        // Our TinyVector
        {
            Timer t;
            TinyVector<int*> vec;
            for (int i = 0; i < LargeScale; ++i) {
                vec.push_back(dummy_node1);
            }
            do_not_optimize(vec.data());
            std::cout << "  TinyVector (Ours):    " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // 准备读取测试数据集
    std::vector<int*> std_large;
    llvm::TinyPtrVector<int*> llvm_large;
    TinyVector<int*> ptr_large;
    std_large.reserve(LargeScale);
    ptr_large.reserve(LargeScale);
    for (int i = 0; i < LargeScale; ++i) {
        int* val = reinterpret_cast<int*>(i * 8);
        std_large.push_back(val);
        llvm_large.push_back(val);
        ptr_large.push_back(val);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：遍历读取速度（L1 缓存边界与 CPU 向量化带宽测试）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 4] Sequential Read / Iteration (10,000,000 elements) ---\n";

        // A. std::vector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : std_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  std::vector:          " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // B. llvm::TinyPtrVector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : llvm_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  llvm::TinyPtrVector:  " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }

        // C. Our TinyVector
        {
            Timer t;
            long long sum = 0;
            for (auto* x : ptr_large) {
                sum += reinterpret_cast<uintptr_t>(x);
            }
            do_not_optimize(sum);
            std::cout << "  TinyVector (Ours):    " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
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
            std::cout << "  std::vector:          " << t.elapsed_ms() << " ms\n";
        }

        // llvm::TinyPtrVector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += reinterpret_cast<uintptr_t>(llvm_large[idx]);
            }
            do_not_optimize(sum);
            std::cout << "  llvm::TinyPtrVector:  " << t.elapsed_ms() << " ms\n";
        }

        // Our TinyVector
        {
            Timer t;
            long long sum = 0;
            for (int idx : random_indices) {
                sum += reinterpret_cast<uintptr_t>(ptr_large[idx]);
            }
            do_not_optimize(sum);
            std::cout << "  TinyVector (Ours):    " << t.elapsed_ms() << " ms\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}