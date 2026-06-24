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
#include "../stucanvas/utils/flex_list.hpp"

template <typename T>
void do_not_optimize(T&& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(val) : "memory");
#else
    volatile T dummy = val;
#endif
}

// 物理资产（定义 8 种尺寸不同的异构数据资产）
struct AssetA { double x, y; };
struct AssetB { double data[4]; };
struct AssetC { double data[8]; };
struct AssetD { double data[2]; };
struct AssetE { double data[3]; };
struct AssetF { double data[5]; };
struct AssetG { double data[6]; };
struct AssetH { double data[7]; };

// C 风格 pNext 链表
struct SObjectAssetHeader {
    uint32_t id;
    SObjectAssetHeader* pNext;
};

// 8 层裸指针链表所需要的节点包装定义
struct RawAssetA { SObjectAssetHeader header; AssetA payload; };
struct RawAssetB { SObjectAssetHeader header; AssetB payload; };
struct RawAssetC { SObjectAssetHeader header; AssetC payload; };
struct RawAssetD { SObjectAssetHeader header; AssetD payload; };
struct RawAssetE { SObjectAssetHeader header; AssetE payload; };
struct RawAssetF { SObjectAssetHeader header; AssetF payload; };
struct RawAssetG { SObjectAssetHeader header; AssetG payload; };
struct RawAssetH { SObjectAssetHeader header; AssetH payload; };

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
    AssetD d;
    AssetE e;
    AssetF f;
    AssetG g;
    AssetH h;
};

// 测试规模：30,000 个对象
const size_t NUM_ELEMENTS = 300'00;
const int RUN_COUNT = 5;

