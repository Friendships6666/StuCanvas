// --- 文件路径: src/plot/plotCircle.cpp ---

#include "../../include/plot/plotCircle.h"
#include "../../include/functions/lerp.h" // 包含 world_to_clip_store
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    // 辅助：检查点是否在矩形内 (带 epsilon 容差)
    inline bool is_point_in_rect(double x, double y, double x_min, double x_max, double y_min, double y_max) {
        const double EPS = 1e-5;
        return x >= x_min - EPS && x <= x_max + EPS &&
               y >= y_min - EPS && y <= y_max + EPS;
    }

    // 辅助：归一化角度到 [0, 2PI)
    inline double normalize_angle(double a) {
        const double TWO_PI = 6.283185307179586;
        a = std::fmod(a, TWO_PI);
        if (a < 0) a += TWO_PI;
        return a;
    }
}

void process_circle_specialized(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double cx, double cy, double r,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    const NDCMap& ndc_map
) {
    // 1. 确定世界坐标视口边界
    double wx_start = world_origin.x;
    double wx_end = world_origin.x + screen_width * wppx;
    double wy_start = world_origin.y;
    double wy_end = world_origin.y + screen_height * wppy;

    double x_min = std::min(wx_start, wx_end);
    double x_max = std::max(wx_start, wx_end);
    double y_min = std::min(wy_start, wy_end);
    double y_max = std::max(wy_start, wy_end);

    // 2. 快速剔除：包围盒不相交
    if (cx + r < x_min || cx - r > x_max || cy + r < y_min || cy - r > y_max) {


        results_queue->push({func_idx, {}});
        return;
    }

    // 3. 计算 LOD (采样密度)
    // 屏幕像素半径 R_px
    double r_pixel = std::abs(r / wppx);
    if (r_pixel < 0.5) {
        results_queue->push({func_idx, {}});
        return;
    }


    double base_dt = 1 / pow(r_pixel,1.005);

    // 4. 寻找所有切分点 t (交点 + 0 + 2PI)
    std::vector<double> cut_points;
    cut_points.reserve(16);
    cut_points.push_back(0.0);
    cut_points.push_back(2.0 * M_PI);

    // 解 x(t) = Boundary (x = cx + r*cos(t)) -> cos(t) = (B - cx)/r
    auto solve_x = [&](double boundary) {
        double val = (boundary - cx) / r;
        if (std::abs(val) <= 1.0) {
            double t = std::acos(val);
            cut_points.push_back(normalize_angle(t));
            cut_points.push_back(normalize_angle(-t));
        }
    };
    solve_x(x_min); solve_x(x_max);

    // 解 y(t) = Boundary (y = cy + r*sin(t)) -> sin(t) = (B - cy)/r
    auto solve_y = [&](double boundary) {
        double val = (boundary - cy) / r;
        if (std::abs(val) <= 1.0) {
            double t = std::asin(val);
            cut_points.push_back(normalize_angle(t));
            cut_points.push_back(normalize_angle(M_PI - t));
        }
    };
    solve_y(y_min); solve_y(y_max); // 修正：这里应该是 y_min, y_max

    // 排序切分点，形成有序的时间轴
    std::sort(cut_points.begin(), cut_points.end());

    std::vector<PointData> final_points;
    // 预估最大点数分配内存

    final_points.reserve(6000);


    // 5. 遍历切分区间，计算有效部分
    for (size_t i = 0; i < cut_points.size() - 1; ++i) {
        double t_start = cut_points[i];
        double t_end = cut_points[i+1];
        if (t_end - t_start < 1e-5) continue;



        // 取中点检查是否在屏幕内
        double t_mid = (t_start + t_end) * 0.5;
        double mx = cx + r * std::cos(t_mid);
        double my = cy + r * std::sin(t_mid);

        if (is_point_in_rect(mx, my, x_min, x_max, y_min, y_max)) {

            // 区间在屏幕内 -> 插值生成
            double span = t_end - t_start;
            // 动态计算该段需要的步数
            int steps = std::max(2, static_cast<int>(std::ceil(span / base_dt)));
            double step_t = span / (steps - 1);

            for (int k = 0; k < steps; ++k) {
                double t = t_start + k * step_t;

                // 计算世界坐标
                double wx = cx + r * std::cos(t);
                double wy = cy + r * std::sin(t);

                // 转存为 Clip Float
                PointData pd{};
                world_to_clip_store(pd, wx, wy, ndc_map, func_idx);
                final_points.push_back(pd);
            }
            // 按照要求：不插入 NaN，多段之间会自动连线
        }
    }


    results_queue->push({func_idx, std::move(final_points)});
}