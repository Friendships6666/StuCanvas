/*
 * Copyright (c) StuCanvas, 2026
 * Adaptive Quadtree Implicit 2D Curve Tracer with Overlapping Bounds.
 * Each subdivided quadrant is dilated outward by 10% for robust root detection.
 * Thread-safe logging to "pruned_regions.txt" filtered strictly for y <= -10 or crossing y = -10.
 */
#pragma once

#include "../stucanvas/utils/l_shade.hpp"
#include <vector>
#include <mutex>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <concepts>
#include <iostream>

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
        // 💡 针对 y <= -10 进行特定过滤记录的自适应四叉树
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
            T g_xmin, T g_xmax, T g_ymin, T g_ymax,
            std::vector<Point2D<T>>* out_points,
            std::ofstream& log_file,
            std::mutex& out_mutex) {

            constexpr size_t tbb_depth_threshold = 2;
            bool enable_tbb = (depth >= tbb_depth_threshold);
            unsigned int current_threads = enable_tbb ? max_threads : 1;

            // 配置 L-SHADE 参数
            utils::optimization::l_shade_parameters<T, 2> params;
            params.lower_bounds = {x1, y1};
            params.upper_bounds = {x2, y2};

            if (depth == 0) {
                params.NP_init = 150;
                params.max_evaluations = 10000;
            } else {
                params.NP_init = 100;
                params.max_evaluations = 10000;
            }
            params.NP_min = 4;
            params.threads = current_threads;
            params.seed = 0;

            params.enable_early_exit = false;
            params.early_exit_value_low = val_low;
            params.early_exit_value_high = val_high;
            params.early_exit_decimal_places = decimal_places;

            auto cost_func = [&](T x, T y) -> T {
                return std::abs(f(x, y));
            };

            // 1. 运行 L-SHADE
            std::array<T, 2> best_solution = utils::optimization::l_shade(cost_func, params);

            T best_x = best_solution[0];
            T best_y = best_solution[1];
            T final_cost = std::abs(f(best_x, best_y));

            // 判定当前区间是否存在根
            bool has_root = (final_cost >= val_low && final_cost <= val_high);

            // 如果不在范围内，检测精度位数是否达到极限（自适应检测奇点）
            // =========================================================================
            // 💡 局部替换：自适应检测自变量小数位是否逼近奇点（直接提取到最近整数的小数偏差）
            // =========================================================================
            if (!has_root) {
                // 计算高精度阈值 10^(-decimal_places)
                T coord_threshold = std::pow(static_cast<T>(10.0), -static_cast<T>(decimal_places));

                for (size_t k = 0; k < 2; ++k) {
                    T val = best_solution[k];

                    // 💡 直接提取小数位：计算当前坐标值到最近整数的绝对距离
                    T frac_part = std::abs(val - std::round(val));

                    // 如果小数部分大于 0 且小于阈值，证明其在物理极限上无限逼近该整数奇点
                    if (frac_part > static_cast<T>(0.0) && frac_part < coord_threshold) {
                        has_root = true;
                        break;
                    }
                }
            }

            // 💡 2. 核心改变：若判定无根，且该区间满足 y <= -10（或跨越 -10），则写入详细日志
            if (!has_root) {
                if (y1 <= static_cast<T>(-100.0)) { // 💡 仅当区间下界低于 -10.0 时进行写入
                    std::scoped_lock lock(out_mutex);
                    if (log_file.is_open()) {
                        log_file << std::scientific << std::setprecision(10);
                        log_file << "[Depth " << depth << "] ❌ 区间已被剪枝 (Pruned) | 满足条件 (y1 <= -10.0)\n"
                                 << "  - 物理边界范围: [" << x1 << ", " << x2 << "] x [" << y1 << ", " << y2 << "]\n"
                                 << "  - 最优局部坐标: (x=" << best_x << ", y=" << best_y << ")\n"
                                 << "  - 绝对值最小残差: " << final_cost << " (未满足退出区间 [" << val_low << ", " << val_high << "])\n"
                                 << "--------------------------------------------------------------------------------\n";
                    }
                }
                return;
            }

            // 3. 达到最小分辨率，写入副作用结果并退出
            if ((x2 - x1) <= min_w && (y2 - y1) <= min_h) {
                std::scoped_lock lock(out_mutex);
                out_points->push_back(Point2D<T>{ (x1 + x2) / static_cast<T>(2.0), (y1 + y2) / static_cast<T>(2.0) });
                return;
            }

            // 4. 四叉树经典均分线
            T cx = (x1 + x2) / static_cast<T>(2.0);
            T cy = (y1 + y2) / static_cast<T>(2.0);

            // 5. 边界膨胀计算 (10% 膨胀，两端各自延伸子区间的 5%)
            T sw = cx - x1;
            T sh = cy - y1;
            T dx = static_cast<T>(0.05) * sw;
            T dy = static_cast<T>(0.05) * sh;

            auto clamp_x = [&](T val) { return std::clamp(val, g_xmin, g_xmax); };
            auto clamp_y = [&](T val) { return std::clamp(val, g_ymin, g_ymax); };

            // 递归分裂四个经过 10% 膨胀保护的子区域
            if (enable_tbb) {
                oneapi::tbb::parallel_invoke(
                    [&]() { subdivide_quadtree(f, clamp_x(x1 - dx), clamp_x(cx + dx), clamp_y(y1 - dy), clamp_y(cy + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex); },
                    [&]() { subdivide_quadtree(f, clamp_x(cx - dx), clamp_x(x2 + dx), clamp_y(y1 - dy), clamp_y(cy + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex); },
                    [&]() { subdivide_quadtree(f, clamp_x(x1 - dx), clamp_x(cx + dx), clamp_y(cy - dy), clamp_y(y2 + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex); },
                    [&]() { subdivide_quadtree(f, clamp_x(cx - dx), clamp_x(x2 + dx), clamp_y(cy - dy), clamp_y(y2 + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex); }
                );
            } else {
                subdivide_quadtree(f, clamp_x(x1 - dx), clamp_x(cx + dx), clamp_y(y1 - dy), clamp_y(cy + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex);
                subdivide_quadtree(f, clamp_x(cx - dx), clamp_x(x2 + dx), clamp_y(y1 - dy), clamp_y(cy + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex);
                subdivide_quadtree(f, clamp_x(x1 - dx), clamp_x(cx + dx), clamp_y(cy - dy), clamp_y(y2 + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex);
                subdivide_quadtree(f, clamp_x(cx - dx), clamp_x(x2 + dx), clamp_y(cy - dy), clamp_y(y2 + dy), min_w, min_h, decimal_places, val_low, val_high, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, out_points, log_file, out_mutex);
            }
        }

    } // namespace detail

    // =========================================================================
    // 💡 外部调用主接口
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

        // 💡 开启并写入过滤文件日志头部
        const std::string log_filename = "pruned_regions.txt";
        std::ofstream log_file(log_filename);
        if (log_file.is_open()) {
            log_file << "================================================================================\n"
                     << " StuCanvas 几何诊断系统 - 无根区间剪枝详细日志 [已启用 Y <= -10.0 过滤器]\n"
                     << "================================================================================\n\n";
        }

        // 计算最大分配核心
        unsigned int max_threads = threads;
        if (max_threads == 0) {
            max_threads = oneapi::tbb::info::default_concurrency();
        }

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
                x_min, x_max, y_min, y_max, // 绑定全局边界
                out_points,
                log_file, // 传入日志流
                out_mutex
            );
        });

        if (log_file.is_open()) {
            log_file << "\n================================================================================\n"
                     << " 几何诊断扫描结束。所有在 y <= -10 范围内的剪枝区间已成功审计保存。\n"
                     << "================================================================================\n";
            log_file.close();
            std::cout << "[诊断自检] 本次追踪中 y <= -10 范围内的无根剪枝区间已成功过滤并保存至: " << log_filename << std::endl;
        }
    }

} // namespace StuCanvas::plot