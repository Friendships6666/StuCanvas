#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <random>
#include <algorithm>
#include <iomanip>

// 引入我们实现的 PinnedVector
#include "pinned_vector.hpp"

// 定义一个 16 字节对齐、64 字节大小的测试结构体
struct alignas(16) Payload {
    uint64_t data[8]{}; // 类内默认初始化为 0，解决 Clang-Tidy 成员未初始化警告

    Payload() = default; // 使用编译器默认生成的构造函数

    explicit Payload(uint64_t val) {
        // 采用 C++20 ranges::fill 提高现代 C++ 代码规范
        std::ranges::fill(data, val);
    }
};

// 阻止编译器优化掉未使用的计算结果
// 修复：使用标准 const T& 并静态强转至 const volatile void*，防止万能引用折叠为非法的“指向引用的指针”
template <typename T>
void do_not_optimize(const T& val) {
    const volatile void* p = static_cast<const volatile void*>(&val);
    (void)p;
}

// 计时辅助工具
struct Timer {
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
    Timer(std::string n) : name(std::move(n)), start(std::chrono::high_resolution_clock::now()) {}
    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start).count();
        // 修复：修复 setsetprecision 拼写错误
        std::cout << std::left << std::setw(40) << name << " : "
                  << std::right << std::setw(10) << std::fixed << std::setprecision(3)
                  << duration << " ms" << std::endl;
    }
};

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "  PinnedVector vs std::vector Benchmark  " << std::endl;
    std::cout << "=========================================" << std::endl;

    std::cout << "sizeof(std::vector<Payload>) : " << sizeof(std::vector<Payload>) << " bytes" << std::endl;
    std::cout << "sizeof(PinnedVector<Payload, 2>) : " << sizeof(PinnedVector<Payload, 2>) << " bytes" << std::endl;
    std::cout << "-----------------------------------------" << std::endl;

    const size_t N = 5000000; // 500 万个元素
    std::cout << "Elements count: " << N << " (" << (N * sizeof(Payload)) / (1024 * 1024) << " MB)" << std::endl;
    std::cout << "-----------------------------------------" << std::endl;

    // 修复：使用 std::random_device 解决 Tidy 常数种子不安全警告
    std::random_device rd;
    std::mt19937 g(rd());
    std::vector<size_t> random_indices(N);
    std::iota(random_indices.begin(), random_indices.end(), 0);
    std::shuffle(random_indices.begin(), random_indices.end(), g);

    // ==========================================
    // 测试 1：动态扩容写入性能 (无预留)
    // ==========================================
    std::cout << "\n[1] Dynamic Write / Push-Back Test (No Reserve):" << std::endl;
    {
        Timer t("std::vector (No Reserve) - Emplace-Back");
        std::vector<Payload> vec;
        for (size_t i = 0; i < N; ++i) {
            // 修复：使用 emplace_back 替代 push_back
            vec.emplace_back(i);
        }
        do_not_optimize(vec[0]);
    }
    {
        Timer t("PinnedVector (No Reserve) - Emplace-Back");
        PinnedVector<Payload, 128> pvec;
        for (size_t i = 0; i < N; ++i) {
            // 修复：使用 emplace_back 替代 push_back
            pvec.emplace_back(i);
        }
        do_not_optimize(pvec[0]);
    }

    // ==========================================
    // 测试 2：顺序读写性能
    // ==========================================
    std::cout << "\n[2] Sequential Read/Write Test:" << std::endl;
    {
        std::vector<Payload> vec(N);
        Timer t("std::vector - Seq Read & Modify");
        uint64_t sum = 0;
        for (size_t i = 0; i < N; ++i) {
            vec[i].data[0] += i;
            sum += vec[i].data[0];
        }
        do_not_optimize(sum);
    }
    {
        PinnedVector<Payload, 128> pvec;
        pvec.resize(N);
        Timer t("PinnedVector - Seq Read & Modify");
        uint64_t sum = 0;
        for (size_t i = 0; i < N; ++i) {
            pvec[i].data[0] += i;
            sum += pvec[i].data[0];
        }
        do_not_optimize(sum);
    }

    // ==========================================
    // 测试 3：随机访问性能
    // ==========================================
    std::cout << "\n[3] Random Access Test (Pre-shuffled Indices):" << std::endl;
    {
        std::vector<Payload> vec(N);
        Timer t("std::vector - Random Read & Modify");
        uint64_t sum = 0;
        for (size_t i = 0; i < N; ++i) {
            size_t idx = random_indices[i];
            vec[idx].data[0] += i;
            sum += vec[idx].data[0];
        }
        do_not_optimize(sum);
    }
    {
        PinnedVector<Payload, 128> pvec;
        pvec.resize(N);
        Timer t("PinnedVector - Random Read & Modify");
        uint64_t sum = 0;
        for (size_t i = 0; i < N; ++i) {
            size_t idx = random_indices[i];
            pvec[idx].data[0] += i;
            sum += pvec[idx].data[0];
        }
        do_not_optimize(sum);
    }

    // ==========================================
    // 测试 4：虚拟内存瞬间大容量预留
    // ==========================================
    std::cout << "\n[4] Giant Capacity Reserve Test:" << std::endl;

    const size_t GiantSize = (16ULL * 1024 * 1024 * 1024) / sizeof(Payload);
    std::cout << "Attempting to reserve 16 GB of virtual storage..." << std::endl;

    {
        Timer t("PinnedVector - Reserve 16 GB Virtual Space");
        PinnedVector<Payload, 16> pvec;
        do_not_optimize(pvec.capacity());
    }

    try {
        Timer t("std::vector - Reserve 16 GB Real Memory");
        std::vector<Payload> vec;
        vec.reserve(GiantSize);
        do_not_optimize(vec.capacity());
    } catch (const std::bad_alloc&) { // 修复：省去不必要的局部变量 e
        std::cout << "std::vector - Reserve 16 GB failed with std::bad_alloc (expected)" << std::endl;
    }

    std::cout << "=========================================" << std::endl;
    return 0;
}