#include <iostream>
#include <chrono>
#include <array>
#include "stucanvas/cache/macros.hpp"

// 模拟一个复杂的 POD 结构体
struct PodData {
    float  values[1024];
    double centroid[3];
    int    id;
};

// 全局计数器
int pod_compute_count = 0;

// 模拟耗时计算（用 std::array 替代 std::vector）
void heavy_pod_compute(float seed, PodData& out_data, std::array<float, 3>& out_arr) {
    ++pod_compute_count;

    for (size_t i = 0; i < 1024; ++i) {
        out_data.values[i] = seed * static_cast<float>(i) * 0.001f;
    }
    out_data.centroid[0] = seed;
    out_data.centroid[1] = seed * 2.0;
    out_data.centroid[2] = seed * 3.0;
    out_data.id = static_cast<int>(seed * 100);

    out_arr = {seed, seed * 0.5f, seed * 2.0f};
}

int main() {
    // ---------- 第一次运行（无缓存）----------
    {
        PodData data;
        std::array<float, 3> arr;

        auto t1 = std::chrono::high_resolution_clock::now();

        STU_CACHE_BLOCK("pod_test", STU_IN(3.14f), STU_OUT(data, arr))
        {
            heavy_pod_compute(3.14f, data, arr);
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

        std::cout << "[First run]  compute_count = " << pod_compute_count
                  << " | time = " << duration << " us\n";
        std::cout << "  data.values[0]: " << data.values[0]
                  << "  data.id: " << data.id
                  << "  arr[0]: " << arr[0] << "\n\n";
    }

    // ---------- 第二次运行（缓存命中）----------
    {
        PodData data2;
        std::array<float, 3> arr2;

        auto t1 = std::chrono::high_resolution_clock::now();

        STU_CACHE_BLOCK("pod_test", STU_IN(3.24f), STU_OUT(data2, arr2))
        {
            heavy_pod_compute(3.24f, data2, arr2);
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

        std::cout << "[Second run] compute_count = " << pod_compute_count
                  << " | time = " << duration << " us\n";
        std::cout << "  data2.values[0]: " << data2.values[0]
                  << "  data2.id: " << data2.id
                  << "  arr2[0]: " << arr2[0] << "\n\n";
    }

    return 0;
}