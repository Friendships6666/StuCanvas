// --- 文件路径: src/plot/plotParametric.cpp ---
#include "../../pch.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/functions/functions.h"
#include "../../include/functions/lerp.h"

#include <vector>
#include <cmath>
#include <algorithm>

// 参数方程采样密度：每单位t采样20个点 (骨架点密度)
constexpr double SAMPLES_PER_UNIT_T = 20.0;

namespace {
    /**
     * @brief 修正后的 SIMD 批量转换存储 (显式处理 Y 轴反转)
     */
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
        // 核心修复：添加负号以匹配屏幕坐标系
        batch_type ndc_y = -(wy_batch - b_center_y) * b_scale_y;

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

    /**
     * @brief 线段细分插值逻辑 (同 plotExplicit)
     */
    void clip_and_tessellate(
        std::vector<PointData>& out,
        const PointData& p1, const PointData& p2,
        float pixel_size_clip,
        unsigned int func_idx
    ) {
        float x1 = p1.position.x, y1 = p1.position.y;
        float x2 = p2.position.x, y2 = p2.position.y;

        // 1. AABB 快速剔除 (针对 Clip 空间 [-1.2, 1.2])
        const float margin = 1.2f;
        if (std::max(x1, x2) < -margin || std::min(x1, x2) > margin ||
            std::max(y1, y2) < -margin || std::min(y1, y2) > margin) {
            return;
        }

        // 2. 距离检查与插值
        float dx = x2 - x1;
        float dy = y2 - y1;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist > pixel_size_clip) {
            int steps = static_cast<int>(std::ceil(dist / pixel_size_clip));
            // 限制最大插值数，防止内存爆炸
            steps = std::min(steps, 1024);

            for (int k = 1; k < steps; ++k) {
                float t = static_cast<float>(k) / static_cast<float>(steps);
                PointData mid;
                mid.position.x = x1 + t * dx;
                mid.position.y = y1 + t * dy;
                mid.function_index = func_idx;
                out.push_back(mid);
            }
        }
        // 压入采样终点
        out.push_back(p2);
    }
}

void process_parametric_chunk(
    const AlignedVector<RPNToken>& rpn_x,
    const AlignedVector<RPNToken>& rpn_y,
    double t_min, double t_max,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    unsigned int func_idx,
    const NDCMap& ndc_map
) {
    // 1. 计算基础采样总数
    double t_range = t_max - t_min;
    if (t_range <= 0) return;
    int total_samples = static_cast<int>(std::floor(t_range * SAMPLES_PER_UNIT_T));
    if (total_samples < 2) total_samples = 100;
    double step_t = t_range / static_cast<double>(total_samples - 1);

    // 2. 计算骨架点 (单一核心 SIMD 循环)
    std::vector<PointData> raw_points;
    raw_points.resize(total_samples);
    int valid_count = 0;

    constexpr int batch_size = batch_type::size;
    const batch_type v_step_t(step_t);
    const batch_type v_step_batch(step_t * (double)batch_size);
    const batch_type v_index = get_index_vec();

    batch_type t_batch = batch_type(t_min) + v_index * v_step_t;

    int i = 0;
    for (; i + batch_size <= total_samples; i += batch_size) {
        batch_type wx = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_batch);
        batch_type wy = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_batch);
        world_to_clip_store_fixed_batch(raw_points.data() + valid_count, wx, wy, ndc_map, func_idx);
        valid_count += batch_size;
        t_batch += v_step_batch;
    }

    for (; i < total_samples; ++i) {
        double t = t_min + i * step_t;
        double wx = evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t);
        double wy = evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t);
        world_to_clip_store(raw_points[valid_count++], wx, wy, ndc_map, func_idx);
    }

    // 3. 全局细分插值 (Tessellation Pass)
    std::vector<PointData> final_points;
    final_points.reserve(valid_count * 2);

    if (valid_count > 0) {
        final_points.push_back(raw_points[0]);

        // ★★★ 精确计算 1 像素对应的 Clip 空间距离 ★★★
        // 逻辑推导：
        // 1 像素在 World 空间的长 = wppx
        // World 空间到 Clip 空间的缩放倍率 = scale_x
        // 因此 1 像素在 Clip 空间的长 = wppx * scale_x
        // 因为 ndc_map.scale_x = 2.0 / (ScreenW * WPPX)，
        // 所以 wppx * scale_x = 2.0 / ScreenW，这正是 Clip Space 下 1 像素的精确定义。
        float pixel_size_clip = static_cast<float>(std::abs(g_global_view_state.wppx * ndc_map.scale_x));

        // 如果 scale_x 异常（如视图被破坏），设置一个安全最小值
        if (pixel_size_clip < 1e-7f) pixel_size_clip = 0.001f;

        for (int k = 1; k < valid_count; ++k) {
            clip_and_tessellate(final_points, raw_points[k - 1], raw_points[k], pixel_size_clip, func_idx);
        }
    }

    // 4. 结果推送
    results_queue->push({ func_idx, std::move(final_points) });
}