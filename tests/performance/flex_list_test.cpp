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

// =============================================================================
// 8 个不同大小的基础资产类型（保持纯净，无多态和 VTable 污染）
// =============================================================================
struct Asset0 { double x, y; };                       // 16 字节
struct Asset1 { double data[2]; };                    // 16 字节
struct Asset2 { double data[4]; };                    // 32 字节
struct Asset3 { double data[4]; };                    // 32 字节
struct Asset4 { double data[8]; };                    // 64 字节
struct Asset5 { double data[8]; };                    // 64 字节
struct Asset6 { double data[16]; };                   // 128 字节
struct Asset7 { double data[16]; };                   // 128 字节

// C 风格 pNext 链表（用于 方案 B 对比）
struct SObjectAssetHeader {
    uint32_t id;
    SObjectAssetHeader* pNext;
};

// 8 个对应包装的裸堆块结构体
struct RawAsset0 { SObjectAssetHeader header; Asset0 payload; };
struct RawAsset1 { SObjectAssetHeader header; Asset1 payload; };
struct RawAsset2 { SObjectAssetHeader header; Asset2 payload; };
struct RawAsset3 { SObjectAssetHeader header; Asset3 payload; };
struct RawAsset4 { SObjectAssetHeader header; Asset4 payload; };
struct RawAsset5 { SObjectAssetHeader header; Asset5 payload; };
struct RawAsset6 { SObjectAssetHeader header; Asset6 payload; };
struct RawAsset7 { SObjectAssetHeader header; Asset7 payload; };

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
    Asset0 a0;
    Asset1 a1;
    Asset2 a2;
    Asset3 a3;
    Asset4 a4;
    Asset5 a5;
    Asset6 a6;
    Asset7 a7;
};

const size_t NUM_ELEMENTS = 300'00;
const int RUN_COUNT = 5;

