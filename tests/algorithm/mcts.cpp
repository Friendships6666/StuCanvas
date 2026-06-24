#include <iostream>
#include <cmath>
#include <random>
#include <vector>
#include "imcts/regressor.hpp" // 引入 C++ 核心 Regressor 头文件

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "🚀 正在运行纯 C++20 环境下 MCTS 符号回归 (y - ln(x) 拟合)" << std::endl;
    std::cout << "=========================================================" << std::endl;

    const int n_samples = 150;

    // 💡 修正 1：改用 std::vector 声明输入特征（2个变量，每个变量包含 n_samples 个采样值）
    std::vector<float> x0(n_samples); // 对应变量 x
    std::vector<float> x1(n_samples); // 对应变量 y

    // 💡 修正 2：改用 std::vector<float> 声明一维目标响应 Z
    std::vector<float> Z(n_samples);

    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dist_x0(0.1, 20.0f);   // 变量 x0 必须大于0
    std::uniform_real_distribution<float> dist_x1(-15.0f, -10.0f); // 变量 x1

    for (int i = 0; i < n_samples; ++i) {
        float x0_val = dist_x0(gen);
        float x1_val = dist_x1(gen);

        x0[i] = x0_val;
        x1[i] = x1_val;

        Z[i] = x1_val - log(x0_val) / (x0_val -1 ); // 计算 z = x1 - ln(x0)
    }

    // 将 x0 和 x1 组合成二维嵌套向量作为最终输入
    std::vector<std::vector<float>> X = { x0, x1 };

    // 2. 配置 MCTS 符号回归参数
    imcts::RegressorConfig cfg;
    cfg.ops = {"+", "-", "*", "/", "log", "R","sin","cos"}; // 允许的算子集合
    cfg.max_depth = 5;
    cfg.K = 400;
    cfg.max_evals = 50000;
    cfg.succ_error_tol = 1e-6;

    // 3. 实例化 Regressor 并进行搜索（此时类型完美对齐，编译错误消除）
    imcts::Regressor regressor(X, Z, cfg);
    auto result = regressor.fit(42); // 传入随机种子

    // 4. 打印拟合出的解析公式
    std::cout << "\n=== 纯 C++ 符号回归结果 ===" << std::endl;
    std::cout << "最大奖励值 (Best Reward): " << result.best_reward << std::endl;
    std::cout << "重构的解析公式 (Expression): " << result.expression << std::endl;
    std::cout << "总黑盒评估次数 (Evaluations): " << result.n_evals << std::endl;

    return 0;
}