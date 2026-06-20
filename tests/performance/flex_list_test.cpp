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

// include 物理头文件
#include "../stucanvas/utils/flex_buffer.hpp"
#include "../stucanvas/utils/flex_list.hpp"

template <typename T>
void do_not_optimize(T&& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(val) : "memory");
#else
    volatile T dummy = val;
#endif
}

// 物理资产
struct AssetA { double x, y; };
struct AssetB { double data[4]; };
struct AssetC { double data[8]; };

// C 风格 pNext 链表
struct SObjectAssetHeader {
    uint32_t id;
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

// 侧表 FlatMap
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
    void push_back_unsorted(const Key& key, const Value& val) { m_data.push_back({key, val}); }
    void sort_map() { std::sort(m_data.begin(), m_data.end()); }

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

struct SObjectFlex {
    uint32_t id;
    StuCanvas::utils::FlexBuffer assets;
};

struct SObjectFlexList {
    uint32_t id;
    StuCanvas::utils::FlexList assets;
};

struct SObjectpNext {
    uint32_t id;
    SObjectAssetHeader* assets_head = nullptr;
};

struct SObjectFlatMap {
    uint32_t id;
};

struct SObjectInline {
    uint32_t id;
    AssetA a;
    AssetB b;
    AssetC c;
};

// 测试规模：30,000 个对象
const size_t NUM_ELEMENTS = 300'00;
const int RUN_COUNT = 5;

// 定义全局 8 个侧边 FlatMap
FlatMap<const SObjectFlatMap*, AssetA> map_a;
FlatMap<const SObjectFlatMap*, AssetB> map_b;
FlatMap<const SObjectFlatMap*, AssetC> map_c;

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "             多态与异构数据存储 —— 终极六路对决 (6-Way Benchmark)              " << std::endl;
    std::cout << "             测试对象规模: " << NUM_ELEMENTS << " 个，挂载 8 个资产，完全自动类型推导" << std::endl;
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
        // 修正为 128 字节紧凑预留，释放极致性能
        list_flex[i]->assets.reserve(128);