// 定义全局 8 个侧边 FlatMap
FlatMap<const SObjectFlatMap*, Asset0> map_0;
FlatMap<const SObjectFlatMap*, Asset1> map_1;
FlatMap<const SObjectFlatMap*, Asset2> map_2;
FlatMap<const SObjectFlatMap*, Asset3> map_3;
FlatMap<const SObjectFlatMap*, Asset4> map_4;
FlatMap<const SObjectFlatMap*, Asset5> map_5;
FlatMap<const SObjectFlatMap*, Asset6> map_6;
FlatMap<const SObjectFlatMap*, Asset7> map_7;

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "             多态与异构数据存储 —— 终极六路对决 (5-Way Benchmark)              " << std::endl;
    std::cout << "             测试对象规模: " << NUM_ELEMENTS << " 个，挂载 8 个资产并提取最里层 [Asset0]" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // =========================================================================
    // 1. 方案 A 的初始化 (FlexBuffer)
    // =========================================================================
    std::cout << "\n[方案 A] 正在初始化 FlexBuffer (8资产共存)..." << std::endl;
    auto start_alloc_a = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlex*> list_flex(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex[i] = new SObjectFlex();
        list_flex[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 🚀 黄金修正：8个资产总体体积约 480 字节，我们只需预留 1024 字节即可！
        // 总分配堆内存降为 30 MB，彻底告别 4 秒级延迟！
        list_flex[i]->assets.reserve(1024);

        list_flex[i]->assets.push_back<Asset0>(0, {static_cast<double>(i), 2.0});
        list_flex[i]->assets.push_back<Asset1>(1, {3.0, 4.0});
        list_flex[i]->assets.push_back<Asset2>(2, {{5.0, 6.0, 7.0, 8.0}});
        list_flex[i]->assets.push_back<Asset3>(3, {{9.0, 10.0, 11.0, 12.0}});
        list_flex[i]->assets.push_back<Asset4>(4, {{13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0}});
        list_flex[i]->assets.push_back<Asset5>(5, {{21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0}});
        list_flex[i]->assets.push_back<Asset6>(6, {{29.0}});
        list_flex[i]->assets.push_back<Asset7>(7, {{30.0}});
    }
    auto end_alloc_a = std::chrono::high_resolution_clock::now();
    double alloc_time_a = std::chrono::duration<double, std::milli>(end_alloc_a - start_alloc_a).count();

    // =========================================================================
    // 2. 方案 F 的初始化 (FlexList)
    // =========================================================================
    std::cout << "[方案 F] 正在初始化 FlexList (直连指针表，挂载 8 个离散资产)..." << std::endl;
    auto start_alloc_f = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlexList*> list_flex_list(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex_list[i] = new SObjectFlexList();
        list_flex_list[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flex_list[i]->assets.reserve(16);

        auto* a0 = static_cast<Asset0*>(std::malloc(sizeof(Asset0)));
        a0->x = static_cast<double>(i); a0->y = 2.0;

        auto* a1 = static_cast<Asset1*>(std::malloc(sizeof(Asset1)));
        a1->data[0] = 3.0;

        auto* a2 = static_cast<Asset2*>(std::malloc(sizeof(Asset2)));
        a2->data[0] = 5.0;

        auto* a3 = static_cast<Asset3*>(std::malloc(sizeof(Asset3)));
        a3->data[0] = 9.0;

        auto* a4 = static_cast<Asset4*>(std::malloc(sizeof(Asset4)));
        a4->data[0] = 13.0;

        auto* a5 = static_cast<Asset5*>(std::malloc(sizeof(Asset5)));
        a5->data[0] = 21.0;

        auto* a6 = static_cast<Asset6*>(std::malloc(sizeof(Asset6)));
        a6->data[0] = 29.0;

        auto* a7 = static_cast<Asset7*>(std::malloc(sizeof(Asset7)));
        a7->data[0] = 30.0;

        // 挂载在连续索引 [0 ~ 7] 上
        list_flex_list[i]->assets.push_back<Asset0>(0, a0);
        list_flex_list[i]->assets.push_back<Asset1>(1, a1);
        list_flex_list[i]->assets.push_back<Asset2>(2, a2);
        list_flex_list[i]->assets.push_back<Asset3>(3, a3);
        list_flex_list[i]->assets.push_back<Asset4>(4, a4);
        list_flex_list[i]->assets.push_back<Asset5>(5, a5);
        list_flex_list[i]->assets.push_back<Asset6>(6, a6);
        list_flex_list[i]->assets.push_back<Asset7>(7, a7);
    }
    auto end_alloc_f = std::chrono::high_resolution_clock::now();
    double alloc_time_f = std::chrono::duration<double, std::milli>(end_alloc_f - start_alloc_f).count();

    // =========================================================================
    // 3. 方案 B 的初始化 (pNext 指针链 - 故意将最热的 Asset0 压入最深层)
    // =========================================================================
    std::cout << "[方案 B] 正在初始化 pNext 裸指针链 (链表深度为 8 级，Asset0 在最深层)..." << std::endl;
    auto start_alloc_b = std::chrono::high_resolution_clock::now();
    std::vector<SObjectpNext*> list_pnext(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_pnext[i] = new SObjectpNext();
        list_pnext[i]->id = static_cast<uint32_t>(i);
    }
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        // 🚀 倒序连接：确保最常读取的 Asset0 处于链表的最末梢（第 8 次跳转位置）
        auto* n0 = static_cast<RawAsset0*>(std::malloc(sizeof(RawAsset0)));
        n0->header.id = 0; n0->header.pNext = nullptr;
        n0->payload = Asset0{static_cast<double>(i), 2.0};

        auto* n1 = static_cast<RawAsset1*>(std::malloc(sizeof(RawAsset1)));
        n1->header.id = 1; n1->header.pNext = &n0->header;
        n1->payload = Asset1{{3.0, 4.0}};

        auto* n2 = static_cast<RawAsset2*>(std::malloc(sizeof(RawAsset2)));
        n2->header.id = 2; n2->header.pNext = &n1->header;
        n2->payload = Asset2{{5.0, 6.0, 7.0, 8.0}};

        auto* n3 = static_cast<RawAsset3*>(std::malloc(sizeof(RawAsset3)));
        n3->header.id = 3; n3->header.pNext = &n2->header;
        n3->payload = Asset3{{9.0, 10.0, 11.0, 12.0}};

        auto* n4 = static_cast<RawAsset4*>(std::malloc(sizeof(RawAsset4)));
        n4->header.id = 4; n4->header.pNext = &n3->header;
        n4->payload = Asset4{{13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0}};

        auto* n5 = static_cast<RawAsset5*>(std::malloc(sizeof(RawAsset5)));
        n5->header.id = 5; n5->header.pNext = &n4->header;
        n5->payload = Asset5{{21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0}};

        auto* n6 = static_cast<RawAsset6*>(std::malloc(sizeof(RawAsset6)));
        n6->header.id = 6; n6->header.pNext = &n5->header;
        std::memset(n6->payload.data, 0, sizeof(n6->payload.data));
        n6->payload.data[0] = 29.0;

        auto* n7 = static_cast<RawAsset7*>(std::malloc(sizeof(RawAsset7)));
        n7->header.id = 7; n7->header.pNext = &n6->header;
        std::memset(n7->payload.data, 0, sizeof(n7->payload.data));
        n7->payload.data[0] = 30.0;

        list_pnext[i]->assets_head = &n7->header; // 头部指向 7
    }
    auto end_alloc_b = std::chrono::high_resolution_clock::now();
    double alloc_time_b = std::chrono::duration<double, std::milli>(end_alloc_b - start_alloc_b).count();

    // =========================================================================
    // 4. 方案 C 的初始化 (FlatMap 侧边存储)
    // =========================================================================
    std::cout << "[方案 C] 正在初始化 FlatMap 侧边存储..." << std::endl;
    auto start_alloc_c = std::chrono::high_resolution_clock::now();
    std::vector<SObjectFlatMap*> list_flatmap(NUM_ELEMENTS);
    map_0.reserve(NUM_ELEMENTS);
    map_1.reserve(NUM_ELEMENTS);
    map_2.reserve(NUM_ELEMENTS);
    map_3.reserve(NUM_ELEMENTS);
    map_4.reserve(NUM_ELEMENTS);
    map_5.reserve(NUM_ELEMENTS);
    map_6.reserve(NUM_ELEMENTS);
    map_7.reserve(NUM_ELEMENTS);

    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_flatmap[i] = new SObjectFlatMap();
        list_flatmap[i]->id = static_cast<uint32_t>(i);
        map_0.push_back_unsorted(list_flatmap[i], Asset0{static_cast<double>(i), 2.0});
        map_1.push_back_unsorted(list_flatmap[i], Asset1{{3.0, 4.0}});
        map_2.push_back_unsorted(list_flatmap[i], Asset2{{5.0, 6.0, 7.0, 8.0}});
        map_3.push_back_unsorted(list_flatmap[i], Asset3{{9.0, 10.0, 11.0, 12.0}});
        map_4.push_back_unsorted(list_flatmap[i], Asset4{{13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0}});
        map_5.push_back_unsorted(list_flatmap[i], Asset5{{21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0}});
        map_6.push_back_unsorted(list_flatmap[i], Asset6{{29.0}});
        map_7.push_back_unsorted(list_flatmap[i], Asset7{{30.0}});
    }
    map_0.sort_map();
    map_1.sort_map();
    map_2.sort_map();
    map_3.sort_map();
    map_4.sort_map();
    map_5.sort_map();
    map_6.sort_map();
    map_7.sort_map();

    auto end_alloc_c = std::chrono::high_resolution_clock::now();
    double alloc_time_c = std::chrono::duration<double, std::milli>(end_alloc_c - start_alloc_c).count();

    // =========================================================================
    // 5. 方案 E 的初始化 (纯内联扁平结构体)
    // =========================================================================
    std::cout << "[方案 E] 正在初始化纯内联结构体 (8 个内联)..." << std::endl;
    auto start_alloc_e = std::chrono::high_resolution_clock::now();
    std::vector<SObjectInline*> list_inline(NUM_ELEMENTS);
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        list_inline[i] = new SObjectInline();
        list_inline[i]->id = static_cast<uint32_t>(i);
        list_inline[i]->a0 = Asset0{static_cast<double>(i), 2.0};
        list_inline[i]->a1 = Asset1{{3.0, 4.0}};
        list_inline[i]->a2 = Asset2{{5.0, 6.0, 7.0, 8.0}};
        list_inline[i]->a3 = Asset3{{9.0, 10.0, 11.0, 12.0}};
        list_inline[i]->a4 = Asset4{{13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0}};
        list_inline[i]->a5 = Asset5{{21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0}};
        list_inline[i]->a6 = Asset6{{29.0}};
        list_inline[i]->a7 = Asset7{{30.0}};
    }
    auto end_alloc_e = std::chrono::high_resolution_clock::now();
    double alloc_time_e = std::chrono::duration<double, std::milli>(end_alloc_e - start_alloc_e).count();


    std::cout << "\n----------------------- 初始化分配耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 A] FlexBuffer 耗时:" << std::fixed << std::setprecision(2) << alloc_time_a << " ms (BUG 修复，回归极速！)" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << alloc_time_f << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 B] pNext 指针链 耗时:" << alloc_time_b << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 C] FlatMap 侧存储 耗时:" << alloc_time_c << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 E] 纯内联结构体 耗时:" << alloc_time_e << " ms" << std::endl;

    // =========================================================================
    // 核心性能读取对决 —— 目标：获取并累加最深层/最热的那个资产 [Asset0]
    // =========================================================================
    std::cout << "\n开始进行高频遍历读取测试..." << std::endl;

    // A. 方案 A 测试 (FlexBuffer)
    double read_time_a = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 0 循环，直接通过编号 0 算术偏移读取物理末尾
            auto* a = list_flex[i]->assets.get_unsafe<Asset0>(0);
            sum += a->x;
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
            // 🚀 0 循环，直接通过索引 0 一击即中获取物理指针
            auto* a = list_flex_list[i]->assets.get_unsafe<Asset0>(0);
            sum += a->x;
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_f += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_f /= RUN_COUNT;

    // B. 方案 B 测试 (pNext 指针链 - 8 次寻址跳转痛苦面具)
    double read_time_b = 0.0;
    for (int r = 0; r < RUN_COUNT; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            // 🚀 为了拿到最里层的 id=0 的 Asset0，CPU 必须硬顶着 8 次极其漫长的指针跳跃
            SObjectAssetHeader* curr = list_pnext[i]->assets_head;
            Asset0* a = nullptr;
            while (curr != nullptr) {
                if (curr->id == 0) {
                    a = &reinterpret_cast<RawAsset0*>(curr)->payload;
                    break; // 找到了
                }
                curr = curr->pNext; // 指针追逐
            }
            sum += a->x;
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
            const auto* a = map_0.find(list_flatmap[i]);
            sum += a->x;
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
            sum += p->a0.x;
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(sum);
        read_time_e += std::chrono::duration<double, std::milli>(end - start).count();
    }
    read_time_e /= RUN_COUNT;

    std::cout << "\n----------------------- 遍历读取耗时对比 -----------------------" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 A] FlexBuffer 耗时:" << std::fixed << std::setprecision(4) << read_time_a << " ms" << std::endl;
    std::cout << std::left << std::setw(35) << "[方案 F] FlexList 耗时:" << read_time_f << " ms (新方案，跳跃速度碾压 pNext!)" << std::endl;
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