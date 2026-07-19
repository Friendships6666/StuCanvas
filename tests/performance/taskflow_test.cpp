#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// Taskflow 头文件
#include <taskflow/taskflow.hpp>

// oneTBB 头文件
#include <tbb/flow_graph.h>
#include <tbb/global_control.h>

// 模拟密集计算任务
void heavy_compute(int iterations) {
  volatile double val = 1234.56;
  for (int i = 0; i < iterations; ++i) {
    val = std::sin(val) * std::cos(val) + std::sqrt(val + 1.0);
  }
}
// 运行 Taskflow 测试
void run_taskflow_test(int width, int iterations, int num_threads) {
  tf::Executor executor(num_threads);
  tf::Taskflow taskflow;

  auto root = taskflow.emplace([]() { heavy_compute(10); });
  auto sink = taskflow.emplace([]() { heavy_compute(10); });

  std::vector<tf::Task> inner;
  inner.reserve(width);
  for (int i = 0; i < width; ++i) {
    auto t = taskflow.emplace([iterations]() { heavy_compute(iterations); });
    root.precede(t);
    t.precede(sink);
  }

  auto start = std::chrono::high_resolution_clock::now();
  executor.run(taskflow).wait();
  auto end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "Taskflow 执行耗时: " << elapsed.count() << " ms\n";
}

// 运行 oneTBB 测试
void run_onetbb_test(int width, int iterations, int num_threads) {
  // 强制限制 oneTBB 的最大并行度，使其与 Taskflow 的线程数完全一致
  tbb::global_control limit(tbb::global_control::max_allowed_parallelism,
                            num_threads);
  tbb::flow::graph g;

  using namespace tbb::flow;
  continue_node<continue_msg> root(
      g, [](const continue_msg &) { heavy_compute(10); });
  continue_node<continue_msg> sink(
      g, [](const continue_msg &) { heavy_compute(10); });

  std::vector<std::unique_ptr<continue_node<continue_msg>>> inner;
  inner.reserve(width);
  for (int i = 0; i < width; ++i) {
    inner.push_back(std::make_unique<continue_node<continue_msg>>(
        g, [iterations](const continue_msg &) { heavy_compute(iterations); }));
    make_edge(root, *inner.back());
    make_edge(*inner.back(), sink);
  }

  auto start = std::chrono::high_resolution_clock::now();
  root.try_put(continue_msg());
  g.wait_for_all();
  auto end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "oneTBB   执行耗时: " << elapsed.count() << " ms\n";
}

int main() {
  const int num_threads = std::thread::hardware_concurrency();
  std::cout << "当前 CPU 逻辑核心数: " << num_threads << "\n\n";

  // 场景 A：1,000个并行重度任务（测试多核负载调度）
  {
    const int width = 1000;
    const int iterations = 500000; // 每个子任务迭代50万次
    std::cout << "=== 场景 A (重型计算，测试多核吞吐) ===\n";
    std::cout << "并行宽度: " << width << ", 单个任务计算强度: " << iterations
              << "\n";

    // 运行热身（排查冷启动干扰）
    run_taskflow_test(10, 10, num_threads);
    run_onetbb_test(10, 10, num_threads);

    // 正式测试
    run_taskflow_test(width, iterations, num_threads);
    run_onetbb_test(width, iterations, num_threads);
    std::cout << "\n";
  }

  // 场景 B：50,000个并行极轻任务（测试框架固有调度开销）
  {
    const int width = 50000;
    const int iterations = 10; // 几乎不计算，直接返回
    std::cout << "=== 场景 B (极轻计算，测试调度器空转开销) ===\n";
    std::cout << "并行宽度: " << width << ", 单个任务计算强度: " << iterations
              << "\n";

    run_taskflow_test(width, iterations, num_threads);
    run_onetbb_test(width, iterations, num_threads);
    std::cout << "\n";
  }

  return 0;
}
