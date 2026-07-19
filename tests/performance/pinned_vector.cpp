#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <random>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <concepts>

// 引入实现的 PinnedVector
#include "pinned_vector.hpp"

// 定义一个 16 字节对齐、64 字节大小的测试结构体
struct alignas(16) Payload {
    uint64_t data[8]{}; 

    Payload() = default; 

    explicit Payload(uint64_t val) {
        std::ranges::fill(data, val);
    }
};

// 采用更强力的编译器屏障，防止 release 模式下整个循环被优化掉
template <typename T>
void do_not_optimize(const T& val) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(&val) : "memory");
#else
    const volatile void* p = static_cast<const volatile void*>(&val);
    (void)p;
#endif
}

// 计时辅助工具
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

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "        Enhanced PinnedVector vs std::vector Benchmark     " << std::endl;
    std::cout << "==========================================================" << std::endl;

    std::cout << "sizeof(std::vector<Payload>) : " << sizeof(std::vector<Payload>) << " bytes" << std::endl;
    std::cout << "sizeof(PinnedVector<Payload, 2>) : " << sizeof(PinnedVector<Payload, 2>) << " bytes" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;

    const size_t N = 5000000; // 500 万个元素
    std::cout << "Elements count: " << N << " (" << (N * sizeof(Payload)) / (1024 * 1024) << " MB)" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;

    std::random_device rd;
    std::mt19937 g(rd());
    std::vector<size_t> random_indices(N);
    std::iota(random_indices.begin(), random_indices.end(), 0);
    std::shuffle(random_indices.begin(), random_indices.end(), g);

    // ==========================================
    // 测试 1：动态扩容写入性能 (无预留 vs 有预留)
    // ==========================================
    std::cout << "\n[1] Dynamic Write / Push-Back Test:" << std::endl;
    {
        Timer t("std::vector (No Reserve) - Emplace-Back");
        std::vector<Payload> vec;
        for (size_t i = 0; i < N; ++i) vec.emplace_back(i);
        do_not_optimize(vec[0]);
    }
    {
        Timer t("PinnedVector (No Reserve) - Emplace-Back");
        PinnedVector<Payload, 128> pvec;
        for (size_t i = 0; i < N; ++i) pvec.emplace_back(i);
        do_not_optimize(pvec[0]);
    }
    std::cout << "  -- With Fair Reserve (公平预留对比) --" << std::endl;
    {
        Timer t("std::vector (With Reserve) - Emplace-Back");
        std::vector<Payload> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) vec.emplace_back(i);
        do_not_optimize(vec[0]);
    }
    {
        Timer t("PinnedVector (With Reserve) - Emplace-Back");
        PinnedVector<Payload, 128> pvec;
        pvec.reserve(N);
        for (size_t i = 0; i < N; ++i) pvec.emplace_back(i);
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
    // 测试 4：密集创建微型容器（内存开销 & 碎片测试）
    // ==========================================
    std::cout << "\n[4] Massive Micro-Containers Test (Allocation Latency):" << std::endl;
    const size_t NumSmall = 5000; // 5000 个微型容器，每个只装 1 个元素
    std::cout << "  Creating " << NumSmall << " containers of size 1 (testing allocator vs VM syscalls)..." << std::endl;
    {
        Timer t("std::vector - Create 5k micro-vectors");
        std::vector<std::vector<Payload>> vec_of_vecs;
        vec_of_vecs.reserve(NumSmall);
        for (size_t i = 0; i < NumSmall; ++i) {
            vec_of_vecs.emplace_back();
            vec_of_vecs.back().emplace_back(1);
        }
        do_not_optimize(vec_of_vecs);
    }
    {
        Timer t("PinnedVector - Create 5k micro-vectors");
        std::vector<PinnedVector<Payload, 1>> vec_of_pvecs;
        vec_of_pvecs.reserve(NumSmall);
        for (size_t i = 0; i < NumSmall; ++i) {
            vec_of_pvecs.emplace_back();
            vec_of_pvecs.back().emplace_back(1);
        }
        do_not_optimize(vec_of_pvecs);
    }
    std::cout << "  *Note: PinnedVector spent 5000x System Call page allocations." << std::endl;
    std::cout << "  *Est. physical RAM wasted: std::vector ~320 KB vs PinnedVector ~20 MB (due to 4KB page alignment)." << std::endl;

    // ==========================================
    // 测试 5：并发扩容性能测试（内核页表锁竞争）
    // ==========================================
    std::cout << "\n[5] Concurrent Multi-Threaded Growth Test:" << std::endl;
    const int num_threads = 4;
    const size_t ThreadN = 1000000;
    std::cout << "  Running 4 concurrent threads, each dynamically populating 1,000,000 elements..." << std::endl;
    {
        Timer t("std::vector - 4-Thread Concurrent Growth");
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&]() {
                std::vector<Payload> local_vec;
                for (size_t j = 0; j < ThreadN; ++j) {
                    local_vec.emplace_back(j);
                }
                do_not_optimize(local_vec[0]);
            });
        }
        for (auto& th : threads) th.join();
    }
    {
        Timer t("PinnedVector - 4-Thread Concurrent Growth");
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&]() {
                PinnedVector<Payload, 2> local_pvec;
                for (size_t j = 0; j < ThreadN; ++j) {
                    local_pvec.emplace_back(j);
                }
                do_not_optimize(local_pvec[0]);
            });
        }
        for (auto& th : threads) th.join();
    }

    // ==========================================
    // 测试 6：析构与物理内存回收性能
    // ==========================================
    std::cout << "\n[6] Cleanup and Decommit Test:" << std::endl;
    {
        std::vector<Payload> vec(N);
        Timer t("std::vector - clear() & shrink_to_fit()");
        vec.clear();
        vec.shrink_to_fit();
        do_not_optimize(vec.capacity());
    }
    {
        PinnedVector<Payload, 128> pvec;
        pvec.resize(N);
        Timer t("PinnedVector - clear() & shrink_to_fit() (Decommit)");
        pvec.clear();
        pvec.shrink_to_fit();
        do_not_optimize(pvec.capacity());
    }

    // ==========================================
    // 测试 7：虚拟内存瞬间大容量预留（原测试 4）
    // ==========================================
    std::cout << "\n[7] Giant Capacity Reserve Test:" << std::endl;

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
    } catch (const std::bad_alloc&) {
        std::cout << "  std::vector - Reserve 16 GB failed with std::bad_alloc (expected)" << std::endl;
    }

    std::cout << "==========================================================" << std::endl;
    return 0;
}