        list_flex[i]->assets.push_back<AssetA>(3,  {static_cast<double>(i), 2.0});
        list_flex[i]->assets.push_back<AssetB>(10000, {{3.0, 4.0, 5.0, 6.0}});
        list_flex[i]->assets.push_back<AssetC>(50000, {{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
    }
    auto end_alloc_a = std::chrono::high_resolution_clock::now();
    double alloc_time_a = std::chrono::duration<double, std::milli>(end_alloc_a - start_alloc_a).count();

    // =========================================================================
    // 2. 方案 F 的初始化 (FlexList)
    // =========================================================================
    std::cout << "[方案 F] 正在初始化 FlexList (全自动推导编译期 ID，0 运行时参数)..." << std::endl;
    auto start_alloc_f = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlexList*> list_flex_list(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex_list[i] = new SObjectFlexList();
        list_flex_list[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex_list[i]->assets.reserve(8);

        auto* a = static_cast<AssetA*>(std::malloc(sizeof(AssetA)));
        a->x = static_cast<double>(i);
        a->y = 2.0;

        auto* b = static_cast<AssetB*>(std::malloc(sizeof(AssetB)));
        std::memset(b->data, 0, sizeof(b->data));
        b->data[0] = 3.0;

        auto* c = static_cast<AssetC*>(std::malloc(sizeof(AssetC)));
        std::memset(c->data, 0, sizeof(c->data));
        c->data[0] = 7.0;

        // 🚀 用户完全不需要指定写任何 ID 编号！模板在编译期自动分发
        list_flex_list[i]->assets.push_back<AssetA>(a);
        list_flex_list[i]->assets.push_back<AssetB>(b);
        list_flex_list[i]->assets.push_back<AssetC>(c);
    }
    auto end_alloc_f = std::chrono::high_resolution_clock::now();
    double alloc_time_f = std::chrono::duration<double, std::milli>(end_alloc_f - start_alloc_f).count();

    // =========================================================================
    // 3. 方案 B 的初始化 (pNext 指针链)
    // =========================================================================
    std::cout << "[方案 B] 正在初始化 pNext 裸指针链..." << std::endl;
    auto start_alloc_b = std::chrono::high_resolution_clock::now();
    std::vector<SObjectpNext*> list_pnext(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_pnext[i] = new SObjectpNext();
        list_pnext[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto* a = static_cast<RawMaterialAsset*>(std::malloc(sizeof(RawMaterialAsset)));
        a->header.id = 3;
        a->header.pNext = list_pnext[i]->assets_head;
        a->payload = AssetA{static_cast<double>(i), 2.0};
        list_pnext[i]->assets_head = &a->header;

        auto* b = static_cast<RawPhysicsAsset*>(std::malloc(sizeof(RawPhysicsAsset)));
        b->header.id = 10000;
        b->header.pNext = list_pnext[i]->assets_head;
        b->payload = AssetB{{3.0, 4.0, 5.0, 6.0}};
        list_pnext[i]->assets_head = &b->header;

        auto* c = static_cast<RawMetadataAsset*>(std::malloc(sizeof(RawMetadataAsset)));
        c->header.id = 50000;
        c->header.pNext = list_pnext[i]->assets_head;
        c->payload = AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}};
        list_pnext[i]->assets_head = &c->header;
    }
    auto end_alloc_b = std::chrono::high_resolution_clock::now();
    double alloc_time_b = std::chrono::duration<double, std::milli>(end_alloc_b - start_alloc_b).count();

    // =========================================================================
    // 4. 方案 C 的初始化 (FlatMap 侧边存储)
    // =========================================================================
    std::cout << "[方案 C] 正在初始化 FlatMap 侧边存储..." << std::endl;
    auto start_alloc_c = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlatMap*> list_flatmap(NUM_ELEMENTS);
    map_a.reserve(NUM_ELEMENTS);
    map_b.reserve(NUM_ELEMENTS);
    map_c.reserve(NUM_ELEMENTS);

    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flatmap[i] = new SObjectFlatMap();
        map_a.push_back_unsorted(list_flatmap[i], AssetA{static_cast<double>(i), 2.0});
        map_b.push_back_unsorted(list_flatmap[i], AssetB{{3.0, 4.0, 5.0, 6.0}});
        map_c.push_back_unsorted(list_flatmap[i], AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
    }
    map_a.sort_map();
    map_b.sort_map();
    map_c.sort_map();

    auto end_alloc_c = std::chrono::high_resolution_clock::now();
    double alloc_time_c = std::chrono::duration<double, std::milli>(end_alloc_c - start_alloc_c).count();

    // =========================================================================
    // 5. 方案 E 的初始化 (纯内联扁平结构体)
    // =========================================================================
    std::cout << "[方案 E] 正在初始化纯内联结构体..." << std::endl;
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
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << alloc_time_f << " ms (全自动类型映射)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << alloc_time_b << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << alloc_time_c << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << alloc_time_e << " ms" << std::endl;

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
            auto* a = list_flex[i]->assets.get_unsafe<AssetA>(3);
            auto* b = list_flex[i]->assets.get_unsafe<AssetB>(10000);
            auto* c = list_flex[i]->assets.get_unsafe<AssetC>(50000);
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_a += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_a /= RUN_COUNT;

    // F. 方案 F 测试 (FlexList)
    double read_time_f = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 用户零手写编号，直接把类型作为模板参数，全系统在编译期自增映射，0 运行时匹配！
            auto* a = list_flex_list[i]->assets.get_unsafe<AssetA>();
            auto* b = list_flex_list[i]->assets.get_unsafe<AssetB>();
            auto* c = list_flex_list[i]->assets.get_unsafe<AssetC>();
            sum += a->x + b->data[0] + c->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_f += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_f /= RUN_COUNT;

    // B. 方案 B 测试 (pNext 指针链)
    double read_time_b = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            SObjectAssetHeader* curr = list_pnext[i]->assets_head;
            AssetA* a = nullptr;
            AssetB* b = nullptr;
            AssetC* c = nullptr;
            while (curr != nullptr) {
                if (curr->id == 3)        a = &reinterpret_cast<RawMaterialAsset*>(curr)->payload;
                else if (curr->id == 10000) b = &reinterpret_cast<RawPhysicsAsset*>(curr)->payload;
                else if (curr->id == 50000) c = &reinterpret_cast<RawMetadataAsset*>(curr)->payload;
                curr = curr->pNext;
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
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << read_time_f << " ms (自动编译期 ID，跳跃速度超 pNext!)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << read_time_b << " ms (慢了 " << (read_time_b/read_time_f) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << read_time_c << " ms (慢了 " << (read_time_c/read_time_f) << " 倍)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << read_time_e << " ms (最快极限，理论上限)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 清理内存
    for (auto* p : list_flex) delete p;
    for (auto* p : list_flex_list) delete p;
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