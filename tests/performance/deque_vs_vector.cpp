#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <algorithm>
#include <string>
#include <iomanip>

// 💡 请根据您实际的目录结构调整以下头文件路径
#include "stucanvas/utils/block_deque.hpp"
#include "stucanvas/utils/tiny_vector.hpp"

using namespace StuCanvas::utils;

// =========================================================================
// 💡 使用模板定义不同物理大小的对象
// =========================================================================
template <size_t FloatCount>
struct TestObject {
    uint32_t id;                        // 4 字节
    float padding[FloatCount];          // FloatCount * 4 字节
};

// 核心测试套件模板
template <size_t FloatCount>
void run_benchmark_for_size() {
    using CurrentObject = TestObject<FloatCount>;
    constexpr size_t object_size = sizeof(CurrentObject);

    std::cout << "\n==================================================================\n";
    std::cout << " 🚀 TESTING OBJECT SIZE: " << std::setw(3) << object_size << " bytes (FloatCount: " << FloatCount << ")\n";
    std::cout << "==================================================================\n";

    // 💡 规模控制在 500,000 以内，防止由于多次大内存循环导致测试耗时过长
    constexpr size_t N = 500000;

    // 1. 分配物理大池
    std::vector<CurrentObject> object_pool(N);
    for (uint32_t i = 0; i < N; ++i) {
        object_pool[i].id = i;
        object_pool[i].padding[FloatCount - 1] = static_cast<float>(i) * 0.5f; // 初始化最后一个 float
    }

    // 2. 初始化容器
    std::vector<uint32_t> std_vec_ids;
    std_vec_ids.reserve(N);

    TinyVector<uint32_t> tiny_ids;
    tiny_ids.reserve(N);

    BlockDeque<CurrentObject*, 256> block_deque_ptrs;

    for (uint32_t i = 0; i < N; ++i) {
        std_vec_ids.push_back(i);
        tiny_ids.push_back(i);
        block_deque_ptrs.push_back(&object_pool[i]);
    }

    // -------------------------------------------------------------------------
    // 测试 1：顺序读取
    // -------------------------------------------------------------------------
    std::cout << "[1. Sequential Access]\n";

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            uint32_t id = std_vec_ids[i];
            sum += object_pool[id].padding[11];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  1.1 std::vector + Pool   : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            uint32_t id = tiny_ids[i];
            sum += object_pool[id].padding[11];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  1.2 TinyVector + Pool    : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            sum += block_deque_ptrs[i]->padding[FloatCount - 1];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  1.3 BlockDeque (Derefer) : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }

    // -------------------------------------------------------------------------
    // 测试 2：随机读取
    // -------------------------------------------------------------------------
    std::cout << "[2. Random Access]\n";

    std::vector<size_t> random_indices(N);
    std::iota(random_indices.begin(), random_indices.end(), 0);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(random_indices.begin(), random_indices.end(), g);

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            uint32_t id = std_vec_ids[random_indices[i]];
            sum += object_pool[id].padding[11];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  2.1 std::vector + Pool   : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            uint32_t id = tiny_ids[random_indices[i]];
            sum += object_pool[id].padding[11];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  2.2 TinyVector + Pool    : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }

    {
        float sum = 0.0f;
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N; ++i) {
            sum += block_deque_ptrs[random_indices[i]]->padding[FloatCount - 1];
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  2.3 BlockDeque (Derefer) : " << std::setw(6) << duration << " us\n";
        volatile float sink = sum; (void)sink;
    }
}

int main() {
    // 💡 逐步增加对象体量，梯度刚好对应：
    // - 8 字节:  1个 Cache Line（64 字节）可以装 8 个对象
    // - 16 字节: 1个 Cache Line 可以装 4 个对象
    // - 32 字节: 1个 Cache Line 可以装 2 个对象
    // - 64 字节: 1个 Cache Line 只能装 1 个对象 (物理边界)
    // - 128 字节: 1个对象跨越 2 个 Cache Lines
    // - 256 字节: 1个对象跨越 4 个 Cache Lines
    // - 512 字节: 1个对象跨越 8 个 Cache Lines
    run_benchmark_for_size<1>();   // sizeof = 8 bytes
    run_benchmark_for_size<3>();   // sizeof = 16 bytes
    run_benchmark_for_size<7>();   // sizeof = 32 bytes
    run_benchmark_for_size<15>();  // sizeof = 64 bytes  <-- 💡 黄金分割点
    run_benchmark_for_size<31>();  // sizeof = 128 bytes
    run_benchmark_for_size<63>();  // sizeof = 256 bytes
    run_benchmark_for_size<127>(); // sizeof = 512 bytes

    return 0;
}