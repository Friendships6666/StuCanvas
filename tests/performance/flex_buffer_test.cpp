// main.cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <memory>
#include <random>
#include <algorithm>
#include <iomanip>
#include <string>
#include <vector>
#include "flex_vector.hpp"

// 防止编译器优化掉测试结果
template <typename T>
void do_not_optimize(const T& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(&val) : "memory");
#else
    const volatile void* p = static_cast<const volatile void*>(&val);
    (void)p;
#endif
}

// 计时辅助器
struct Timer {
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
    Timer(std::string n) : name(std::move(n)), start(std::chrono::high_resolution_clock::now()) {}
    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "  " << std::left << std::setw(50) << name << " : "
                  << std::right << std::setw(10) << std::fixed << std::setprecision(3)
                  << duration << " ms" << std::endl;
    }
};

// =========================================================================
// 🚀 1. 定义标准 OOP 多态基类 (用于 RTTI 对比)
// =========================================================================
struct OOPBase {
    virtual ~OOPBase() = default;
    virtual void update() noexcept = 0; // 模拟虚调用
};

// =========================================================================
// 🚀 2. 定义 24 个异构组件类型 (12个 Trivial + 12个 Non-Trivial)
// =========================================================================
#define DEFINE_TRIVIAL_TYPE(ID, ... ) \
struct Trivial##ID { \
    __VA_ARGS__ \
    inline void update() noexcept { (void)this; } \
}; \
struct OOPTrivial##ID : public OOPBase { \
    __VA_ARGS__ \
    void update() noexcept override { (void)this; } \
};

#define DEFINE_NON_TRIVIAL_TYPE(ID, ... ) \
struct NonTrivial##ID { \
    __VA_ARGS__ \
    NonTrivial##ID() = default; \
    ~NonTrivial##ID() { (void)this; } \
    NonTrivial##ID(const NonTrivial##ID&) = default; \
    NonTrivial##ID(NonTrivial##ID&&) noexcept = default; \
    inline void update() noexcept { (void)this; } \
}; \
struct OOPNonTrivial##ID : public OOPBase { \
    __VA_ARGS__ \
    OOPNonTrivial##ID() = default; \
    ~OOPNonTrivial##ID() override { (void)this; } \
    OOPNonTrivial##ID(const OOPNonTrivial##ID&) = default; \
    OOPNonTrivial##ID(OOPNonTrivial##ID&&) noexcept = default; \
    void update() noexcept override { (void)this; } \
};

// 12 个物理对齐、尺寸不同的 Trivial (POD) 类型 [1.1.1]
DEFINE_TRIVIAL_TYPE(0, int a = 1; float b = 1.1f;)
DEFINE_TRIVIAL_TYPE(1, double c = 2.2; int d = 2;)
DEFINE_TRIVIAL_TYPE(2, char e[16] = "trivial_2";)
DEFINE_TRIVIAL_TYPE(3, float f = 3.3f; double g = 4.4;)
DEFINE_TRIVIAL_TYPE(4, int h = 4; char i = 'A';)
DEFINE_TRIVIAL_TYPE(5, uint64_t j = 555; uint32_t k = 5;)
DEFINE_TRIVIAL_TYPE(6, float l[4] = {1, 2, 3, 4};)
DEFINE_TRIVIAL_TYPE(7, double m = 7.7; float n = 8.8f;)
DEFINE_TRIVIAL_TYPE(8, uint16_t o = 88; char p[8] = "test";)
DEFINE_TRIVIAL_TYPE(9, int q = 9; float r = 9.9f;)
DEFINE_TRIVIAL_TYPE(10, double s = 10.10; int t = 10;)
DEFINE_TRIVIAL_TYPE(11, char u[32] = "trivial_last";)

