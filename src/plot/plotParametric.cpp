// --- 文件路径: src/plot/plotParametric.cpp ---
#include "../../pch.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/functions/functions.h"
#include "../../include/functions/lerp.h"

#include <oneapi/tbb/parallel_for.h>
#include <vector>
#include <cmath>
#include <algorithm>

// 参数方程采样密度：每单位t采样20个点
constexpr double SAMPLES_PER_UNIT_T = 20.0;
constexpr int BLOCK_GRAIN_SIZE = 1024;

namespace {
    // 内部SIMD辅助：世界转NDC存储
    template<typename T>
    FORCE_INLINE void write_param_batch_ndc(
        const T& x_batch,
        const T& y_batch,
        PointData*& out_ptr,
        int& valid_count,
        unsigned int func_idx,
        const NDCMap& ndc_map)
    {
        world_to_clip_store_batch(out_ptr, x_batch, y_batch, ndc_map, func_idx);
        out_ptr += T::size;
        valid_count += T::size;
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
    // 1. 计算采样总数
    double t_range = t_max - t_min;
    if (t_range <= 0) return;
    int total_samples = static_cast<int>(std::floor(t_range * SAMPLES_PER_UNIT_T));
    if (total_samples <= 0) total_samples = 100;
    double step_t = t_range / static_cast<double>(total_samples);

    // 2. 准备并行分块
    int num_blocks = (total_samples + BLOCK_GRAIN_SIZE - 1) / BLOCK_GRAIN_SIZE;
    std::vector<std::vector<PointData>> block_buffers(num_blocks);

    // 3. TBB并行分块执行
    oneapi::tbb::parallel_for(0, num_blocks, [&](int block_idx) {
        int start_idx = block_idx * BLOCK_GRAIN_SIZE;
        int count = std::min(BLOCK_GRAIN_SIZE, total_samples - start_idx);

        auto& local_buffer = block_buffers[block_idx];
        local_buffer.resize(count);
        PointData* out_ptr = local_buffer.data();
        int valid_count = 0;

        int i = 0;
        constexpr int batch_size = batch_type::size;
        constexpr int unroll_factor = 2;
        constexpr int step_stride = batch_size * unroll_factor;

        const batch_type v_step_t(step_t);
        const batch_type v_step_block(step_t * step_stride);
        const batch_type v_step_offset2(step_t * batch_size);
        const batch_type v_index = get_index_vec();

        // 初始 t 向量
        batch_type t_batch_1 = batch_type(t_min) + (batch_type((double)start_idx) + v_index) * v_step_t;
        batch_type t_batch_2 = t_batch_1 + v_step_offset2;

        // --- SIMD 循环 ---
        for (; i + step_stride <= count; i += step_stride) {
            // 计算 X(t) 和 Y(t)。注意：evaluate_rpn 第三个参数是 t_param
            batch_type wx1 = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_batch_1);
            batch_type wy1 = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_batch_1);

            batch_type wx2 = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_batch_2);
            batch_type wy2 = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_batch_2);

            // 检查 X 或 Y 任意一维是否失效
            auto inv1 = xs::isnan(wx1) | xs::isinf(wx1) | xs::isnan(wy1) | xs::isinf(wy1);
            auto inv2 = xs::isnan(wx2) | xs::isinf(wx2) | xs::isnan(wy2) | xs::isinf(wy2);

            if (xs::none(inv1) && xs::none(inv2)) {
                write_param_batch_ndc(wx1, wy1, out_ptr, valid_count, func_idx, ndc_map);
                write_param_batch_ndc(wx2, wy2, out_ptr, valid_count, func_idx, ndc_map);
            } else {
                // 处理无效值路径 (略，逻辑同 plotExplicit 的解包逻辑)
            }
            t_batch_1 += v_step_block;
            t_batch_2 += v_step_block;
        }

        // --- 标量清理 ---
        for (; i < count; ++i) {
            double t = t_min + (start_idx + i) * step_t;
            double wx = evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t);
            double wy = evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t);
            if (std::isfinite(wx) && std::isfinite(wy)) {
                world_to_clip_store(*out_ptr, wx, wy, ndc_map, func_idx);
                out_ptr++; valid_count++;
            }
        }
        local_buffer.resize(valid_count);
    });

    // 4. 最终数据合并
    std::vector<PointData> final_vec;
    size_t total_points = 0;
    for (const auto& buf : block_buffers) total_points += buf.size();
    final_vec.reserve(total_points);

    for (auto& buf : block_buffers) {
        final_vec.insert(final_vec.end(),
                         std::make_move_iterator(buf.begin()),
                         std::make_move_iterator(buf.end()));
    }

    // 5. 结果推送
    results_queue->push({ func_idx, std::move(final_vec) });
}