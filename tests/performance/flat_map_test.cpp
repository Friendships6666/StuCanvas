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
#include "flat_map.hpp" // 引入我们重写的极致 8 字节 FlatMap

using namespace StuCanvas::utils;

// 阻止编译器死代码消除（DCE）的平台自适应黑科技（完美支持 GCC, Clang & MSVC）
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
    std::cout << "          FlatMap vs std::unordered_map             \n";
    std::cout << "                Performance Benchmark               \n";
    std::cout << "====================================================\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 1：类静态栈空间开销对比
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "--- [Test 1] Memory Footprint (Stack Size) ---\n";
    std::cout << "sizeof(std::unordered_map<int*, uint64_t>): " << sizeof(std::unordered_map<int*, uint64_t>) << " bytes\n";
    std::cout << "sizeof(FlatMap<int*, uint64_t>):            " << sizeof(FlatMap<int*, uint64_t>) << " bytes (Pointer Specialization)\n";
    std::cout << "sizeof(FlatMap<uint64_t, double>):          " << sizeof(FlatMap<uint64_t, double>) << " bytes (Generic Template)\n";
    std::cout << "----------------------------------------------------\n\n";

    constexpr size_t NumElements = 300'000;
    std::mt19937 rng(1337);

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 2：指针类型键对比（激活 C++23 Pointer-Key 特化，哨兵零堆空洞分支）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 2] Pointer Keys (int* -> uint64_t) ---\n";
        std::cout << "Description: Active Pointer-Key Specialization (Sentinel-based Tagging, 0-State Field).\n";

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

        // A. std::unordered_map 写入与查找
        std::unordered_map<int*, uint64_t> std_map;
        {
            Timer t;
            for (size_t i = 0; i < NumElements; ++i) {
                std_map[insert_keys[i]] = i;
            }
            std::cout << "  std::unordered_map Insert: " << t.elapsed_ms() << " ms\n";
        }
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
            std::cout << "  std::unordered_map Lookup: " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        {
            Timer t;
            long long sum = 0;
            for (const auto& pair : std_map) {
                sum += pair.second;
            }
            do_not_optimize(sum);
            std::cout << "  std::unordered_map Iter:   " << t.elapsed_ms() << " ms\n";
        }

        // B. FlatMap 指针特化版写入与查找
        FlatMap<int*, uint64_t> flat_map;
        {
            Timer t;
            for (size_t i = 0; i < NumElements; ++i) {
                flat_map.insert(insert_keys[i], i);
            }
            std::cout << "  FlatMap (Specialized) Insert: " << t.elapsed_ms() << " ms\n";
        }
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
            std::cout << "  FlatMap (Specialized) Lookup: " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        {
            Timer t;
            long long sum = 0;
            for (const auto& entry : flat_map) {
                sum += entry.second;
            }
            do_not_optimize(sum);
            std::cout << "  FlatMap (Specialized) Iter:   " << t.elapsed_ms() << " ms\n";
        }

        // C. 擦除性能对比
        std::vector<int*> erase_keys(insert_keys.begin(), insert_keys.begin() + 100'000);
        {
            Timer t;
            size_t erased = 0;
            for (auto* k : erase_keys) {
                if (std_map.erase(k)) erased++;
            }
            do_not_optimize(erased);
            std::cout << "  std::unordered_map Erase:  " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
        }
        {
            Timer t;
            size_t erased = 0;
            for (auto* k : erase_keys) {
                if (flat_map.erase(k)) erased++;
            }
            do_not_optimize(erased);
            std::cout << "  FlatMap (Specialized) Erase: " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 测试 3：普通 64 位整数类型键对比（激活普通模板，保留 State 标记，扁平 Cache 优化）
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::cout << "--- [Test 3] Integer Keys (uint64_t -> double) ---\n";
        std::cout << "Description: Actives Generic Template (Has 'State' Field, Flat Open-Addressing).\n";

        std::vector<uint64_t> insert_keys(NumElements);
        for (size_t i = 0; i < NumElements; ++i) {
            insert_keys[i] = i * 97 + 13; // 随机非连续整数键
        }
        std::shuffle(insert_keys.begin(), insert_keys.end(), rng);

        std::vector<uint64_t> lookup_keys;
        lookup_keys.reserve(1'000'000);
        for (size_t i = 0; i < 500'000; ++i) {
            lookup_keys.push_back(insert_keys[i % NumElements]);
        }
        for (size_t i = 0; i < 500'000; ++i) {
            lookup_keys.push_back(i * 103 + 99999999);
        }
        std::shuffle(lookup_keys.begin(), lookup_keys.end(), rng);

        // A. std::unordered_map
        std::unordered_map<uint64_t, double> std_map;
        {
            Timer t;
            for (size_t i = 0; i < NumElements; ++i) {
                std_map[insert_keys[i]] = static_cast<double>(i);
            }
            std::cout << "  std::unordered_map Insert: " << t.elapsed_ms() << " ms\n";
        }
        {
            Timer t;
            double sum = 0;
            for (auto k : lookup_keys) {
                auto it = std_map.find(k);
                if (it != std_map.end()) {
                    sum += it->second;
                }
            }
            do_not_optimize(sum);
            std::cout << "  std::unordered_map Lookup: " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        {
            Timer t;
            double sum = 0;
            for (const auto& pair : std_map) {
                sum += pair.second;
            }
            do_not_optimize(sum);
            std::cout << "  std::unordered_map Iter:   " << t.elapsed_ms() << " ms\n";
        }

        // B. FlatMap 通用版
        FlatMap<uint64_t, double> flat_map;
        {
            Timer t;
            for (size_t i = 0; i < NumElements; ++i) {
                flat_map.insert(insert_keys[i], static_cast<double>(i));
            }
            std::cout << "  FlatMap (Generic) Insert:    " << t.elapsed_ms() << " ms\n";
        }
        {
            Timer t;
            double sum = 0;
            for (auto k : lookup_keys) {
                auto it = flat_map.find(k);
                if (it != flat_map.end()) {
                    sum += it->second;
                }
            }
            do_not_optimize(sum);
            std::cout << "  FlatMap (Generic) Lookup:    " << t.elapsed_ms() << " ms (sum=" << sum << ")\n";
        }
        {
            Timer t;
            double sum = 0;
            for (const auto& entry : flat_map) {
                sum += entry.second;
            }
            do_not_optimize(sum);
            std::cout << "  FlatMap (Generic) Iter:      " << t.elapsed_ms() << " ms\n";
        }

        // C. 擦除对比
        std::vector<uint64_t> erase_keys(insert_keys.begin(), insert_keys.begin() + 100'000);
        {
            Timer t;
            size_t erased = 0;
            for (auto k : erase_keys) {
                if (std_map.erase(k)) erased++;
            }
            do_not_optimize(erased);
            std::cout << "  std::unordered_map Erase:  " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
        }
        {
            Timer t;
            size_t erased = 0;
            for (auto k : erase_keys) {
                if (flat_map.erase(k)) erased++;
            }
            do_not_optimize(erased);
            std::cout << "  FlatMap (Generic) Erase:     " << t.elapsed_ms() << " ms (erased=" << erased << ")\n";
        }
        std::cout << "----------------------------------------------------\n\n";
    }

    return 0;
}