// 12 个含有动态分配资源的 Non-Trivial 类型 (用于测试强移动重定位与 RAII 销毁)
DEFINE_NON_TRIVIAL_TYPE(0, std::string a = "non_trivial_0";)
DEFINE_NON_TRIVIAL_TYPE(1, std::vector<int> b = {1, 2, 3};)
DEFINE_NON_TRIVIAL_TYPE(2, std::string c = "non_trivial_2_long_string_to_force_heap_allocation";)
DEFINE_NON_TRIVIAL_TYPE(3, std::vector<double> d = {1.1, 2.2, 3.3};)
DEFINE_NON_TRIVIAL_TYPE(4, std::string e = "short"; std::vector<char> f = {'a', 'b'};)
DEFINE_NON_TRIVIAL_TYPE(5, std::vector<std::string> g = {"hello", "world"};)
DEFINE_NON_TRIVIAL_TYPE(6, std::string h = "non_trivial_6";)
DEFINE_NON_TRIVIAL_TYPE(7, std::vector<int> i = {7, 8, 9, 10};)
DEFINE_NON_TRIVIAL_TYPE(8, std::string j = "another_string_eight_eight_eight";)
DEFINE_NON_TRIVIAL_TYPE(9, std::vector<float> k = {9.9f};)
DEFINE_NON_TRIVIAL_TYPE(10, std::string l = "string_ten_ten_ten_ten";)
DEFINE_NON_TRIVIAL_TYPE(11, std::vector<uint32_t> m = {11, 22, 33};)

