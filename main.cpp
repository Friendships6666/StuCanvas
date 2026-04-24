#include <iostream>
#include <vector>
#include <chrono>
#include <random>

// 引入 StuCanvas 核心头文件
#include "include/stucanvas/types/point.hpp"
#include "include/stucanvas/cache/macros.hpp"

using namespace StuCanvas;

// 简单的计时工具
struct BenchTimer {
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
    BenchTimer(std::string n) : name(n), start(std::chrono::high_resolution_clock::now()) {}
    ~BenchTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000000.0;
        std::printf("[%s] 耗时: %.4f ms\n", name.c_str(), ms);
    }
};

// 模拟一个重型计算函数
void FindNearestPoint(const std::vector<Point3D<double>>& cloud,
                      Point3D<double> target,
                      Point3D<double>& out_result,
                      double& out_dist)
{
    double min_dist_sq = std::numeric_limits<double>::max();
    for (const auto& p : cloud) {
        double dx = p.x - target.x;
        double dy = p.y - target.y;
        double dz = p.z - target.z;
        double d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            out_result = p;
        }
    }
    out_dist = std::sqrt(min_dist_sq);
}

int main() {
    // 1. 准备数据 (1万个随机点)
    std::vector<Point3D<double>> points;
    std::mt19937 gen(42); // 固定种子保证数据一致性
    std::uniform_real_distribution<double> dist(-100, 100);
    for(int i=0; i<10000; ++i) points.push_back({dist(gen), dist(gen), dist(gen)});

    Point3D<double> query = {0.0, 0.0, 0.0};
    double input_param_id = 1.0; // 模拟某种输入 ID

    // 存储结果的变量 (必须是 POD)
    Point3D<double> best_p;
    double best_dist;

    std::cout << "--- 第一次运行 (Cold Start: 缓存缺失) ---" << std::endl;
    {
        BenchTimer t("First Run");
        // 使用宏：输入 input_param_id，输出变量 best_p 和 best_dist
        STU_CACHE_BLOCK("nearest_point_test", STU_IN(input_param_id), STU_OUT(best_p, best_dist))
        {
            std::cout << "[Logic] 缓存未命中，正在执行重型搜索算法..." << std::endl;
            FindNearestPoint(points, query, best_p, best_dist);
        }
    }
    std::printf("结果: (%.2f, %.2f, %.2f), 距离: %.4f\n\n", best_p.x, best_p.y, best_p.z, best_dist);

    std::cout << "--- 第二次运行 (Cache Hit: 命中缓存) ---" << std::endl;
    {
        BenchTimer t("Second Run");
        // 相同的 Label 和相同的 INPUT，大括号内的代码将完全不执行
        STU_CACHE_BLOCK("nearest_point_test", STU_IN(input_param_id), STU_OUT(best_p, best_dist))
        {
            std::cout << "[Logic] 你不应该看到这句话！" << std::endl;
            FindNearestPoint(points, query, best_p, best_dist);
        }
    }
    std::printf("结果: (%.2f, %.2f, %.2f), 距离: %.4f\n\n", best_p.x, best_p.y, best_p.z, best_dist);

    std::cout << "--- 第三次运行 (Parameter Changed: 缓存失效) ---" << std::endl;
    input_param_id = 2.0; // 修改输入参数
    {
        BenchTimer t("Third Run");
        STU_CACHE_BLOCK("nearest_point_test", STU_IN(input_param_id), STU_OUT(best_p, best_dist))
        {
            std::cout << "[Logic] 输入已变，重新执行计算..." << std::endl;
            FindNearestPoint(points, query, best_p, best_dist);
        }
    }

    return 0;
}