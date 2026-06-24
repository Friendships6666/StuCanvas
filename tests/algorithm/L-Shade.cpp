#include "../stucanvas/utils/function.hpp"
#include "../stucanvas/utils/differential_evoluation.hpp"
#include "../stucanvas/utils/l_shade.hpp"
#include <minion/minion.h>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <string>

// 1. 原生多自变量标量函数定义
double f1_f2_scalar_direct(double x, double y) {
    return std::abs(1e306*y - 1e306*x);
}

double f3_scalar_direct(double x, double y) {
    return std::abs(y - std::sin(10000.0 * x));
}

// 适配 Minion 的批量评估接口
std::vector<double> f1_f2_batch_wrapper(const std::vector<std::vector<double>>& X, void* user_data) {
    std::vector<double> out(X.size());
    for (size_t i = 0; i < X.size(); ++i) {
        out[i] = f1_f2_scalar_direct(X[i][0], X[i][1]);
    }
    return out;
}

std::vector<double> f3_batch_wrapper(const std::vector<std::vector<double>>& X, void* user_data) {
    std::vector<double> out(X.size());
    for (size_t i = 0; i < X.size(); ++i) {
        out[i] = f3_scalar_direct(X[i][0], X[i][1]);
    }
    return out;
}

// 3. 对比执行逻辑
void run_comparison_test(
    const std::string& test_name,
    minion::MinionFunction minion_batch_func,
    const StuCanvas::utils::FfiFunction<double(double, double)>& ffi_scalar_func,
    const std::vector<std::pair<double, double>>& bounds,
    bool enable_early_exit_test = false) { // 💡 增加了测试参数

    std::cout << "\n==================================================================================================" << std::endl;
    std::cout << " 正在测试 (三方横向盲测、完全随机分布): " << test_name << std::endl;
    std::cout << "==================================================================================================" << std::endl;

    // ---------------------------------------------------------
    // A. 运行 Custom TBB DE
    // ---------------------------------------------------------
    StuCanvas::utils::optimization::differential_evolution_parameters<double, 2> de_params;
    de_params.lower_bounds = {bounds[0].first, bounds[1].first};
    de_params.upper_bounds = {bounds[0].second, bounds[1].second};
    de_params.NP = 150;
    de_params.max_generations = 1000;
    de_params.threads = 0;
    de_params.seed = 42;

    auto t1 = std::chrono::high_resolution_clock::now();
    std::array<double, 2> best_custom_de = StuCanvas::utils::optimization::differential_evolution(
        ffi_scalar_func,
        de_params
    );
    auto t2 = std::chrono::high_resolution_clock::now();
    double de_time = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // ---------------------------------------------------------
    // B. 运行 Custom TBB L-SHADE
    // ---------------------------------------------------------
    StuCanvas::utils::optimization::l_shade_parameters<double, 2> l_shade_params;
    l_shade_params.lower_bounds = {bounds[0].first, bounds[1].first};
    l_shade_params.upper_bounds = {bounds[0].second, bounds[1].second};
    l_shade_params.NP_init = 100;
    l_shade_params.NP_min = 4;
    l_shade_params.max_evaluations = 150000;
    l_shade_params.threads = 0;
    l_shade_params.seed = 0;
    l_shade_params.enable_early_exit = false;

    // 💡 如果是测试二，配置抢占式提前退出条件
    if (enable_early_exit_test) {
        l_shade_params.enable_early_exit = false;                  // 开启提前退出
        l_shade_params.early_exit_value_low = 99200.0;             // 条件 1: 代价值下限
        l_shade_params.early_exit_value_high = 99300.0;            // 条件 1: 代价值上限
        l_shade_params.early_exit_decimal_places = 50;             // 条件 2: 坐标距离达到 50 位小数
    }

    auto t3 = std::chrono::high_resolution_clock::now();
    std::array<double, 2> best_custom_l_shade = StuCanvas::utils::optimization::l_shade(
        ffi_scalar_func,
        l_shade_params
    );
    auto t4 = std::chrono::high_resolution_clock::now();
    double l_shade_time = std::chrono::duration<double, std::milli>(t4 - t3).count();

    // ---------------------------------------------------------
    // C. 运行 Minion LSHADE
    // ---------------------------------------------------------
    auto minion_options = minion::DefaultSettings().getDefaultSettings("LSHADE");
    minion_options["population_size"] = 150;
    minion_options["convergence_tol"] = 1e-30;

    std::vector<std::vector<double>> x0;

    minion::Minimizer optimizer(
        minion_batch_func,
        bounds,
        x0,
        nullptr,
        nullptr,
        "LSHADE",
        150000,
        42,
        minion_options
    );

    auto t5 = std::chrono::high_resolution_clock::now();
    minion::MinionResult minion_res = optimizer.optimize();
    auto t6 = std::chrono::high_resolution_clock::now();
    double minion_time = std::chrono::duration<double, std::milli>(t6 - t5).count();

    // ---------------------------------------------------------
    // D. 结果表格对比打印
    // ---------------------------------------------------------
    std::cout << std::left << std::setw(18) << "优化器名称"
              << std::setw(25) << "最佳目标函数值 (f)"
              << std::setw(18) << "最优解坐标 x"
              << std::setw(18) << "最优解坐标 y"
              << std::setw(15) << "耗时 (ms)" << std::endl;
    std::cout << "--------------------------------------------------------------------------------------------------" << std::endl;

    std::cout << std::left << std::setw(18) << "Custom TBB DE"
              << std::scientific << std::setprecision(10) << std::setw(25) << ffi_scalar_func(best_custom_de[0], best_custom_de[1])
              << std::setw(18) << best_custom_de[0]
              << std::setw(18) << best_custom_de[1]
              << std::fixed << std::setprecision(2) << std::setw(15) << de_time << std::endl;

    std::cout << std::left << std::setw(18) << "Custom TBB LSHADE"
              << std::scientific << std::setprecision(10) << std::setw(25) << ffi_scalar_func(best_custom_l_shade[0], best_custom_l_shade[1])
              << std::setw(18) << best_custom_l_shade[0]
              << std::setw(18) << best_custom_l_shade[1]
              << std::fixed << std::setprecision(2) << std::setw(15) << l_shade_time << std::endl;

    std::cout << std::left << std::setw(18) << "Minion LSHADE"
              << std::scientific << std::setprecision(10) << std::setw(25) << minion_res.fun
              << std::setw(18) << minion_res.x[0]
              << std::setw(18) << minion_res.x[1]
              << std::fixed << std::setprecision(2) << std::setw(15) << minion_time << std::endl;
}