int main() {
    std::cout << "==================================================================" << std::endl;
    std::cout << "         FlexVector vs C++ Standard OOP & RTTI Benchmark          " << std::endl;
    std::cout << "==================================================================" << std::endl;

    constexpr size_t NUM_ENTITIES = 50'000; // 50,000 个实体
    std::cout << "异构实体(Entities) 数量 : " << NUM_ENTITIES << " 个" << std::endl;
    std::cout << "每个实体的最大组件数   : 24 个 (12个 Trivial + 12个 Non-Trivial)" << std::endl;
    std::cout << "总对象存储体量          : " << NUM_ENTITIES * 24 << " 个" << std::endl;
    std::cout << "------------------------------------------------------------------" << std::endl;

    // =========================================================================
    // 🚀 测试一：批量异构原位构造性能 (Bulk Insertion)
    // =========================================================================
    std::cout << "\n[1] Bulk Insertion Performance (Memory Layout Setup):" << std::endl;

    std::vector<StuCanvas::utils::FlexVector<>> flex_entities;
    {
        Timer t("A. FlexVector (Flat Buffer Merged Allocation)");
        flex_entities.resize(NUM_ENTITIES);
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = flex_entities[i];
            
            // 写入 12 个 Trivial 组件
            entity.emplace_back<Trivial0>();
            entity.emplace_back<Trivial1>();
            entity.emplace_back<Trivial2>();
            entity.emplace_back<Trivial3>();
            entity.emplace_back<Trivial4>();
            entity.emplace_back<Trivial5>();
            entity.emplace_back<Trivial6>();
            entity.emplace_back<Trivial7>();
            entity.emplace_back<Trivial8>();
            entity.emplace_back<Trivial9>();
            entity.emplace_back<Trivial10>();
            entity.emplace_back<Trivial11>();

            // 写入 12 个 Non-Trivial 组件
            entity.emplace_back<NonTrivial0>();
            entity.emplace_back<NonTrivial1>();
            entity.emplace_back<NonTrivial2>();
            entity.emplace_back<NonTrivial3>();
            entity.emplace_back<NonTrivial4>();
            entity.emplace_back<NonTrivial5>();
            entity.emplace_back<NonTrivial6>();
            entity.emplace_back<NonTrivial7>();
            entity.emplace_back<NonTrivial8>();
            entity.emplace_back<NonTrivial9>();
            entity.emplace_back<NonTrivial10>();
            entity.emplace_back<NonTrivial11>();
        }
    }

    std::vector<std::vector<std::unique_ptr<OOPBase>>> oop_entities;
    {
        Timer t("B. C++ Standard OOP (24 heap allocs per entity!)");
        oop_entities.resize(NUM_ENTITIES);
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = oop_entities[i];
            entity.reserve(24);
            
            entity.push_back(std::make_unique<OOPTrivial0>());
            entity.push_back(std::make_unique<OOPTrivial1>());
            entity.push_back(std::make_unique<OOPTrivial2>());
            entity.push_back(std::make_unique<OOPTrivial3>());
            entity.push_back(std::make_unique<OOPTrivial4>());
            entity.push_back(std::make_unique<OOPTrivial5>());
            entity.push_back(std::make_unique<OOPTrivial6>());
            std::vector<std::unique_ptr<OOPBase>> temp;
            entity.push_back(std::make_unique<OOPTrivial7>());
            entity.push_back(std::make_unique<OOPTrivial8>());
            entity.push_back(std::make_unique<OOPTrivial9>());
            entity.push_back(std::make_unique<OOPTrivial10>());
            entity.push_back(std::make_unique<OOPTrivial11>());

            entity.push_back(std::make_unique<OOPNonTrivial0>());
            entity.push_back(std::make_unique<OOPNonTrivial1>());
            entity.push_back(std::make_unique<OOPNonTrivial2>());
            entity.push_back(std::make_unique<OOPNonTrivial3>());
            entity.push_back(std::make_unique<OOPNonTrivial4>());
            entity.push_back(std::make_unique<OOPNonTrivial5>());
            entity.push_back(std::make_unique<OOPNonTrivial6>());
            entity.push_back(std::make_unique<OOPNonTrivial7>());
            entity.push_back(std::make_unique<OOPNonTrivial8>());
            entity.push_back(std::make_unique<OOPNonTrivial9>());
            entity.push_back(std::make_unique<OOPNonTrivial10>());
            entity.push_back(std::make_unique<OOPNonTrivial11>());
        }
    }

    // =========================================================================
    // 🚀 测试二：类型动态检索性能 (Type Retrieval / get<T>() vs dynamic_cast)
    // =========================================================================
    std::cout << "\n[2] Type Retrieval/Query Performance (O(1) Slot vs dynamic_cast):" << std::endl;

    {
        Timer t("A. FlexVector O(1) Flat Slot Directory");
        uint64_t checksum = 0;
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = flex_entities[i];
            
            // 检索平凡对象并读取值
            auto* comp_t5 = entity.get<Trivial5>();
            if (comp_t5) [[likely]] {
                checksum += comp_t5->j;
            }
            // 检索非平凡对象并读取 vector 长度
            auto* comp_nt7 = entity.get<NonTrivial7>();
            if (comp_nt7) [[likely]] {
                checksum += comp_nt7->i.size();
            }
        }
        do_not_optimize(checksum);
        std::cout << "     (Checksum: " << checksum << ")\n";
    }

    {
        Timer t("B. C++ Standard OOP RTTI dynamic_cast (Array Scan)");
        uint64_t checksum = 0;
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = oop_entities[i];
            OOPTrivial5* comp_t5 = nullptr;
            OOPNonTrivial7* comp_nt7 = nullptr;

            // 传统的指针数组扫描，对每一个多态指针尝试进行 RTTI dynamic_cast 判定 [1.2.4]
            for (auto& base_ptr : entity) {
                if (!comp_t5) {
                    comp_t5 = dynamic_cast<OOPTrivial5*>(base_ptr.get());
                }
                if (!comp_nt7) {
                    comp_nt7 = dynamic_cast<OOPNonTrivial7*>(base_ptr.get());
                }
                if (comp_t5 && comp_nt7) break; // 提前找到终止
            }

            if (comp_t5) [[likely]] {
                checksum += comp_t5->j;
            }
            if (comp_nt7) [[likely]] {
                checksum += comp_nt7->i.size();
            }
        }
        do_not_optimize(checksum);
        std::cout << "     (Checksum: " << checksum << ")\n";
    }

    // =========================================================================
    // 🚀 测试三：链表顺序迭代性能 (Sequential Iteration)
    // =========================================================================
    std::cout << "\n[3] Sequential Iteration Performance (Object Traversal):" << std::endl;

    {
        Timer t("A. FlexVector Intrusive Singly-Linked List Iteration");
        uint64_t object_count = 0;
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = flex_entities[i];
            for (auto it = entity.begin(); it != entity.end(); ++it) {
                // 沿着 Flat Buffer 中的 `next_oh_offset` 指针链快速滑行
                object_count++;
            }
        }
        do_not_optimize(object_count);
        std::cout << "     (Total elements iterated: " << object_count << ")\n";
    }

    {
        Timer t("B. C++ Standard OOP Array Iteration (Virtual Call)");
        uint64_t object_count = 0;
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            auto& entity = oop_entities[i];
            for (auto& base_ptr : entity) {
                base_ptr->update(); // 虚拟多态调用
                object_count++;
            }
        }
        do_not_optimize(object_count);
        std::cout << "     (Total elements iterated: " << object_count << ")\n";
    }

    std::cout << "==================================================================" << std::endl;
    return 0;
}
