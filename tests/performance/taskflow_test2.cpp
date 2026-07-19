#include <iostream>
#include <chrono>
#include <vector>
#include <cmath>
#include <numeric>
#include <thread>
#include <algorithm>
#include <memory>

// Taskflow 头文件
#include <taskflow/taskflow.hpp>
// 修正 1：必须显式引入并行循环算法头文件
#include <taskflow/algorithm/for_each.hpp>

// oneTBB 头文件
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/global_control.h>

// 重设数据，保证每次测试输入完全一致
void reset_data(std::vector<double>& data) {
    std::iota(data.begin(), data.end(), 1.0);
}

// 场景 1：重度计算（Compute-Bound）
void compute_heavy(double& val) {
    val = std::sin(val) * std::cos(val) + std::sqrt(std::abs(val) + 1.0);
}

// 场景 2：轻度计算/内存受限（Memory-Bound）
void compute_light(double& val) {
    val = val * 1.01 + 0.5;
}

// 场景 3：不均衡负载（Unbalanced Workload）
void compute_unbalanced(double& val, size_t index) {
    int iterations = (index % 100 == 0) ? 1000 : 1;
    for (int i = 0; i < iterations; ++i) {
        val = std::sin(val) * std::cos(val) + std::sqrt(std::abs(val) + 1.0);
    }
}

// ==================== Taskflow 测试函数 ====================
void test_taskflow_heavy(std::vector<double>& data, int num_threads) {
    tf::Executor executor(num_threads);
    tf::Taskflow taskflow;

    // 修正 2：在 taskflow 对象上构建图，而不是 executor
    taskflow.for_each(data.begin(), data.end(), [](double& val) {
        compute_heavy(val);
    });

    auto start = std::chrono::high_resolution_clock::now();
    executor.run(taskflow).wait(); // 提交给 executor 运行并等待完成
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Taskflow Loop (Heavy): " << elapsed.count() << " ms\n";
}

void test_taskflow_light(std::vector<double>& data, int num_threads) {
    tf::Executor executor(num_threads);
    tf::Taskflow taskflow;

    taskflow.for_each(data.begin(), data.end(), [](double& val) {
        compute_light(val);
    });

    auto start = std::chrono::high_resolution_clock::now();
    executor.run(taskflow).wait();
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Taskflow Loop (Light): " << elapsed.count() << " ms\n";
}

void test_taskflow_unbalanced(std::vector<double>& data, int num_threads) {
    tf::Executor executor(num_threads);
    tf::Taskflow taskflow;

    taskflow.for_each_index(size_t{0}, data.size(), size_t{1}, [&data](size_t i) {
        compute_unbalanced(data[i], i);
    });

    auto start = std::chrono::high_resolution_clock::now();
    executor.run(taskflow).wait();
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Taskflow Loop (Unbalanced): " << elapsed.count() << " ms\n";
}

// ==================== oneTBB 测试函数 ====================
void test_onetbb_heavy(std::vector<double>& data, int num_threads) {
    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, num_threads);
    auto start = std::chrono::high_resolution_clock::now();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, data.size()), [&](const tbb::blocked_range<size_t>& r) {
        for (size_t i = r.begin(); i != r.end(); ++i) {
            compute_heavy(data[i]);
        }
    });

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "oneTBB Loop   (Heavy): " << elapsed.count() << " ms\n";
}

void test_onetbb_light(std::vector<double>& data, int num_threads) {
    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, num_threads);
    auto start = std::chrono::high_resolution_clock::now();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, data.size()), [&](const tbb::blocked_range<size_t>& r) {
        for (size_t i = r.begin(); i != r.end(); ++i) {
            compute_light(data[i]);
        }
    });

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "oneTBB Loop   (Light): " << elapsed.count() << " ms\n";
}

void test_onetbb_unbalanced(std::vector<double>& data, int num_threads) {
    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, num_threads);
    auto start = std::chrono::high_resolution_clock::now();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, data.size()), [&](const tbb::blocked_range<size_t>& r) {
        for (size_t i = r.begin(); i != r.end(); ++i) {
            compute_unbalanced(data[i], i);
        }
    }, tbb::auto_partitioner());

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "oneTBB Loop   (Unbalanced): " << elapsed.count() << " ms\n";
}

int main() {
    const int num_threads = std::thread::hardware_concurrency();
    std::cout << "检测到 CPU 逻辑核心数: " << num_threads << "\n\n";

    // 运行一次微型热身
    {
        std::vector<double> warm_data(100);
        test_taskflow_heavy(warm_data, num_threads);
        test_onetbb_heavy(warm_data, num_threads);
        std::cout << "\n";
    }

    // 场景 1：200 万元素，重度计算
    {
        std::vector<double> data(2000000);
        std::cout << "=== 场景 1 (2,000,000 元素 - 重度计算密集型) ===\n";

        reset_data(data);
        test_taskflow_heavy(data, num_threads);

        reset_data(data);
        test_onetbb_heavy(data, num_threads);
        std::cout << "\n";
    }

    // 场景 2：5000 万元素，轻度计算（测试内存带宽）
    {
        std::vector<double> data(50000000);
        std::cout << "=== 场景 2 (50,000,000 元素 - 轻度计算/内存带宽限制) ===\n";
        
        reset_data(data);
        test_taskflow_light(data, num_threads);
        
        reset_data(data);
        test_onetbb_light(data, num_threads);
        std::cout << "\n";
    }

    // 场景 3：1000 万元素，极度不均衡负载
    {
        std::vector<double> data(10000000);
        std::cout << "=== 场景 3 (10,000,000 元素 - 极度不均衡负载) ===\n";
        
        reset_data(data);
        test_taskflow_unbalanced(data, num_threads);
        
        reset_data(data);
        test_onetbb_unbalanced(data, num_threads);
        std::cout << "\n";
    }

    return 0;
}