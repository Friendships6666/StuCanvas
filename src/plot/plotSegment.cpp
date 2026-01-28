// --- 文件路径: src/plot/plotSegment.cpp ---

#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/plot/plotSegment.h"
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdint>

/**
 * @brief 极致优化的线段/直线绘制器 (16.16 定点数插值版)
 * 逻辑：在 CLIP 整数空间 [-32767, 32767] 内进行 DDA 插值
 */
void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& queue,
    double x1, double y1, double x2, double y2, // 输入为相对坐标 (x_view, y_view)
    bool is_segment,
    const ViewState& view
) {
    // 1. 计算局部裁剪边界 (基于相对坐标系)
    // 增加 5% 冗余防止边缘空隙
    const double margin = 1.05;
    double rx_max = view.half_w * view.wpp * margin;
    double rx_min = -rx_max;
    double ry_max = view.half_h * view.wpp * margin;
    double ry_min = -ry_max;

    // 2. 准备参数
    double dx = x2 - x1;
    double dy = y2 - y1;
    double t0 = is_segment ? 0.0 : -1.0e8; // 直线模式下的虚似无穷大
    double t1 = is_segment ? 1.0 : 1.0e8;

    // 3. Liang-Barsky 裁剪 (使用 f64 确保裁剪精度)
    auto clip_test = [&](double p, double q) -> bool {
        if (std::abs(p) < 1e-15) return q >= 0;
        double r = q / p;
        if (p < 0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    };

    if (!clip_test(-dx, x1 - rx_min)) return;
    if (!clip_test( dx, rx_max - x1)) return;
    if (!clip_test(-dy, y1 - ry_min)) return;
    if (!clip_test( dy, ry_max - y1)) return;

    if (t0 > t1) return;

    // 4. 转换裁剪后的世界端点到 CLIP 整数空间 (int16_t)
    // 💡 调用 NoOffset 版本防止二次减去相机偏移
    Vec2i c1 = view.WorldToClipNoOffset(x1 + t0 * dx, y1 + t0 * dy);
    Vec2i c2 = view.WorldToClipNoOffset(x1 + t1 * dx, y1 + t1 * dy);

    // 5. 确定采样密度 (LOD)
    // 计算在 CLIP 空间下的位移量
    int32_t dcx = static_cast<int32_t>(c2.x) - static_cast<int32_t>(c1.x);
    int32_t dcy = static_cast<int32_t>(c2.y) - static_cast<int32_t>(c1.y);

    // 利用 s2c_scale 将 Clip 距离映射到屏幕像素距离
    float px_dist_x = static_cast<float>(dcx) / static_cast<float>(view.s2c_scale_x);
    float px_dist_y = static_cast<float>(dcy) / static_cast<float>(view.s2c_scale_y);
    float pixel_dist = std::sqrt(px_dist_x * px_dist_x + px_dist_y * px_dist_y);

    // 每 0.5 像素补一个点，兼顾平滑度与性能
    int num_samples = std::max(2, static_cast<int>(std::ceil(pixel_dist / 0.5f)) + 1);
    num_samples = std::min(num_samples, 16384);

    std::vector<PointData> final_points;
    final_points.reserve(num_samples);

    // =========================================================
    // 6. 核心优化：16.16 定点数插值循环
    // =========================================================
    // 使用 int64 计算步长防止位移溢出，循环内使用 int32
    int32_t divisor = num_samples - 1;

    // 定点数步长：(差值 << 16) / 除数
    int32_t step_x = static_cast<int32_t>((static_cast<int64_t>(dcx) << 16) / divisor);
    int32_t step_y = static_cast<int32_t>((static_cast<int64_t>(dcy) << 16) / divisor);

    // 当前定点坐标值 (高16位为整数部分)
    int32_t cur_x = static_cast<int32_t>(c1.x) << 16;
    int32_t cur_y = static_cast<int32_t>(c1.y) << 16;

    // 极致性能循环：仅整数加法与移位，不产生类型转换开销
    for (int i = 0; i < divisor; ++i) {
        final_points.push_back({
            static_cast<int16_t>(cur_x >> 16),
            static_cast<int16_t>(cur_y >> 16)
        });
        cur_x += step_x;
        cur_y += step_y;
    }

    // 最后一项强制锁定，确保图形闭合且无舍入误差
    final_points.push_back({c2.x, c2.y});

    // 7. 推送至并发队列
    queue.push(std::move(final_points));
}