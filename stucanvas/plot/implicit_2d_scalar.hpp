/*
 * Copyright (c) StuCanvas, 2026
 * Adaptive Quadtree Implicit 2D Curve Tracer.
 * Starts with single-core global optimization, branches into quadtree,
 * and dynamically activates nested TBB parallel_invoke under load.
 * Added verbose, thread-safe real-time diagnostic logging.
 */
#pragma once

#include "../stucanvas/utils/function.hpp"
#include "../stucanvas/utils/l_shade.hpp"
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <concepts>
#include <iostream>   // 💡 引入控制台输入输出
#include <iomanip>    // 💡 引入格式化输出

// 💡 引入 TBB 并行调用和任务域组件
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/info.h>

namespace StuCanvas::plot {

    template <typename T>
    struct Point2D {
        T x;
        T y;
    };

    namespace detail {

        // =========================================================================
        // 💡 带有实时线程安全日志的自适应四叉树
        // =========================================================================
        template <typename T, typename Func>
        void subdivide_quadtree(
            const Func& f,
            T x1, T x2, T y1, T y2,
            T min_w, T min_h,
            size_t decimal_places,
            T val_low, T val_high,
            unsigned int max_threads,
            size_t depth,
            std::vector<Point2D<T>>* out_points,
            std::mutex& out_mutex) {

            // 💡 判定当前任务负载，深度 >= 2（存在大量潜在任务）时，开启 TBB
            constexpr size_t tbb_depth_threshold = 2;
            bool enable_tbb = (depth >= tbb_depth_threshold);
            unsigned int current_threads = enable_tbb ? max_threads : 1;

            // 💡 1. 打印：正在扫描的范围与环境信息
            {
                std::scoped_lock lock(out_mutex);
                std::cout << std::scientific << std::setprecision(6);
                std::cout << "[Depth " << depth << "] 🔍 正在扫描区间: ["
                          << x1 << ", " << x2 << "] x [" << y1 << ", " << y2 << "]"
                          << " (分配线程: " << current_threads << ")" << std::endl;
            }

            // 配置 L-SHADE 超参数
            utils::optimization::l_shade_parameters<T, 2> params;
            params.lower_bounds = {x1, y1};
            params.upper_bounds = {x2, y2};

            // 如果是第一层（对整个大世界范围进行全局扫描），采用大种群与高代数防止漏根
            if (depth == 0) {
                params.NP_init = 150;
                params.max_evaluations = 15000;
            } else {
                params.NP_init = 25;
                params.max_evaluations = 15000;
            }
            params.NP_min = 16;
            params.threads = current_threads;
            params.seed = 0;

            // 开启提前退出
            params.enable_early_exit = true;
            params.early_exit_value_low = val_low;
            params.early_exit_value_high = val_high;
            params.early_exit_decimal_places = decimal_places;

            // 目标函数：|f(x, y)|
            auto cost_func = [&](T x, T y) -> T {
                return std::abs(f(x, y));
            };

            // 2. 运行 L-SHADE 寻找极小值
            std::array<T, 2> best_solution = utils::optimization::l_shade(cost_func, params);

            T best_x = best_solution[0];
            T best_y = best_solution[1];
            T final_cost = std::abs(f(best_x, best_y));

            // 判定当前区间是否存在根
            bool has_root = (final_cost >= val_low && final_cost <= val_high);
            bool is_subnormal_trigger = false;

            // 如果不在范围内，检测精度位数是否达到极限（疑似奇异点/渐近线）
            if (!has_root) {
                T coord_threshold = std::pow(static_cast<T>(10.0), -static_cast<T>(decimal_places));
                T dist_x = std::min(best_x - x1, x2 - best_x);
                T dist_y = std::min(best_y - y1, y2 - best_y);

                if ((dist_x > static_cast<T>(0.0) && dist_x < coord_threshold) ||
                    (dist_y > static_cast<T>(0.0) && dist_y < coord_threshold)) {
                    has_root = true;
                    is_subnormal_trigger = true;
                }
            }

            // 💡 3. 打印：L-SHADE 寻优结果与诊断逻辑
            {
                std::scoped_lock lock(out_mutex);
                std::cout << std::scientific << std::setprecision(10);
                std::cout << "[Depth " << depth << "] 📊 优化完成 | 范围: ["
                          << x1 << ", " << x2 << "] x [" << y1 << ", " << y2 << "]\n"
                          << "        -> 最优自变量坐标 (x, y): (" << best_x << ", " << best_y << ")\n"
                          << "        -> 绝对值残差 |f(x,y)|:  " << final_cost << "\n";

                if (has_root) {
                    if (is_subnormal_trigger) {
                        std::cout << "        -> [判定结果] ⚠️ 存在根 (触发非正规数下溢边界诊断，疑似渐近线或奇点)\n";
                    } else {
                        std::cout << "        -> [判定结果] ✅ 存在根 (满足终止区间代价值要求)\n";
                    }

                    if ((x2 - x1) <= min_w && (y2 - y1) <= min_h) {
                        std::cout << "        -> [节点状态] 📍 达到叶子分辨率限，在中心处记录物理交点: ("
                                  << (x1 + x2) / 2.0 << ", " << (y1 + y2) / 2.0 << ")" << std::endl;
                    } else {
                        std::cout << "        -> [节点状态] 🌿 正在裂变为 4 个子象限进行更深细分探查..." << std::endl;
                    }
                } else {
                    std::cout << "        -> [判定结果] ❌ 无根，此分支已成功剪枝 (Pruned)" << std::endl;
                }
                std::cout << std::defaultfloat; // 恢复默认打印格式
            }

            // 4. 剪枝：如果当前范围内没有任何根的迹象，直接丢弃该分支
            if (!has_root) {
                return;
            }

            // 5. 达到最小细分分辨率，将中心位置写入结果并退出
            if ((x2 - x1) <= min_w && (y2 - y1) <= min_h) {
                std::scoped_lock lock(out_mutex);
                out_points->push_back(Point2D<T>{ (x1 + x2) / static_cast<T>(2.0), (y1 + y2) / static_cast<T>(2.0) });
                return;
            }

            // 6. 四叉树分裂
            T cx = (x1 + x2) / static_cast<T>(2.0);
            T cy = (y1 + y2) / static_cast<T>(2.0);

            // 7. 并行策略切换：当深度较深（细分任务极多）时，开启 TBB 双重并行
            if (enable_tbb) {
                oneapi::tbb::parallel_invoke(
                    [&]() { subdivide_quadtree(f, x1, cx, y1, cy, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex); },
                    [&]() { subdivide_quadtree(f, cx, x2, y1, cy, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex); },
                    [&]() { subdivide_quadtree(f, x1, cx, cy, y2, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex); },
                    [&]() { subdivide_quadtree(f, cx, x2, cy, y2, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex); }
                );
            } else {
                subdivide_quadtree(f, x1, cx, y1, cy, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex);
                subdivide_quadtree(f, cx, x2, y1, cy, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex);
                subdivide_quadtree(f, x1, cx, cy, y2, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex);
                subdivide_quadtree(f, cx, x2, cy, y2, min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, out_points, out_mutex);
            }
        }

    } // namespace detail

