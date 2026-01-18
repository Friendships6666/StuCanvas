// --- 文件路径: src/plot/plotSegment.cpp ---

#include "../../include/plot/plotCall.h"
#include "../../include/functions/lerp.h"
#include <algorithm>
#include <vector>
#include <cmath>

/**
 * @brief 优化后的 process_two_point_line
 * 移除 TBB 并行分发，简化 XSIMD 为标量逻辑，保留 Liang-Barsky 核心裁剪算法
 */
void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2,
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, // 对应头文件签名
    const NDCMap& ndc_map
) {
    // 1. 计算视口边界 (World Space)
    double wx_end = world_origin.x + screen_width * wppx;
    double wy_end = world_origin.y + screen_height * wppy;

    double x_min = std::min(world_origin.x, wx_end);
    double x_max = std::max(world_origin.x, wx_end);
    double y_min = std::min(world_origin.y, wy_end);
    double y_max = std::max(world_origin.y, wy_end);

    // 2. 参数化准备: P(t) = P1 + t*(P2 - P1)
    double dx = x2 - x1;
    double dy = y2 - y1;

    // 初始参数范围
    double final_t0 = is_segment ? 0.0 : -1.0e9;
    double final_t1 = is_segment ? 1.0 : 1.0e9;

    // =========================================================
    // 3. 顺序执行 Liang-Barsky 裁剪 (替代原有的 TBB/XSIMD 逻辑)
    // =========================================================
    auto clip_test = [&](double p, double q) -> bool {
        if (p < 0) { // 外部射入内部 (Entry)
            double r = q / p;
            if (r > final_t1) return false;
            if (r > final_t0) final_t0 = r;
        } else if (p > 0) { // 内部射向外部 (Exit)
            double r = q / p;
            if (r < final_t0) return false;
            if (r < final_t1) final_t1 = r;
        } else if (q < 0) { // 平行于边界且在界外
            return false;
        }
        return true;
    };

    // 依次对四个边界进行测试
    if (!clip_test(-dx, x1 - x_min)) { results_queue->push({func_idx, {}}); return; } // 左
    if (!clip_test( dx, x_max - x1)) { results_queue->push({func_idx, {}}); return; } // 右
    if (!clip_test(-dy, y1 - y_min)) { results_queue->push({func_idx, {}}); return; } // 下
    if (!clip_test( dy, y_max - y1)) { results_queue->push({func_idx, {}}); return; } // 上

    // =========================================================
    // 4. 判定最终范围
    // =========================================================
    if (final_t0 > final_t1) {
        results_queue->push({func_idx, {}});
        return;
    }

    // =========================================================
    // 5. 转换裁剪端点到 CLIP 空间 (Double -> Float)
    // =========================================================
    PointData p1_clip, p2_clip;
    world_to_clip_store(p1_clip, x1 + final_t0 * dx, y1 + final_t0 * dy, ndc_map, func_idx);
    world_to_clip_store(p2_clip, x1 + final_t1 * dx, y1 + final_t1 * dy, ndc_map, func_idx);

    // =========================================================
    // 6. 在 CLIP 空间使用 FLOAT 精度进行加密插值
    // =========================================================
    float cx1 = p1_clip.position.x; float cy1 = p1_clip.position.y;
    float cx2 = p2_clip.position.x; float cy2 = p2_clip.position.y;

    // 算出该线段在屏幕上占据的像素距离
    float dx_pixel = (cx2 - cx1) * (float)screen_width * 0.5f;
    float dy_pixel = (cy2 - cy1) * (float)screen_height * 0.5f;
    float pixel_dist = 0.5f * std::sqrt(dx_pixel * dx_pixel + dy_pixel * dy_pixel);

    // 步长：0.4 像素 (确保采样率满足 < 0.5 像素的要求)
    int num_samples = std::max(2, static_cast<int>(std::ceil(pixel_dist / 0.4f)) + 1);

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

    // 发送至结果队列
    results_queue->push({func_idx, std::move(final_points)});
}