// 定义全局侧边 FlatMap（扩充至 8 个平行侧表）
FlatMap<const SObjectFlatMap*, AssetA> map_a;
FlatMap<const SObjectFlatMap*, AssetB> map_b;
FlatMap<const SObjectFlatMap*, AssetC> map_c;
FlatMap<const SObjectFlatMap*, AssetD> map_d;
FlatMap<const SObjectFlatMap*, AssetE> map_e;
FlatMap<const SObjectFlatMap*, AssetF> map_f;
FlatMap<const SObjectFlatMap*, AssetG> map_g;
FlatMap<const SObjectFlatMap*, AssetH> map_h;

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "             多态与异构数据存储 —— 终极五路对决 (5-Way Benchmark)              " << std::endl;
    std::cout << "             测试对象规模: " << NUM_ELEMENTS << " 个，测试各代方案在指定读取下的表现" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // =========================================================================
    // 1. 方案 F 的初始化 (FlexList - 全自动挡 1 类型 ↔ 1 插槽)
    // =========================================================================
    std::cout << "\n[方案 F] 正在初始化 FlexList (完美转发自动原位构造，不提供任何 ID)..." << std::endl;
    auto start_alloc_f = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlexList*> list_flex_list(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex_list[i] = new SObjectFlexList();
        list_flex_list[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 自动挡 API：完全隐式按类型进行连续构造与扩容
        list_flex_list[i]->assets.emplace<AssetA>(static_cast<double>(i), 2.0);
        list_flex_list[i]->assets.emplace<AssetB>(AssetB{{3.0, 4.0, 5.0, 6.0}});
        list_flex_list[i]->assets.emplace<AssetC>(AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
        list_flex_list[i]->assets.emplace<AssetD>(AssetD{{15.0, 16.0}});
        list_flex_list[i]->assets.emplace<AssetE>(AssetE{{17.0, 18.0, 19.0}});
        list_flex_list[i]->assets.emplace<AssetF>(AssetF{{20.0, 21.0, 22.0, 23.0, 24.0}});
        list_flex_list[i]->assets.emplace<AssetG>(AssetG{{25.0, 26.0, 27.0, 28.0, 29.0, 30.0}});
        list_flex_list[i]->assets.emplace<AssetH>(AssetH{{31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0}});
    }
    auto end_alloc_f = std::chrono::high_resolution_clock::now();
    double alloc_time_f = std::chrono::duration<double, std::milli>(end_alloc_f - start_alloc_f).count();

    // =========================================================================
    // 2. 方案 B 的初始化 (8 层 pNext 指针链表)
    // =========================================================================
    std::cout << "[方案 B] 正在初始化 8 层 pNext 裸指针链..." << std::endl;
    auto start_alloc_b = std::chrono::high_resolution_clock::now();
    std::vector<SObjectpNext*> list_pnext(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_pnext[i] = new SObjectpNext();
        list_pnext[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 依次 malloc 并压入 8 层节点
        auto* h = static_cast<RawAssetH*>(std::malloc(sizeof(RawAssetH)));
        h->header.id = 7;
        h->header.pNext = list_pnext[i]->assets_head;
        h->payload = AssetH{{31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0}};
        list_pnext[i]->assets_head = &h->header;

        auto* g = static_cast<RawAssetG*>(std::malloc(sizeof(RawAssetG)));
        g->header.id = 6;
        g->header.pNext = list_pnext[i]->assets_head;
        g->payload = AssetG{{25.0, 26.0, 27.0, 28.0, 29.0, 30.0}};
        list_pnext[i]->assets_head = &g->header;

        auto* f = static_cast<RawAssetF*>(std::malloc(sizeof(RawAssetF)));
        f->header.id = 5;
        f->header.pNext = list_pnext[i]->assets_head;
        f->payload = AssetF{{20.0, 21.0, 22.0, 23.0, 24.0}};
        list_pnext[i]->assets_head = &f->header;

        auto* e = static_cast<RawAssetE*>(std::malloc(sizeof(RawAssetE)));
        e->header.id = 4;
        e->header.pNext = list_pnext[i]->assets_head;
        e->payload = AssetE{{17.0, 18.0, 19.0}};
        list_pnext[i]->assets_head = &e->header;

        auto* d = static_cast<RawAssetD*>(std::malloc(sizeof(RawAssetD)));
        d->header.id = 3;
        d->header.pNext = list_pnext[i]->assets_head;
        d->payload = AssetD{{15.0, 16.0}};
        list_pnext[i]->assets_head = &d->header;

        auto* c = static_cast<RawAssetC*>(std::malloc(sizeof(RawAssetC)));
        c->header.id = 2;
        c->header.pNext = list_pnext[i]->assets_head;
        c->payload = AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}};
        list_pnext[i]->assets_head = &c->header;

        auto* b = static_cast<RawAssetB*>(std::malloc(sizeof(RawAssetB)));
        b->header.id = 1;
        b->header.pNext = list_pnext[i]->assets_head;
        b->payload = AssetB{{3.0, 4.0, 5.0, 6.0}};
        list_pnext[i]->assets_head = &b->header;

        auto* a = static_cast<RawAssetA*>(std::malloc(sizeof(RawAssetA)));
        a->header.id = 0;
        a->header.pNext = list_pnext[i]->assets_head;
        a->payload = AssetA{static_cast<double>(i), 2.0};
        list_pnext[i]->assets_head = &a->header;
    }
    auto end_alloc_b = std::chrono::high_resolution_clock::now();
    double alloc_time_b = std::chrono::duration<double, std::milli>(end_alloc_b - start_alloc_b).count();

    // =========================================================================
    // 3. 方案 C 的初始化 (FlatMap 侧边 8 个表平行存储)
    // =========================================================================
    std::cout << "[方案 C] 正在初始化 FlatMap 侧边存储..." << std::endl;
    auto start_alloc_c = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlatMap*> list_flatmap(NUM_ELEMENTS);
    map_a.reserve(NUM_ELEMENTS);
    map_b.reserve(NUM_ELEMENTS);
    map_c.reserve(NUM_ELEMENTS);
    map_d.reserve(NUM_ELEMENTS);
    map_e.reserve(NUM_ELEMENTS);
    map_f.reserve(NUM_ELEMENTS);
    map_g.reserve(NUM_ELEMENTS);
    map_h.reserve(NUM_ELEMENTS);

    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flatmap[i] = new SObjectFlatMap();
        map_a.push_back_unsorted(list_flatmap[i], AssetA{static_cast<double>(i), 2.0});
        map_b.push_back_unsorted(list_flatmap[i], AssetB{{3.0, 4.0, 5.0, 6.0}});
        map_c.push_back_unsorted(list_flatmap[i], AssetC{{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}});
        map_d.push_back_unsorted(list_flatmap[i], AssetD{{15.0, 16.0}});
        map_e.push_back_unsorted(list_flatmap[i], AssetE{{17.0, 18.0, 19.0}});
        map_f.push_back_unsorted(list_flatmap[i], AssetF{{20.0, 21.0, 22.0, 23.0, 24.0}});
        map_g.push_back_unsorted(list_flatmap[i], AssetG{{25.0, 26.0, 27.0, 28.0, 29.0, 30.0}});
        map_h.push_back_unsorted(list_flatmap[i], AssetH{{31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0}});
    }
    map_a.sort_map();
    map_b.sort_map();
    map_c.sort_map();
    map_d.sort_map();
    map_e.sort_map();
    map_f.sort_map();
    map_g.sort_map();
    map_h.sort_map();

    auto end_alloc_c = std::chrono::high_resolution_clock::now();
    double alloc_time_c = std::chrono::duration<double, std::milli>(end_alloc_c - start_alloc_c).count();

    // =========================================================================
    // 4. 方案 E 的初始化 (纯内联 8 资产扁平结构体)
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
        list_inline[i]->d = AssetD{{15.0, 16.0}};
        list_inline[i]->e = AssetE{{17.0, 18.0, 19.0}};
        list_inline[i]->f = AssetF{{20.0, 21.0, 22.0, 23.0, 24.0}};
        list_inline[i]->g = AssetG{{25.0, 26.0, 27.0, 28.0, 29.0, 30.0}};
        list_inline[i]->h = AssetH{{31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0}};
    }
    auto end_alloc_e = std::chrono::high_resolution_clock::now();
    double alloc_time_e = std::chrono::duration<double, std::milli>(end_alloc_e - start_alloc_e).count();


    std::cout << "\n----------------------- 初始化分配耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << std::fixed << std::setprecision(2) << alloc_time_f << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << alloc_time_b << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << alloc_time_c << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << alloc_time_e << " ms" << std::endl;

    // =========================================================================
    // 核心性能读取对决 —— 目标：高频全量获取并累加全部的这 8 个资产
    // =========================================================================
    std::cout << "\n开始进行高频遍历读取测试..." << std::endl;

    // F. 方案 F 测试 (FlexList - 零跳转直接寻址，支持 8 资产)
    double read_time_f = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 通过静态类型指针机制，实现 O(1) 物理索引偏移读取
            auto* a = list_flex_list[i]->assets.get_unsafe<AssetA>();
            auto* b = list_flex_list[i]->assets.get_unsafe<AssetB>();
            auto* c = list_flex_list[i]->assets.get_unsafe<AssetC>();
            auto* d = list_flex_list[i]->assets.get_unsafe<AssetD>();
            auto* e = list_flex_list[i]->assets.get_unsafe<AssetE>();
            auto* f = list_flex_list[i]->assets.get_unsafe<AssetF>();
            auto* g = list_flex_list[i]->assets.get_unsafe<AssetG>();
            auto* h = list_flex_list[i]->assets.get_unsafe<AssetH>();

            sum += a->x + b->data[0] + c->data[0] + d->data[0] + e->data[0] + f->data[0] + g->data[0] + h->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_f += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_f /= RUN_COUNT;

    // B. 方案 B 测试 (8 层 pNext 指针链表 - 链式寻址跳转)
    double read_time_b = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            SObjectAssetHeader* curr = list_pnext[i]->assets_head;
            AssetA* a = nullptr;
            AssetB* b = nullptr;
            AssetC* c = nullptr;
            AssetD* d = nullptr;
            AssetE* e = nullptr;
            AssetF* f = nullptr;
            AssetG* g = nullptr;
            AssetH* h = nullptr;

            // 8 层深度指针链查找，极其严重的指针追逐开销
            while (curr != nullptr) {
                switch (curr->id) {
                    case 0: a = &reinterpret_cast<RawAssetA*>(curr)->payload; break;
                    case 1: b = &reinterpret_cast<RawAssetB*>(curr)->payload; break;
                    case 2: c = &reinterpret_cast<RawAssetC*>(curr)->payload; break;
                    case 3: d = &reinterpret_cast<RawAssetD*>(curr)->payload; break;
                    case 4: e = &reinterpret_cast<RawAssetE*>(curr)->payload; break;
                    case 5: f = &reinterpret_cast<RawAssetF*>(curr)->payload; break;
                    case 6: g = &reinterpret_cast<RawAssetG*>(curr)->payload; break;
                    case 7: h = &reinterpret_cast<RawAssetH*>(curr)->payload; break;
                }
                curr = curr->pNext;
            }
            sum += a->x + b->data[0] + c->data[0] + d->data[0] + e->data[0] + f->data[0] + g->data[0] + h->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_b += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_b /= RUN_COUNT;

    // C. 方案 C 测试 (FlatMap 侧存储 - 8 个侧表并发二分查找)
    double read_time_c = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            const auto* a = map_a.find(list_flatmap[i]);
            const auto* b = map_b.find(list_flatmap[i]);
            const auto* c = map_c.find(list_flatmap[i]);
            const auto* d = map_d.find(list_flatmap[i]);
            const auto* e = map_e.find(list_flatmap[i]);
            const auto* f = map_f.find(list_flatmap[i]);
            const auto* g = map_g.find(list_flatmap[i]);
            const auto* h = map_h.find(list_flatmap[i]);

            sum += a->x + b->data[0] + c->data[0] + d->data[0] + e->data[0] + f->data[0] + g->data[0] + h->data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_c += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_c /= RUN_COUNT;

    // E. 方案 E 测试 (纯内联 8 资产扁平结构体)
    double read_time_e = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            auto* p = list_inline[i];
            sum += p->a.x + p->b.data[0] + p->c.data[0] + p->d.data[0] + p->e.data[0] + p->f.data[0] + p->g.data[0] + p->h.data[0];
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_e += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_e /= RUN_COUNT;

    std::cout << "\n----------------------- 遍历读取耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << std::fixed << std::setprecision(4) << read_time_f << " ms (新方案，完美转发全自动无 ID 寻址!)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << read_time_b << " ms (8 层深度指针追逐)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << read_time_c << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << read_time_e << " ms (最快极限，理论上限)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 清理分配的物理内存
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

    // =========================================================================
    // FlexList 新版全自动挡功能性测试
    // =========================================================================
    struct TestStructA { int x; int y; };     // 8 字节
    struct TestStructB { float u; float v; }; // 8 字节（大小与 TestStructA 完全一致）

    StuCanvas::utils::FlexList list;

    // 1. 直接挂载，完全不提供任何显式 ID
    list.emplace<TestStructA>(10, 20);

    std::cout << "\n[FlexList 功能测试]" << std::endl;

    // 2. 检测是否存在
    if (list.has<TestStructA>()) {
        auto* ptr = list.get<TestStructA>();
        std::cout << " -> 成功检测并获取 TestStructA 资产！数据值: x = " << ptr->x << ", y = " << ptr->y << std::endl;
    }

    // 3. 验证同尺寸不同类型的类型隔离安全性
    if (!list.has<TestStructB>()) {
        std::cout << " -> 成功！同尺寸不同类型的 TestStructB 未被容器误判存在。" << std::endl;
    }

    return 0;
}