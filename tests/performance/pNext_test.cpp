// tests/performance/benchmark_pnext_vs_flatmap.cpp
#pragma once

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <algorithm>
#include <string>
#include <iomanip>

// 💡 优化屏障：阻止编译器在编译期对循环进行优化折叠和常量预测
#if defined(__GNUC__) || defined(__clang__)
    #define PREVENT_OPTIMIZE(var) asm volatile("" : "+r"(var))
#else
    #define PREVENT_OPTIMIZE(var) (void)reinterpret_cast<volatile char*>(&var)
#endif

// =========================================================================
// 1. 模拟的高性能 FlatMap 实现（基于连续内存的二分查找）
// =========================================================================
template <typename K, typename V>
class FlatMap {
public:
    std::vector<std::pair<K, V>> data;

    void insert(const K& k, const V& v) {
        data.push_back({k, v});
    }

    void sort_map() {
        std::sort(data.begin(), data.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
    }

    // O(log M) 二分查找
    [[nodiscard]] const V* find(const K& k) const noexcept {
        auto it = std::lower_bound(data.begin(), data.end(), k, [](const auto& pair, const K& key) {
            return pair.first < key;
        });
        if (it != data.end() && it->first == k) [[likely]] {
            return &(it->second);
        }
        return nullptr;
    }
};

// =========================================================================
// 2. 模拟的 pNext 属性链系统（每个节点扩大到 256 字节）
// =========================================================================
enum class AttrType : uint32_t {
    TYPE_0, TYPE_1, TYPE_2, TYPE_3, TYPE_4, TYPE_5, TYPE_6, TYPE_7,
    TYPE_8, TYPE_9, TYPE_10, TYPE_11, TYPE_12, TYPE_13, TYPE_14, TYPE_15,
    TYPE_16, TYPE_17, TYPE_18, TYPE_19, TYPE_20, TYPE_21, TYPE_22, TYPE_23,
    TYPE_24, TYPE_25, TYPE_26, TYPE_27, TYPE_28, TYPE_29, TYPE_30, TYPE_31
};

struct SObjectAttribute {
    AttrType type;
    const SObjectAttribute* next = nullptr; // Vulkan pNext 指针
};

// 💡 具体的外挂属性结构体（重型对象）
template <size_t Idx>
struct ConcreteAttribute : public SObjectAttribute {
    ConcreteAttribute() {
        this->type = static_cast<AttrType>(Idx);
        // 初始化重型数据包
        std::ranges::fill(payload, static_cast<float>(Idx));
    }

    // 💡 增大挂载数据：64 个 float = 256 字节（跨越整整 4 个 CPU Cache Lines）
    // 模拟真实的 OpenPBR 工业级表面/物理材质大包
    float payload[4];
};

struct SObject {
    uint64_t id;
    const SObjectAttribute* attributes = nullptr; // 链表头
};

// 释放链表内存的辅助函数
void free_attribute_chain(const SObjectAttribute* head) {
    while (head) {
        const SObjectAttribute* next = head->next;
        delete head;
        head = next;
    }
}

// =========================================================================
// 3. 测试运行内核
// =========================================================================
void run_comparison_for_chain_length(size_t chain_length) {
    constexpr size_t M = 10000;

    std::vector<SObject> nodes(M);
    FlatMap<const SObject*, const SObjectAttribute*> flat_map;

    const AttrType target_type = static_cast<AttrType>(chain_length - 1);

    // 1. 构建测试数据
    for (size_t i = 0; i < M; ++i) {
        nodes[i].id = i;

        SObjectAttribute* head = nullptr;
        SObjectAttribute* current = nullptr;

        for (size_t l = 0; l < chain_length; ++l) {
            SObjectAttribute* attr = nullptr;
            if (l == 0) attr = new ConcreteAttribute<0>();
            else if (l == 1) attr = new ConcreteAttribute<1>();
            else if (l == 2) attr = new ConcreteAttribute<2>();
            else if (l == 3) attr = new ConcreteAttribute<3>();
            else if (l == 4) attr = new ConcreteAttribute<4>();
            else if (l == 5) attr = new ConcreteAttribute<5>();
            else if (l == 6) attr = new ConcreteAttribute<6>();
            else if (l == 7) attr = new ConcreteAttribute<7>();
            else if (l == 8) attr = new ConcreteAttribute<8>();
            else if (l == 9) attr = new ConcreteAttribute<9>();
            else if (l == 10) attr = new ConcreteAttribute<10>();
            else if (l == 11) attr = new ConcreteAttribute<11>();
            else if (l == 12) attr = new ConcreteAttribute<12>();
            else if (l == 13) attr = new ConcreteAttribute<13>();
            else if (l == 14) attr = new ConcreteAttribute<14>();
            else if (l == 15) attr = new ConcreteAttribute<15>();
            else attr = new SObjectAttribute{static_cast<AttrType>(l), nullptr};

            if (!head) {
                head = attr;
                current = attr;
            } else {
                current->next = attr;
                current = attr;
            }

            if (static_cast<size_t>(attr->type) == static_cast<size_t>(target_type)) {
                flat_map.insert(&nodes[i], attr);
            }
        }
        nodes[i].attributes = head;
    }

    flat_map.sort_map();

    // 2. 吞吐量基准测试
    std::cout << "Chain Length: " << std::setw(2) << chain_length << " | ";

    // --- 方案 A：FlatMap 二分查找 ($O(\log M)$) ---
    {
        auto start = std::chrono::high_resolution_clock::now();
        uint64_t sum = 0;
        for (size_t i = 0; i < M; ++i) {
            const SObject* key = &nodes[i];
            PREVENT_OPTIMIZE(key);

            const SObjectAttribute* attr = *flat_map.find(key);
            sum += static_cast<uint32_t>(attr->type);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        std::cout << "FlatMap: " << std::setw(6) << (duration / M) << " ns/op | ";
        volatile uint64_t sink = sum; (void)sink;
    }

    // --- 方案 B：Vulkan pNext 链表顺序遍历 ($O(L)$) ---
    {
        auto start = std::chrono::high_resolution_clock::now();
        uint64_t sum = 0;
        for (size_t i = 0; i < M; ++i) {
            const SObjectAttribute* curr = nodes[i].attributes;
            const SObjectAttribute* target_attr = nullptr;

            while (curr) {
                PREVENT_OPTIMIZE(curr);
                if (curr->type == target_type) [[likely]] {
                    target_attr = curr;
                    break;
                }
                curr = curr->next;
            }
            if (target_attr) {
                sum += static_cast<uint32_t>(target_attr->type);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        std::cout << "pNext Chain: " << std::setw(6) << (duration / M) << " ns/op\n";
        volatile uint64_t sink = sum; (void)sink;
    }

    // 内存清理
    for (size_t i = 0; i < M; ++i) {
        free_attribute_chain(nodes[i].attributes);
    }
}

int main() {
    std::cout << "Starting heavy pNext vs FlatMap (Size: 10,000, Node: 256 bytes) Benchmark...\n";
    std::cout << "Finding the LAST element of the chain (Worst Case).\n\n";

    run_comparison_for_chain_length(1);
    run_comparison_for_chain_length(2);
    run_comparison_for_chain_length(3);
    run_comparison_for_chain_length(4);
    run_comparison_for_chain_length(6);
    run_comparison_for_chain_length(8);
    run_comparison_for_chain_length(12);
    run_comparison_for_chain_length(16);
    run_comparison_for_chain_length(24);
    run_comparison_for_chain_length(32);

    return 0;
}