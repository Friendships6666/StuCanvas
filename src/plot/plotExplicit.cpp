// --- 文件路径: src/plot/plotExplicit.cpp ---
#include "../../pch.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/functions/functions.h"
#include "../../include/functions/lerp.h"
#include <vector>
#include <cmath>
#include <algorithm>
constexpr double SAMPLING_DENSITY = 1;
namespace {
    // 简单的线段裁剪并补点函数
    // 针对 Clip Space [-1.2, 1.2] 范围进行裁剪（多留 0.2 边缘防止缝隙）
    void clip_and_interpolate(
        std::vector<PointData>& out,
        PointData p1, PointData p2,
        float pixel_size_clip,
        unsigned int func_idx
    ) {
        float x1 = p1.position.x, y1 = p1.position.y;
        float x2 = p2.position.x, y2 = p2.position.y;

        // 1. 简单的视口包围盒快速剔除 (AABB 检查)
        // 增加缓冲区到 1.2，确保斜线能画到屏幕边缘
        const float margin = 1.1f;
        if (std::max(x1, x2) < -margin || std::min(x1, x2) > margin ||
            std::max(y1, y2) < -margin || std::min(y1, y2) > margin) {
            return;
        }

        // 2. 计算线段参数
        float dx = x2 - x1;
        float dy = y2 - y1;
        float t_min = 0.0f;
        float t_max = 1.0f;

        // 3. 针对 Y 轴边界进行参数裁剪 (Liang-Barsky 简化版)
        // 我们重点防止 Y 轴溢出导致的补点爆炸
        auto clip_t = [&](float p, float q) {
            if (p < 0) {
                float r = q / p;
                if (r > t_max) return false;
                if (r > t_min) t_min = r;
            } else if (p > 0) {
                float r = q / p;
                if (r < t_min) return false;
                if (r < t_max) t_max = r;
            } else if (q < 0) return false;
            return true;
        };

        // 裁剪 Y 轴范围 [-margin, margin]
        if (!clip_t(-dy, y1 - (-margin))) return;
        if (!clip_t(dy, margin - y1)) return;

        // 4. 根据裁剪后的 t 范围确定有效起止点
        float start_x = x1 + t_min * dx;
        float start_y = y1 + t_min * dy;
        float end_x = x1 + t_max * dx;
        float end_y = y1 + t_max * dy;

        // 5. 在有效范围内进行插值
        float vis_dx = end_x - start_x;
        float vis_dy = end_y - start_y;
        float dist = 0.5*std::sqrt(vis_dx * vis_dx + vis_dy * vis_dy);

        if (dist > pixel_size_clip) {
            int steps = static_cast<int>(std::ceil(dist / pixel_size_clip));
            // 现在的 steps 永远不会爆炸，因为 dist 最大也就是 2.8 左右 (屏幕对角线)
            steps = std::min(steps, 2048);

            for (int k = 0; k <= steps; ++k) {
                float t = (float)k / (float)steps;
                PointData pd{};
                pd.position.x = start_x + t * vis_dx;
                pd.position.y = start_y + t * vis_dy;
                pd.function_index = func_idx;
                out.push_back(pd);
            }
        } else {
            // 距离很近，直接加端点
            PointData pd;
            pd.position.x = start_x; pd.position.y = start_y; pd.function_index = func_idx;
            out.push_back(pd);
            pd.position.x = end_x; pd.position.y = end_y;
            out.push_back(pd);
        }
    }

    // 内部 SIMD 转换
    FORCE_INLINE void world_to_clip_store_fixed_batch(
        PointData* out_ptr,
        const batch_type& wx_batch,
        const batch_type& wy_batch,
        const NDCMap& map,
        unsigned int func_idx
    ) {
        batch_type b_center_x(map.center_x);
        batch_type b_center_y(map.center_y);
        batch_type b_scale_x(map.scale_x);
        batch_type b_scale_y(map.scale_y);

        batch_type ndc_x = (wx_batch - b_center_x) * b_scale_x;
        batch_type ndc_y = -(wy_batch - b_center_y) * b_scale_y; // 修复 Y 反转

        constexpr std::size_t N = batch_type::size;
        alignas(64) double buf_x[N], buf_y[N];
        ndc_x.store_aligned(buf_x);
        ndc_y.store_aligned(buf_y);

        for (std::size_t k = 0; k < N; ++k) {
            out_ptr[k].position.x = static_cast<float>(buf_x[k]);
            out_ptr[k].position.y = static_cast<float>(buf_y[k]);
            out_ptr[k].function_index = func_idx;
        }
    }
}

void process_explicit_chunk(
    double x_start_world, double x_end_world,
    const AlignedVector<RPNToken>& rpn_program,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    unsigned int func_idx,
    double screen_width,
    const NDCMap& ndc_map
) {
    int total_samples = static_cast<int>(std::ceil(screen_width * SAMPLING_DENSITY));
    if (total_samples < 2) total_samples = 2;
    double step_x = (x_end_world - x_start_world) / (total_samples - 1);

    // 计算骨架点 (SIMD)
    std::vector<PointData> raw_points;
    raw_points.resize(total_samples);
    PointData* raw_ptr = raw_points.data();
    int valid_count = 0;

    constexpr int batch_size = batch_type::size;
    const batch_type v_step_x(step_x);
    const batch_type v_step_batch(step_x * (double)batch_size);
    const batch_type v_index = get_index_vec();
    batch_type x_batch = batch_type(x_start_world) + v_index * v_step_x;

    int i = 0;
    for (; i + batch_size <= total_samples; i += batch_size) {
        batch_type y_batch = evaluate_rpn<batch_type>(rpn_program, x_batch);
        world_to_clip_store_fixed_batch(raw_ptr + valid_count, x_batch, y_batch, ndc_map, func_idx);
        valid_count += batch_size;
        x_batch += v_step_batch;
    }
    for (; i < total_samples; ++i) {
        double wx = x_start_world + i * step_x;
        double wy = evaluate_rpn<double>(rpn_program, wx);
        world_to_clip_store(raw_ptr[valid_count++], wx, wy, ndc_map, func_idx);
    }

    // 细分插值 (基于裁剪后的线段)
    std::vector<PointData> final_points;
    final_points.reserve(total_samples * 2);

    float pixel_size_clip = 2.0f / static_cast<float>(screen_width);

    for (int k = 1; k < valid_count; ++k) {
        // 对每一对相邻点构成的线段进行视口裁剪并插值
        clip_and_interpolate(final_points, raw_points[k-1], raw_points[k], pixel_size_clip, func_idx);
    }

    results_queue->push({ func_idx, std::move(final_points) });
}