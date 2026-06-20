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
#include <iomanip>
#include <variant>
#include <algorithm>
#include <cstring>
#include <cstdlib>

// 🚀 核心：直接 include 之前在 utils 下创建的极致分配器头文件
#include "../stucanvas/utils/flex_buffer.hpp"

// 防止编译器将未使用的读取操作优化掉
template <typename T>
void do_not_optimize(T&& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(val) : "memory");
#else
    volatile T dummy = val;
#endif
}

// =============================================================================
// 测试用基础资产类型（保持纯净，无任何 VTable 或数据头污染）
// =============================================================================
struct AssetA { double x, y; };                       // 16 字节
struct AssetB { double data[4]; };                    // 32 字节
struct AssetC { double data[8]; };                    // 64 字节

// =============================================================================
// C 风格 pNext 指针链所需要的 Raw 包装结构体（用于 方案 B 对比）
// =============================================================================
struct SObjectAssetHeader {
    uint32_t type;
    SObjectAssetHeader* pNext;
};

struct RawMaterialAsset {
    SObjectAssetHeader header;
    AssetA payload;
};

struct RawPhysicsAsset {
    SObjectAssetHeader header;
    AssetB payload;
};

struct RawMetadataAsset {
    SObjectAssetHeader header;
    AssetC payload;
};

// =============================================================================
// 扁平二分映射表 FlatMap 极简实现（用于 方案 C 对比）
// =============================================================================
template <typename Key, typename Value>
class FlatMap {
private:
    struct Entry {
        Key key;
        Value value;
        bool operator<(const Entry& other) const noexcept { return key < other.key; }
    };
    std::vector<Entry> m_data;

public:
    void reserve(size_t cap) { m_data.reserve(cap); }

    void push_back_unsorted(const Key& key, const Value& val) {
        m_data.push_back({key, val});
    }

    void sort_map() {
        std::sort(m_data.begin(), m_data.end());
    }

    // 🚀 手写极简高性能二分查找，编译后无任何标准库模板开销
    [[nodiscard]] inline const Value* find(const Key& key) const noexcept {
        int low = 0;
        int high = static_cast<int>(m_data.size()) - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            const auto& entry = m_data[mid];
            if (entry.key == key) return &entry.value;
            if (entry.key < key) low = mid + 1;
            else high = mid - 1;
        }
        return nullptr;
    }
};

// =============================================================================
// 五种不同的宿主结构体
// =============================================================================

// 方案 A：自研弹性数据打包器 (物理连续，高能 8 字节宽度) [1.2.7]
struct SObjectFlex {
    uint32_t id;
    StuCanvas::utils::FlexBuffer assets;
};

// 方案 B：C 风格 pNext 指针链 (每个资产独立 new，零虚表，但存在指针跳转) [1.2.5]
struct SObjectpNext {
    uint32_t id;
    SObjectAssetHeader* assets_head = nullptr;
};

// 方案 C：FlatMap 侧边存储 (宿主不占任何字节，资产全在外部独立 FlatMap 中) [1.2.1, 1.2.6]
struct SObjectFlatMap {
    uint32_t id;
};

// 方案 D：标准库 std::variant (静态多态联合体，值类型，无堆二次分配)
using VarAsset = std::variant<AssetA, AssetB, AssetC>;
struct SObjectVariant {
    uint32_t id;
    std::vector<VarAsset> assets;
};

// 方案 E：纯 C 风格扁平内联结构体 (无任何多态，数据硬内联)
struct SObjectInline {
    uint32_t id;
    AssetA a;
    AssetB b;
    AssetC c;
};

const size_t NUM_ELEMENTS = 100'000; // 测试 10 万个对象
const int RUN_COUNT = 5;

