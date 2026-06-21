/*
 * Copyright (c) StuCanvas, 2026
 * Test suite for 2D Implicit Curve Tracer with side-effect output saving.
 */
#include "../stucanvas/utils/function.hpp"
#include "../stucanvas/plot/implicit_2d_scalar.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <iomanip>

// 1. 定义隐式代数曲线方程：圆形 x^2 + y^2 - 4.0 = 0
double circle_equation(double x, double y) {
    return x * x + y * y - 4.0;
}

int main() {
    using T = double;

    // 2. 将数学公式封装为 Move-Only 的 FfiFunction
    StuCanvas::utils::FfiFunction<T(T, T)> ffi_circle(circle_equation);

    // 3. 准备接收点数据的向量
    std::vector<StuCanvas::plot::Point2D<T>> points;

    // 设定 2D 世界搜索边界范围
    T x_min = -3.0, x_max = 3.0;
    T y_min = -3.0, y_max = 3.0;

    // 粗粒度主网格划分尺寸
    size_t M = 12; // 横向划分为 12 块
    size_t N = 12; // 纵向划分为 12 块

    // 终止细分的最小块物理尺寸（决定了最终绘图的分辨率）
    T min_block_width = 0.02;
    T min_block_height = 0.02;

    // 抢占式提前退出精度与容错条件
    size_t exit_decimal_places = 15;  // 自变量逼近至双精度下溢区 (约 15 位有效数字) 视为有根
    T exit_value_low = 0.0;
    T exit_value_high = 1e-4;         // |f(x, y)| 的绝对值降入 [0.0, 1e-4] 范围内视为有根

    unsigned int threads = 0; // 0 代表自动分配 Intel oneTBB 的全部空闲线程

    std::cout << "正在启动双重 TBB 并行的自适应四叉树隐式曲线追踪..." << std::endl;
    std::cout << "世界空间边界: [" << x_min << ", " << x_max << "] x [" << y_min << ", " << y_max << "]" << std::endl;
    std::cout << "最小细分叶子分辨率: " << min_block_width << " x " << min_block_height << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    // 4. 调用隐式曲线追踪接口（传入 vector 指针，利用副作用回收所有解点）
    StuCanvas::plot::implicit_2d_scalar(
        ffi_circle,
        x_min, x_max,
        y_min, y_max,
        M, N,
        min_block_width, min_block_height,
        exit_decimal_places,
        exit_value_low, exit_value_high,
        threads,
        &points
    );

    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "追踪完成！" << std::endl;
    std::cout << "  - 耗时: " << elapsed_ms << " ms" << std::endl;
    std::cout << "  - 共捕获到合法根交点数量: " << points.size() << " 个" << std::endl;

    // 5. 将点数据保存为 txt 文件，格式为 x y，一行一组
    const std::string filename = "points.txt";
    std::ofstream outfile(filename);
    if (outfile.is_open()) {
        // 设置极高精度输出，防止截断带来的物理点位置变形
        outfile << std::fixed << std::setprecision(10);
        for (const auto& pt : points) {
            outfile << pt.x << " " << pt.y << "\n";
        }
        outfile.close();
        std::cout << "成功将点集以 'x y' 格式保存至: " << filename << std::endl;
    } else {
        std::cerr << "错误：无法打开 " << filename << " 进行写入操作。" << std::endl;
        return 1;
    }

    return 0;
}