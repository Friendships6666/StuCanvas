/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <random>
#include <vector>
#include <iomanip>
#include <cstdint>
#include <numeric>

// 🚀 导入 Boost 官方最强 flat_map 头文件（无需链接任何库，纯 Header-only）
#include <boost/unordered/unordered_flat_map.hpp>

// 导入我们定制的 C++23 Ptr 哨兵特化版 FlatMap
#include "flat_map.hpp"

using namespace StuCanvas::utils;

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
    std::cout << " std::unordered_map vs boost::unordered_flat_map vs FlatMap\n";
    std::cout << "       (Specialized for Pointer Keys: int* -> uint64_t)\n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态栈空间开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::unordered_map):       " << sizeof(std::unordered_map<int*, uint64_t>) << " bytes\n";
    std::cout << "sizeof(boost::unordered_flat_map): " << sizeof(boost::unordered_flat_map<int*, uint64_t>) << " bytes\n";
    std::cout << "sizeof(FlatMap):                  " << sizeof(FlatMap<int*, uint64_t>) << " bytes (Our 8B version)\n";
    std::cout << "----------------------------------------------------\n\n";

    constexpr size_t NumElements = 300'000;
    std::mt19937 rng(1337);

    // 预生成测试数据集
    std::vector<int*> insert_keys(NumElements);
    for (size_t i = 0; i < NumElements; ++i) {
        insert_keys[i] = reinterpret_cast<int*>(0x10000000 + i * 16);
    }
    std::shuffle(insert_keys.begin(), insert_keys.end(), rng);

    // 混合 50% 成功查找与 50% 失败查找的随机检索数据集
    std::vector<int*> lookup_keys;
    lookup_keys.reserve(1'000'000);
    for (size_t i = 0; i < 500'000; ++i) {
        lookup_keys.push_back(insert_keys[i % NumElements]);
    }
    for (size_t i = 0; i < 500'000; ++i) {
        lookup_keys.push_back(reinterpret_cast<int*>(0x20000000 + i * 16));
    }
    std::shuffle(lookup_keys.begin(), lookup_keys.end(), rng);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：高频顺序/随机写入能力对比（Insert）
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 2] Million-Scale Sequential Insert ---\n";

    // A. std::unordered_map
    std::unordered_map<int*, uint64_t> std_map;
    {
        Timer t;
        for (size_t i = 0; i < NumElements; ++i) {
            std_map[insert_keys[i]] = i;
        }
        std::cout << "  std::unordered_map:        " << t.elapsed_ms() << " ms\n";
    }

    // B. boost::unordered_flat_map
    boost::unordered_flat_map<int*, uint64_t> boost_map;
    {
        Timer t;
        for (size_t i = 0; i < NumElements; ++i) {
            boost_map[insert_keys[i]] = i;
        }
        std::cout << "  boost::unordered_flat_map: " << t.elapsed_ms() << " ms\n";
    }

    // C. Our FlatMap
    FlatMap<int*, uint64_t> flat_map;
    {
        Timer t;
        for (size_t i = 0; i < NumElements; ++i) {
            flat_map.insert(insert_keys[i], i);
        }
        std::cout << "  FlatMap (Ours):            " << t.elapsed_ms() << " ms\n";
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：高频混合查找能力对比（Lookup - 50% Hits / 50% Misses）
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 3] High-Frequency Lookup (1,000,000 times) ---\n";

    // A. std::unordered_map
    {
        Timer t;
        long long sum = 0;
        for (auto* k : lookup_keys) {
            auto it = std_map.find(k);
            if (it != std_map.end()) {
                sum += it->second;
            }
        }
        do_not_optimize(sum);
        std::cout << "  std::unordered_map:        " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
    }

    // B. boost::unordered_flat_map
    {
        Timer t;
        long long sum = 0;
        for (auto* k : lookup_keys) {
            auto it = boost_map.find(k);
            if (it != boost_map.end()) {
                sum += it->second;
            }
        }
        do_not_optimize(sum);
        std::cout << "  boost::unordered_flat_map: " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
    }

    // C. Our FlatMap
    {
        Timer t;
        long long sum = 0;
        for (auto* k : lookup_keys) {
            auto it = flat_map.find(k);
            if (it != flat_map.end()) {
                sum += it->second;
            }
        }
        do_not_optimize(sum);
        std::cout << "  FlatMap (Ours):            " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 4：迭代器顺序遍历对比（Traverse）
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 4] Sequential Read / Iteration ---\n";

    // A. std::unordered_map
    {
        Timer t;
        long long sum = 0;
        for (const auto& pair : std_map) {
            sum += pair.second;
        }
        do_not_optimize(sum);
        std::cout << "  std::unordered_map:        " << t.elapsed_ms() << " ms\n";
    }

    // B. boost::unordered_flat_map
    {
        Timer t;
        long long sum = 0;
        for (const auto& pair : boost_map) {
            sum += pair.second;
        }
        do_not_optimize(sum);
        std::cout << "  boost::unordered_flat_map: " << t.elapsed_ms() << " ms\n";
    }

    // C. Our FlatMap
    {
        Timer t;
        long long sum = 0;
        for (const auto& entry : flat_map) {
            sum += entry.second;
        }
        do_not_optimize(sum);
        std::cout << "  FlatMap (Ours):            " << t.elapsed_ms() << " ms\n";
    }
    std::cout << "----------------------------------------------------\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 5：批量擦除性能对比（Erase 100,000 elements）
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 5] Bulk Erase Performance ---\n";
    std::vector<int*> erase_keys(insert_keys.begin(), insert_keys.begin() + 100'000);

    // A. std::unordered_map
    {
        Timer t;
        size_t erased = 0;
        for (auto* k : erase_keys) {
            if (std_map.erase(k)) erased++;
        }
        do_not_optimize(erased);
        std::cout << "  std::unordered_map:        " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
    }

    // B. boost::unordered_flat_map
    {
        Timer t;
        size_t erased = 0;
        for (auto* k : erase_keys) {
            if (boost_map.erase(k)) erased++;
        }
        do_not_optimize(erased);
        std::cout << "  boost::unordered_flat_map: " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
    }

    // C. Our FlatMap
    {
        Timer t;
        size_t erased = 0;
        for (auto* k : erase_keys) {
            if (flat_map.erase(k)) erased++;
        }
        do_not_optimize(erased);
        std::cout << "  FlatMap (Ours):            " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
    }
    std::cout << "----------------------------------------------------\n\n";

    return 0;
}