// 定义全局侧边 FlatMap
FlatMap<const SObjectFlatMap*, AssetA> map_a;
FlatMap<const SObjectFlatMap*, AssetB> map_b;
FlatMap<const SObjectFlatMap*, AssetC> map_c;

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "             多态与异构数据存储 —— 终极五路对决 (5-Way Benchmark)              " << std::endl;
    std::cout << "             测试对象规模: " << NUM_ELEMENTS << " 个，每个节点挂载 3 个不同大小资产" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // =========================================================================
    // 1. 方案 A 的初始化 (FlexBuffer)
    // =========================================================================
    std::cout << "\n[方案 A] 正在初始化 FlexBuffer..." << std::endl;
    auto start_alloc_a = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlex*> list_flex(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex[i] = new SObjectFlex();
        list_flex[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex[i]->assets.reserve(160);
        list_flex[i]->assets.push_back<AssetA>({static_cast<double>(i), 2.0});
        list_flex[i]->assets.push_back<AssetB>({{3.0, 4.0, 5.0, 6.0}});
        list_flex[i]->assets.push_back<AssetC>({{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
    }
    auto end_alloc_a = std::chrono::high_resolution_clock::now();
    double alloc_time_a = std::chrono::duration<double, std::milli>(end_alloc_a - start_alloc_a).count();

    // =========================================================================
    // 2. 方案 B 的初始化 (pNext 指针链)
    // =========================================================================
    std::cout << "[方案 B] 正在初始化 pNext 裸指针链 (零碎堆分配并穿插串联)..." << std::endl;
    auto start_alloc_b = std::chrono::high_resolution_clock::now();
    std::vector<SObjectpNext*> list_pnext(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_pnext[i] = new SObjectpNext();
        list_pnext[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 分别分配三个独立内存块，并用链表连接
        auto* a = static_cast<RawMaterialAsset*>(std::malloc(sizeof(RawMaterialAsset)));
        a->header.type = 0;
        a->header.pNext = list_pnext[i]->assets_head;
        a->payload = AssetA{static_cast<double>(i), 2.0};
        list_pnext[i]->assets_head = &a->header;

        auto* b = static_cast<RawPhysicsAsset*>(std::malloc(sizeof(RawPhysicsAsset)));
        b->header.type = 1;
        b->header.pNext = list_pnext[i]->assets_head;
        b->payload = AssetB{{3.0, 4.0, 5.0, 6.0}};
        list_pnext[i]->assets_head = &b->header;

        auto* c = static_cast<RawMetadataAsset*>(std::malloc(sizeof(RawMetadataAsset)));
        c->header.type = 2;
        c->header.pNext = list_pnext[i]->assets_head;
        c->payload = AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}};
        list_pnext[i]->assets_head = &c->header;
    }
    auto end_alloc_b = std::chrono::high_resolution_clock::now();
    double alloc_time_b = std::chrono::duration<double, std::milli>(end_alloc_b - start_alloc_b).count();

    // =========================================================================
    // 3. 方案 C 的初始化 (FlatMap 侧边存储)
    // =========================================================================
    std::cout << "[方案 C] 正在初始化 FlatMap 侧边存储 (宿主节点0分配，数据全进侧表)..." << std::endl;
    auto start_alloc_c = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlatMap*> list_flatmap(NUM_ELEMENTS);
    map_a.reserve(NUM_ELEMENTS);
    map_b.reserve(NUM_ELEMENTS);
    map_c.reserve(NUM_ELEMENTS);

    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flatmap[i] = new SObjectFlatMap();
        list_flatmap[i]->id = static_cast<uint32_t>(i);

        // 快速无序追加
        map_a.push_back_unsorted(list_flatmap[i], AssetA{static_cast<double>(i), 2.0});
        map_b.push_back_unsorted(list_flatmap[i], AssetB{{3.0, 4.0, 5.0, 6.0}});
        map_c.push_back_unsorted(list_flatmap[i], AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
    }
    // 一次性快速物理排序，避免 O(N^2) 插入
    map_a.sort_map();
    map_b.sort_map();
    map_c.sort_map();

    auto end_alloc_c = std::chrono::high_resolution_clock::now();
    double alloc_time_c = std::chrono::duration<double, std::milli>(end_alloc_c - start_alloc_c).count();

    // =========================================================================
    // 4. 方案 D 的初始化 (std::variant)
    // =========================================================================
    std::cout << "[方案 D] 正在初始化 std::variant (值类型，无堆二次分配)..." << std::endl;
    auto start_alloc_d = std::chrono::high_resolution_clock::now();
    std::vector<SObjectVariant*> list_variant(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_variant[i] = new SObjectVariant();
        list_variant[i]->id = static_cast<uint32_t>(i);
        list_variant[i]->assets.reserve(3);
        list_variant[i]->assets.push_back(AssetA{static_cast<double>(i), 2.0});
        list_variant[i]->assets.push_back(AssetB{{3.0, 4.0, 5.0, 6.0}});
        list_variant[i]->assets.push_back(AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
    }
    auto end_alloc_d = std::chrono::high_resolution_clock::now();
    double alloc_time_d = std::chrono::duration<double, std::milli>(end_alloc_d - start_alloc_d).count();

    // =========================================================================
    // 5. 方案 E 的初始化 (纯内联扁平结构体)
    // =========================================================================
    std::cout << "[方案 E] 正在初始化纯内联结构体 (零指针，物理连续的绝对极限)..." << std::endl;
    auto start_alloc_e = std::chrono::high_resolution_clock::now();
    std::vector<SObjectInline*> list_inline(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_inline[i] = new SObjectInline();
        list_inline[i]->id = static_cast<uint32_t>(i);
        list_inline[i]->a = AssetA{static_cast<double>(i), 2.0};
        list_inline[i]->b = AssetB{{3.0, 4.0, 5.0, 6.0}};
        list_inline[i]->c = AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}};
    }
    auto end_alloc_e = std::chrono::high_resolution_clock::now();
    double alloc_time_e = std::chrono::duration<double, std::milli>(end_alloc_e - start_alloc_e).count();


    std::cout << "\n----------------------- 初始化分配耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 A] FlexBuffer 耗时:" << std::fixed << std::setprecision(2) << alloc_time_a << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << alloc_time_b << " ms (慢了 " << (alloc_time_b/alloc_time_a) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << alloc_time_c << " ms (最快分配，因为批量排序)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 D] std::variant 耗时:" << alloc_time_d << " ms (慢了 " << (alloc_time_d/alloc_time_a) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << alloc_time_e << " ms (最快堆对象整体分配)" << std::endl;

    // =========================================================================
    // 核心性能读取对决
    // =========================================================================
    std::cout << "\n开始进行高频遍历读取测试..." << std::endl;

    // A. 方案 A 测试 (FlexBuffer)
    double read_time_a = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 物理偏置无分支一击即中，没有间接跳转
            auto* a = list_flex[i]->assets.get_unsafe<AssetA>(0);
            auto* b = list_flex[i]->assets.get_unsafe<AssetB>(1);
            auto* c = list_flex[i]->assets.get_unsafe<AssetC>(2);
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_a += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_a /= RUN_COUNT;

    // B. 方案 B 测试 (pNext 指针链)
    double read_time_b = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 传统的 pNext 指针链追逐（3次循环条件转移，3次跨堆块跳转） [1.2.5]
            SObjectAssetHeader* curr = list_pnext[i]->assets_head;
            AssetA* a = nullptr;
            AssetB* b = nullptr;
            AssetC* c = nullptr;
            while (curr != nullptr) {
                if (curr->type == 0)      a = &reinterpret_cast<RawMaterialAsset*>(curr)->payload;
                else if (curr->type == 1) b = &reinterpret_cast<RawPhysicsAsset*>(curr)->payload;
                else if (curr->type == 2) c = &reinterpret_cast<RawMetadataAsset*>(curr)->payload;
                curr = curr->pNext; // 指针追逐
            }
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_b += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_b /= RUN_COUNT;

    // C. 方案 C 测试 (FlatMap 侧存储)
    double read_time_c = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 3次外部 FlatMap 的 O(log N) 级二分查找，引起剧烈的 Cache Miss 和页表碰撞 [1.2.1, 1.2.6]
            const auto* a = map_a.find(list_flatmap[i]);
            const auto* b = map_b.find(list_flatmap[i]);
            const auto* c = map_c.find(list_flatmap[i]);
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_c += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_c /= RUN_COUNT;

    // D. 方案 D 测试 (std::variant)
    double read_time_d = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            auto* a = std::get_if<AssetA>(&list_variant[i]->assets[0]);
            auto* b = std::get_if<AssetB>(&list_variant[i]->assets[1]);
            auto* c = std::get_if<AssetC>(&list_variant[i]->assets[2]);
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_d += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_d /= RUN_COUNT;

    // E. 方案 E 测试 (纯内联扁平结构体)
    double read_time_e = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            auto* p = list_inline[i];
            sum += p->a.x + p->b.data[0] + p->c.data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_e += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_e /= RUN_COUNT;

    std::cout << "\n----------------------- 遍历读取耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 A] FlexBuffer 耗时:" << std::fixed << std::setprecision(4) << read_time_a << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << read_time_b << " ms (慢了 " << (read_time_b/read_time_a) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << read_time_c << " ms (慢了 " << (read_time_c/read_time_a) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 D] std::variant 耗时:" << read_time_d << " ms (慢了 " << (read_time_d/read_time_a) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << read_time_e << " ms (最快极限，理论上限)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 清理内存
    for (auto* p : list_flex) delete p;
    for (auto* p : list_variant) delete p;
    for (auto* p : list_inline) delete p;
    for (auto* p : list_flatmap) delete p;
    for (auto* p : list_pnext) {
        SObjectAssetHeader* curr = p->assets_head;
        while (curr != nullptr) {
            SObjectAssetHeader* next = curr->pNext;
            std::free(curr);
            curr = next;
        }
        delete p;
    }

    return 0;
}