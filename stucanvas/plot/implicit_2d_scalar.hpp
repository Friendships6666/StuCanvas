/*
 * Copyright (c) StuCanvas, 2026
 * Double-parallelized Implicit 2D Scalar Curve Tracer using L-SHADE.
 */
#pragma once

#include "../stucanvas/utils/function.hpp"
#include "../stucanvas/utils/l_shade.hpp"
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <concepts>

// 💡 引入 oneTBB 二维范围与任务域头文件
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range2d.h>
#include <oneapi/tbb/task_arena.h>

namespace StuCanvas::plot {

    // =========================================================================
    // 💡 2D 坐标点结构体定义
    // =========================================================================
    template <typename T>
    struct Point2D {
        T x;
        T y;
    };

    namespace detail {

        // =========================================================================
        // 💡 四叉树自适应递归寻根函数
        // =========================================================================
        template <typename T, typename Func>
        void subdivide_quadtree(
            const Func& f,
            T x1, T x2, T y1, T y2,
            T min_w, T min_h,
            size_t decimal_places,
            T val_low, T val_high,
            unsigned int threads,
            std::vector<Point2D<T>>* out_points,
            std::mutex& out_mutex) {

            // 1. 在当前局部块 [x1, x2] x [y1, y2] 上运行 L-SHADE 寻找 |f(x,y)| 的最小值
            utils::optimization::l_shade_parameters<T, 2> params;
            params.lower_bounds = {x1, y1};
            params.upper_bounds = {x2, y2};

            // 局部细分无需过大种群，30 个体可兼顾速度与精度
            params.NP_init = 30;
            params.NP_min = 4;
            params.max_evaluations = 800; // 限制单块最大评估次数，防止深度陷入
            params.threads = threads;     // 传递核心数，触发嵌套双重并行
            params.seed = 0;              // 自动熵源种子

            // 配置抢占式提前退出条件
            params.enable_early_exit = true;
            params.early_exit_value_low = val_low;
            params.early_exit_value_high = val_high;
            params.early_exit_decimal_places = decimal_places;

            // 实例化随机发生器（使用 L-SHADE 的自闭环 PRNG）
            std::random_device rd;
            std::mt19937 gen(rd());

            // 寻优目标：最小化距离绝对值 |f(x, y)|
            auto cost_func = [&](T x, T y) -> T {
                return std::abs(f(x, y));
            };

            // 执行 L-SHADE 优化
            std::array<T, 2> best_solution = utils::optimization::l_shade(cost_func, params);

            T best_x = best_solution[0];
            T best_y = best_solution[1];
            T final_cost = std::abs(f(best_x, best_y));

            // 💡 诊断指标 1：代价值落入用户终止区间 (存在根)
            bool has_root = (final_cost >= val_low && final_cost <= val_high);

            // 💡 诊断指标 2：值不在区间，但小数点位数达到临界值 (存在渐近线或奇点，亦视为有根)
            if (!has_root) {
                T coord_threshold = std::pow(static_cast<T>(10.0), -static_cast<T>(decimal_places));
                T dist_x = std::min(best_x - x1, x2 - best_x);
                T dist_y = std::min(best_y - y1, y2 - best_y);

                // 陷入非正规数下溢边界，拉响几何特征判定警报
                if ((dist_x > static_cast<T>(0.0) && dist_x < coord_threshold) ||
                    (dist_y > static_cast<T>(0.0) && dist_y < coord_threshold)) {
                    has_root = true;
                }
            }

            // 2. 剪枝决策
            if (!has_root) {
                return; // 此区域无交点，直接剪枝
            }

            // 3. 达到用户指定的最小叶子块大小，停止细分，在中心放置点
            if ((x2 - x1) <= min_w && (y2 - y1) <= min_h) {
                std::scoped_lock lock(out_mutex);
                out_points->push_back(Point2D<T>{ (x1 + x2) / static_cast<T>(2.0), (y1 + y2) / static_cast<T>(2.0) });
                return;
            }

            // 4. 未达到最小分辨率，继续执行四叉树物理细分
            T cx = (x1 + x2) / static_cast<T>(2.0);
            T cy = (y1 + y2) / static_cast<T>(2.0);

            // 递归细分四个象限
            subdivide_quadtree(f, x1, cx, y1, cy, min_w, min_h, decimal_places, val_low, val_high, threads, out_points, out_mutex);
            subdivide_quadtree(f, cx, x2, y1, cy, min_w, min_h, decimal_places, val_low, val_high, threads, out_points, out_mutex);
            subdivide_quadtree(f, x1, cx, cy, y2, min_w, min_h, decimal_places, val_low, val_high, threads, out_points, out_mutex);
            subdivide_quadtree(f, cx, x2, cy, y2, min_w, min_h, decimal_places, val_low, val_high, threads, out_points, out_mutex);
        }

    } // namespace detail

    // =========================================================================
    // 💡 4. 外部调用主接口 (双重 TBB 并行化调度)
    // =========================================================================
    template <typename T, typename Func>
        requires std::floating_point<T>
    void implicit_2d_scalar(
        const Func& f,                              // 💡 支持您的 FfiFunction 或任何双参可调用对象
        T x_min, T x_max,                           // 世界 X 范围
        T y_min, T y_max,                           // 世界 Y 范围
        size_t M,                                   // 横向网格分块数
        size_t N,                                   // 纵向网格分块数
        T min_block_width,                          // 终止细分的最小块宽度
        T min_block_height,                         // 终止细分的最小块高度
        size_t exit_decimal_places,                 // 截止小数点判定位数（用于奇点触发）
        T exit_value_low,                           // 函数优化终止范围下界
        T exit_value_high,                          // 函数优化终止范围上界
        unsigned int threads,                       // 启动的核心/线程数量 (0代表全部核心)
        std::vector<Point2D<T>>* out_points) {      // 💡 通过修改指针（副作用）返回结果

        if (!out_points) return;

        // 清空输出
        out_points->clear();
        std::mutex out_mutex;

        // 计算粗粒度主网格的长宽
        T dx = (x_max - x_min) / static_cast<T>(M);
        T dy = (y_max - y_min) / static_cast<T>(N);

        // 设置外层网格 TBB task_arena 并行度
        int outer_threads = threads;
        if (outer_threads == 0) {
            outer_threads = oneapi::tbb::info::default_concurrency();
        }
        oneapi::tbb::task_arena outer_arena(outer_threads);

        // 💡 第一重并行：在外层网格之间使用 blocked_range2d 进行并行化调度
        outer_arena.execute([&]() {
            oneapi::tbb::parallel_for(oneapi::tbb::blocked_range2d<size_t>(0, M, 0, N), [&](const oneapi::tbb::blocked_range2d<size_t>& range) {
                for (size_t i = range.rows().begin(); i < range.rows().end(); ++i) {
                    for (size_t j = range.cols().begin(); j < range.cols().end(); ++j) {
                        T x1 = x_min + static_cast<T>(i) * dx;
                        T x2 = x_min + static_cast<T>(i + 1) * dx;
                        T y1 = y_min + static_cast<T>(j) * dy;
                        T y2 = y_min + static_cast<T>(j + 1) * dy;

                        // 在每个主网格内部启动自适应四叉树寻根（内含第二重 L-SHADE 种群并行评估）
                        detail::subdivide_quadtree(
                            f, x1, x2, y1, y2,
                            min_block_width, min_block_height,
                            exit_decimal_places,
                            exit_value_low, exit_value_high,
                            threads,
                            out_points,
                            out_mutex
                        );
                    }
                }
            });
        });
    }

} // namespace StuCanvas::plot