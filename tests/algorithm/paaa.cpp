#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <iomanip>
#include <Eigen/Dense>

#include "stucanvas/utils/paaa.hpp"

using namespace StuCanvas::utils;

double test_rational_func(double x, double y) {
    return (x * x + x * y + y + 1.0) / (x + y + 5.0);
}

double test_transcendental_func(double x, double y) {
    return y - std::tan(x);
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "   ⚡ StuCanvas::utils::ScatteredPaaa 算法测试套件 ⚡   " << std::endl;
    std::cout << "=========================================================" << std::endl;

    std::mt19937 gen(1337);

    // ---------------------------------------------------------------------
    // 📌 测试一: 代数有理函数精准还原
    // ---------------------------------------------------------------------
    std::cout << "\n[测试 1] 正在还原代数有理函数..." << std::endl;
    const int K1 = 150;
    Eigen::VectorXd X1(K1), Y1(K1);
    std::uniform_real_distribution<double> dist_1(-2.0, 2.0);
    for (int i = 0; i < K1; ++i) {
        X1[i] = dist_1(gen);
        Y1[i] = dist_1(gen);
    }

    FfiFunction<double(double, double)> f1([](double x, double y) {
        return test_rational_func(x, y);
    });

    BivariateBarycentricRational r1 = ScatteredPaaa::fit(f1, X1, Y1, 1e-13, 30);
    std::cout << "  > 拟合收敛结束！" << std::endl;

    double max_err_1 = 0.0;
    for (int i = 0; i < 2000; ++i) {
        double tx = dist_1(gen);
        double ty = dist_1(gen);
        max_err_1 = std::max(max_err_1, std::abs(test_rational_func(tx, ty) - r1(tx, ty)));
    }
    std::cout << std::scientific << std::setprecision(5);
    std::cout << "  > 测试点最大绝对误差: " << max_err_1 << std::endl;

    // ---------------------------------------------------------------------
    // 📌 测试二: 超越函数 y - ln(x) 拟合 (优化采样密度，压制虚假极点)
    // ---------------------------------------------------------------------
    std::cout << "\n[测试 2] 正在逼近超越函数: y - ln(x)..." << std::endl;

    // 💡 改进点：将训练集散点数提升至 1000。
    // 这能够提供极其致密的全局约束，在数学上彻底锁死 SVD 产生自由虚假极点的空间。
    const int K2 = 2000;
    Eigen::VectorXd X2(K2), Y2(K2);

    std::uniform_real_distribution<double> dist_x2(-4, 4);
    std::uniform_real_distribution<double> dist_y2(-100000005.0, -100000000.0);

    for (int i = 0; i < K2; ++i) {
        X2[i] = dist_x2(gen);
        Y2[i] = dist_y2(gen);
    }

    FfiFunction<double(double, double)> f2([](double x, double y) {
        return test_transcendental_func(x, y);
    });

    // 容差设为 1e-6，允许其生长出更多阶数以逼近对数边界
    BivariateBarycentricRational r2 = ScatteredPaaa::fit(f2, X2, Y2, 1e-6, 50);
    std::cout << "  > 拟合收敛结束！" << std::endl;
    std::cout << "  > 自适应提取的 x 方向节点 (lambda) 数: " << r2.lambda.size() << std::endl;
    std::cout << "  > 自适应提取的 y 方向节点 (mu) 数:     " << r2.mu.size() << std::endl;

    double max_err_2 = 0.0;
    for (int i = 0; i < 2000; ++i) {
        double tx = dist_x2(gen);
        double ty = dist_y2(gen);
        max_err_2 = std::max(max_err_2, std::abs(test_transcendental_func(tx, ty) - r2(tx, ty)));
    }

    std::cout << "  > 2000 个随机渐近线测试点上的最大绝对误差: " << max_err_2 << std::endl;

    return 0;
}