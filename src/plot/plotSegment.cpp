// --- 文件路径: src/plot/plotSegment.cpp ---

#include "../../include/plot/plotCall.h"
#include "../../include/functions/lerp.h"
#include <algorithm>
#include <vector>
#include <cmath>

/**
 * @brief 优化后的 process_two_point_line (浮动原点版)
 *
 * 逻辑：
 * 1. 输入的 x1, y1, x2, y2 是相对于相机 offset 的局部坐标 (x_view)。
 * 2. 裁剪边界也转化为相对于相机中心的局部边界。
 * 3. 所有的裁剪计算都在 0 附近的极小数值环境下进行，确保浮点精度。
 */
void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2, // 💡 这里的输入已经是相对坐标 (view-relative)
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin, // 保持签名一致，但计算将更多参考屏幕尺寸
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y,
    const NDCMap& ndc_map
) {
    // =========================================================
    // 1. 计算局部裁剪边界 (Relative Viewport Bounds)
    // =========================================================
    // 在相对坐标系中，屏幕中心是 (0, 0)
    // 边界就是正负半屏的世界距离
    double half_w = (screen_width * 0.5) * std::abs(wppx);
    double half_h = (screen_height * 0.5) * std::abs(wppy);

    double rx_min = -half_w;
    double rx_max =  half_w;
    double ry_min = -half_h;
    double ry_max =  half_h;

    // 2. 参数化准备: P(t) = P1 + t*(P2 - P1)
    // 这里的 dx, dy 是小数字之间的减法，精度极高
    double dx = x2 - x1;
    double dy = y2 - y1;

    double final_t0 = is_segment ? 0.0 : -1.0e9;
    double final_t1 = is_segment ? 1.0 : 1.0e9;

    // =========================================================
    // 3. 局部坐标系下的 Liang-Barsky 裁剪
    // =========================================================
    auto clip_test = [&](double p, double q) -> bool {
        if (std::abs(p) < 1e-15) return q >= 0; // 平行于边界
        double r = q / p;
        if (p < 0) { // 外部射入内部
            if (r > final_t1) return false;
            if (r > final_t0) final_t0 = r;
        } else { // 内部射向外部
            if (r < final_t0) return false;
            if (r < final_t1) final_t1 = r;
        }
        return true;
    };

    if (!clip_test(-dx, x1 - rx_min)) { results_queue->push({func_idx, {}}); return; }
    if (!clip_test( dx, rx_max - x1)) { results_queue->push({func_idx, {}}); return; }
    if (!clip_test(-dy, y1 - ry_min)) { results_queue->push({func_idx, {}}); return; }
    if (!clip_test( dy, ry_max - y1)) { results_queue->push({func_idx, {}}); return; }

    if (final_t0 > final_t1) {
        results_queue->push({func_idx, {}});
        return;
    }

    // =========================================================
    // 4. 转换裁剪端点到 CLIP 空间
    // =========================================================
    // 💡 浮动原点优势：直接使用相对坐标乘以 scale，无需再减去巨大的 center_x
    // 我们假设 ndc_map 里的 scale 已经根据当前 view 算好了
    float cx1 = static_cast<float>((x1 + final_t0 * dx) * ndc_map.scale_x);
    float cy1 = -static_cast<float>((y1 + final_t0 * dy) * ndc_map.scale_y);
    float cx2 = static_cast<float>((x1 + final_t1 * dx) * ndc_map.scale_x);
    float cy2 = -static_cast<float>((y1 + final_t1 * dy) * ndc_map.scale_y);

    // =========================================================
    // 5. 像素级插值 (LOD 保持不变)
    // =========================================================
    float dx_pixel = (cx2 - cx1) * (float)screen_width * 0.5f;
    float dy_pixel = (cy2 - cy1) * (float)screen_height * 0.5f;
    float pixel_dist = std::sqrt(dx_pixel * dx_pixel + dy_pixel * dy_pixel);

    // 步长：0.4 像素
    int num_samples = std::max(2, static_cast<int>(std::ceil(pixel_dist / 0.4f)) + 1);

    // 限制最大采样数，防止内存爆炸
    num_samples = std::min(num_samples, 8192);

    std::vector<PointData> final_points;
    final_points.reserve(num_samples);

    float f_dx = cx2 - cx1;
    float f_dy = cy2 - cy1;

    for (int i = 0; i < num_samples; ++i) {
        float t = (float)i / (float)(num_samples - 1);
        PointData pd;
        pd.position.x = cx1 + t * f_dx;
        pd.position.y = cy1 + t * f_dy;
        pd.function_index = func_idx;
        final_points.push_back(pd);
    }

    results_queue->push({func_idx, std::move(final_points)});
}