    // =========================================================================
    // 💡 外部调用主接口 (彻底移除了外部粗分块 M、N，实现 100% 动态自适应寻优)
    // =========================================================================
    template <typename T, typename Func>
        requires std::floating_point<T>
    void implicit_2d_scalar(
        const Func& f,
        T x_min, T x_max,
        T y_min, T y_max,
        T min_block_width,
        T min_block_height,
        size_t exit_decimal_places,
        T exit_value_low,
        T exit_value_high,
        unsigned int threads,
        std::vector<Point2D<T>>* out_points) {

        if (!out_points) return;
        out_points->clear();
        std::mutex out_mutex;

        // 计算最大分配核心
        unsigned int max_threads = threads;
        if (max_threads == 0) {
            max_threads = oneapi::tbb::info::default_concurrency();
        }

        std::cout << "\n======================== [StuCanvas 实时几何诊断系统启动] ========================\n" << std::endl;

        oneapi::tbb::task_arena arena(max_threads);
        arena.execute([&]() {
            // 初始自举：对整个世界范围（depth = 0）启动单核高精度全局扫描
            detail::subdivide_quadtree(
                f,
                x_min, x_max,
                y_min, y_max,
                min_block_width,
                min_block_height,
                exit_decimal_places,
                exit_value_low,
                exit_value_high,
                max_threads,
                0, // 初始深度为 0
                out_points,
                out_mutex
            );
        });

        std::cout << "\n======================== [StuCanvas 实时几何诊断系统结束] ========================\n" << std::endl;
    }

} // namespace StuCanvas::plot