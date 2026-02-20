// --- 文件路径: src/plot/plotCircle.cpp ---

#include "../../include/plot/plotCircle.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    // 辅助：检查点是否在矩形内 (带 epsilon 容差)
    inline bool is_point_in_rect(double x, double y, double x_min, double x_max, double y_min, double y_max) {
        const double EPS = 1e-6;
        return x >= x_min - EPS && x <= x_max + EPS &&
               y >= y_min - EPS && y <= y_max + EPS;
    }

    // 辅助：归一化角度到 [0, 2PI)
    inline double normalize_angle(double a) {
        const double TWO_PI = 6.283185307179586;
        double res = std::fmod(a, TWO_PI);
        if (res < 0) res += TWO_PI;
        return res;
    }

    // 检查角度是否在圆弧范围内 (支持逆时针跨越 0 点的情况)
    inline bool is_angle_in_arc(double a, double start, double end, bool is_full_circle) {
        if (is_full_circle) return true;
        a = normalize_angle(a);
        start = normalize_angle(start);
        end = normalize_angle(end);

        if (start <= end) {
            return a >= start && a <= end;
        }
        return a >= start || a <= end;
    }
}

/**
 * @brief 高性能圆/圆弧绘制器 (16.16 定点数旋转 DDA 版)
 */
void PlotCircle(
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>* results_queue,
    double cx, double cy, double r,
    const ViewState& view,
    double start_t,
    double end_t,
    bool is_full_circle
) {
    // 1. 确定世界坐标视口边界
    Vec2 world_top_left = view.ScreenToWorld(0, 0);
    Vec2 world_bottom_right = view.ScreenToWorld(view.screen_width, view.screen_height);

    double x_min = std::min(world_top_left.x, world_bottom_right.x);
    double x_max = std::max(world_top_left.x, world_bottom_right.x);
    double y_min = std::min(world_top_left.y, world_bottom_right.y);
    double y_max = std::max(world_top_left.y, world_bottom_right.y);

    // 2. 快速剔除：如果圆的包围盒与屏幕完全不相交，直接返回
    if (cx + r < x_min || cx - r > x_max || cy + r < y_min || cy - r > y_max) {
        return;
    }

    // 3. 计算 LOD (采样密度)
    double r_pixel = std::abs(r / view.wpp);
    if (r_pixel < 0.5) return; // 太小了，不画

    // 经验公式：步长随像素半径增加而减小
    double base_dt = 1.0 / std::pow(r_pixel, 0.95);

    // 4. 收集切分点 (Cut Points)
    std::vector<double> cut_points;
    cut_points.reserve(24);
    cut_points.push_back(0.0);
    cut_points.push_back(6.283185307179586); // 2PI

    if (!is_full_circle) {
        cut_points.push_back(normalize_angle(start_t));
        cut_points.push_back(normalize_angle(end_t));
    }

    // 计算圆与屏幕四边的交点角
    auto solve_x = [&](double boundary) {
        double val = (boundary - cx) / r;
        if (std::abs(val) <= 1.0) {
            double t = std::acos(val);
            cut_points.push_back(normalize_angle(t));
            cut_points.push_back(normalize_angle(-t));
        }
    };
    solve_x(x_min); solve_x(x_max);

    auto solve_y = [&](double boundary) {
        double val = (boundary - cy) / r;
        if (std::abs(val) <= 1.0) {
            double t = std::asin(val);
            cut_points.push_back(normalize_angle(t));
            cut_points.push_back(normalize_angle(3.141592653589793 - t));
        }
    };
    solve_y(y_min); solve_y(y_max);

    // 排序并去重
    std::ranges::sort(cut_points);
    cut_points.erase(std::ranges::unique(cut_points, [](double a, double b){
        return std::abs(a - b) < 1e-8;
    }).begin(), cut_points.end());

    std::vector<PointData> final_points;
    final_points.reserve(2048);

    // 5. 遍历每个采样区间
    for (size_t i = 0; i < cut_points.size() - 1; ++i) {
        double t_s = cut_points[i];
        double t_e = cut_points[i+1];
        if (t_e - t_s < 1e-7) continue;

        // 取区间中点检查是否在“屏幕内”且“圆弧内”
        double t_mid = (t_s + t_e) * 0.5;
        double mx = cx + r * std::cos(t_mid);
        double my = cy + r * std::sin(t_mid);

        if (is_point_in_rect(mx, my, x_min, x_max, y_min, y_max) &&
            is_angle_in_arc(t_mid, start_t, end_t, is_full_circle))
        {
            double span = t_e - t_s;
            int steps = std::max(2, static_cast<int>(std::ceil(span / base_dt)));
            double step_size = span / (steps - 1);

            // --- 核心优化：16.16 定点数旋转 DDA ---
            double cos_dt = std::cos(step_size);
            double sin_dt = std::sin(step_size);

            // 预计算旋转增量系数
            int32_t c_fp = static_cast<int32_t>(cos_dt * 65536.0);
            int32_t s_fp = static_cast<int32_t>(sin_dt * 65536.0);

            // 初始向量 (World -> NDC -> 16.16)
            double vx0 = r * std::cos(t_s);
            double vy0 = r * std::sin(t_s);
            int64_t cur_vx_fp = static_cast<int64_t>(vx0 * view.ndc_scale_x * 65536.0);
            int64_t cur_vy_fp = static_cast<int64_t>(vy0 * view.ndc_scale_y * 65536.0);

            // 获取 Clip 空间圆心坐标
            Vec2i center_clip = view.WorldToClipNoOffset(cx, cy);
            int32_t cx_fp = static_cast<int32_t>(center_clip.x) << 16;
            int32_t cy_fp = static_cast<int32_t>(center_clip.y) << 16;

            // 插值循环
            for (int k = 0; k < steps - 1; ++k) {
                final_points.push_back({
                    static_cast<int16_t>((cx_fp + cur_vx_fp) >> 16),
                    static_cast<int16_t>((cy_fp + cur_vy_fp) >> 16)
                });

                // 旋转变换: [vx, vy] * [[c, s], [-s, c]]
                int64_t next_vx = (cur_vx_fp * c_fp - cur_vy_fp * s_fp) >> 16;
                int64_t next_vy = (cur_vx_fp * s_fp + cur_vy_fp * c_fp) >> 16;
                cur_vx_fp = next_vx;
                cur_vy_fp = next_vy;
            }

            // 最后一项强制锁定到区间终点，消除累积舍入误差
            Vec2i end_pt = view.WorldToClipNoOffset(cx + r * std::cos(t_e), cy + r * std::sin(t_e));
            final_points.push_back({end_pt.x, end_pt.y});
        }
    }

    // 6. 导出结果
    if (!final_points.empty()) {
        results_queue->push(std::move(final_points));
    }
}