int main() {
    StuCanvas::utils::FfiFunction<double(double, double)> ffi_f1_f2(f1_f2_scalar_direct);
    StuCanvas::utils::FfiFunction<double(double, double)> ffi_f3(f3_scalar_direct);


    // [Depth 0] ❌ 区间已被剪枝 (Pruned) | 满足条件 (y1 <= -10.0)
    //   - 物理边界范围: [-2.0000000000e+00, 2.0000000000e+00] x [-1.1000000000e+02, -1.0000000000e+02]
    //   - 最优局部坐标: (x=2.9249855168e-28, y=-1.0000000037e+02)
    //   - 绝对值最小残差: 3.6600907293e+01 (未满足退出区间 [-1.0000000000e-01, 1.0000000000e-03])
    std::vector<std::pair<double, double>> bounds_1 = {
        {-10, 30},
        {-10 ,10}
    };
    run_comparison_test(
        "测试一：奇异点附近的局部精细寻优",
        f1_f2_batch_wrapper,
        ffi_f1_f2,
        bounds_1
    );

    // 测试二：双精度下溢极限挑战
    std::vector<std::pair<double, double>> bounds_2 = {
        {0.0, 3.0},
        {100000.0, 100001.0}
    };
    // 💡 开启提前退出测试
    run_comparison_test(
        "测试二：双精度下溢极限挑战（已激活提前退出机制）",
        f1_f2_batch_wrapper,
        ffi_f1_f2,
        bounds_2,
        true
    );

    // 测试三：万级多峰超高频振荡优化
    std::vector<std::pair<double, double>> bounds_3 = {
        {10.0, 11.0},
        {10.0, 11.0}
    };
    run_comparison_test(
        "测试三：万级多峰超高频振荡优化",
        f3_batch_wrapper,
        ffi_f3,
        bounds_3
    );

    return 0;
}