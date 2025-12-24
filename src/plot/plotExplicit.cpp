// --- 文件路径: src/plot/plotExplicit.cpp ---
#include "../../pch.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/functions/functions.h"
#include "../../include/functions/lerp.h"

#include <oneapi/tbb/parallel_for.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// 采样密度控制：1.0表示每像素采样1个点
constexpr double SAMPLING_DENSITY = 1.0;
// TBB并行任务切分的粒度
constexpr int BLOCK_GRAIN_SIZE = 1024;

namespace {
    // 内部SIMD写入辅助：利用lerp.h中的高精度转换函数
    template<typename T>
    FORCE_INLINE void write_batch_to_buffer_ndc(
        const T& x_batch,
        const T& y_batch,
        PointData*& out_ptr,
        int& valid_count,
        unsigned int func_idx,
        const NDCMap& ndc_map)
    {
        // 调用统一的Double->Float Clip转换存储函数
        world_to_clip_store_batch(out_ptr, x_batch, y_batch, ndc_map, func_idx);

        // 步进指针和计数器
        out_ptr += T::size;
        valid_count += T::size;
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
    // 1. 计算采样参数
    int total_samples = static_cast<int>(std::ceil(screen_width * SAMPLING_DENSITY));
    if (total_samples <= 0) total_samples = 100;
    double step_x = (x_end_world - x_start_world) / static_cast<double>(total_samples);

    // 2. 准备并行分块缓冲区
    int num_blocks = (total_samples + BLOCK_GRAIN_SIZE - 1) / BLOCK_GRAIN_SIZE;
    std::vector<std::vector<PointData>> block_buffers(num_blocks);

    // 3. TBB并行分块计算
    oneapi::tbb::parallel_for(0, num_blocks, [&](int block_idx) {
        int start_idx = block_idx * BLOCK_GRAIN_SIZE;
        int count = std::min(BLOCK_GRAIN_SIZE, total_samples - start_idx);

        auto& local_buffer = block_buffers[block_idx];
        local_buffer.resize(count); // 预分配最大可能空间
        PointData* out_ptr = local_buffer.data();
        int valid_count = 0;

        int i = 0;
        constexpr int batch_size = batch_type::size;
        // 使用2倍循环展开以利用指令级并行(ILP)掩盖RPN计算延迟
        constexpr int unroll_factor = 2;
        constexpr int step_stride = batch_size * unroll_factor;

        const batch_type v_step_x(step_x);
        const batch_type v_step_block(step_x * step_stride);
        const batch_type v_step_offset2(step_x * batch_size);
        const batch_type v_index = get_index_vec();

        // 初始X向量计算 (Double)
        batch_type x_batch_1 = batch_type(x_start_world) + (batch_type((double)start_idx) + v_index) * v_step_x;
        batch_type x_batch_2 = x_batch_1 + v_step_offset2;

        // --- SIMD 主循环 ---
        for (; i + step_stride <= count; i += step_stride) {
            // A. 执行RPN求值 (全程Double)
            batch_type y_batch_1 = evaluate_rpn<batch_type>(rpn_program, x_batch_1);
            batch_type y_batch_2 = evaluate_rpn<batch_type>(rpn_program, x_batch_2);

            // B. 检查NaN或无穷大
            auto inv1 = xs::isnan(y_batch_1) | xs::isinf(y_batch_1);
            auto inv2 = xs::isnan(y_batch_2) | xs::isinf(y_batch_2);

            // C. 快速路径：所有数据有效
            if (xs::none(inv1) && xs::none(inv2)) {
                write_batch_to_buffer_ndc(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx, ndc_map);
                write_batch_to_buffer_ndc(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx, ndc_map);
            }
            else {
                // 慢速路径：含有无效值，分Batch处理
                // 处理第一个 Batch
                if (xs::none(inv1)) {
                    write_batch_to_buffer_ndc(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx, ndc_map);
                } else if (!xs::all(inv1)) {
                    alignas(64) double xs_arr[batch_size], ys_arr[batch_size];
                    x_batch_1.store_aligned(xs_arr); y_batch_1.store_aligned(ys_arr);
                    for(int k=0; k<batch_size; ++k) {
                        if(std::isfinite(ys_arr[k])) {
                            world_to_clip_store(*out_ptr, xs_arr[k], ys_arr[k], ndc_map, func_idx);
                            out_ptr++; valid_count++;
                        }
                    }
                }
                // 处理第二个 Batch
                if (xs::none(inv2)) {
                    write_batch_to_buffer_ndc(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx, ndc_map);
                } else if (!xs::all(inv2)) {
                    alignas(64) double xs_arr[batch_size], ys_arr[batch_size];
                    x_batch_2.store_aligned(xs_arr); y_batch_2.store_aligned(ys_arr);
                    for(int k=0; k<batch_size; ++k) {
                        if(std::isfinite(ys_arr[k])) {
                            world_to_clip_store(*out_ptr, xs_arr[k], ys_arr[k], ndc_map, func_idx);
                            out_ptr++; valid_count++;
                        }
                    }
                }
            }
            x_batch_1 += v_step_block;
            x_batch_2 += v_step_block;
        }

        // --- 标量清理路径 (处理不足一个Batch的剩余点) ---
        for (; i < count; ++i) {
            double wx = x_start_world + (start_idx + i) * step_x;
            double wy = evaluate_rpn<double>(rpn_program, wx);
            if (std::isfinite(wy)) {
                world_to_clip_store(*out_ptr, wx, wy, ndc_map, func_idx);
                out_ptr++; valid_count++;
            }
        }
        local_buffer.resize(valid_count); // 最终收缩至实际有效数量
    });

    // 4. 数据扁平化合并
    std::vector<PointData> final_vec;
    size_t total_points = 0;
    for (const auto& buf : block_buffers) total_points += buf.size();
    final_vec.reserve(total_points);

    for (auto& buf : block_buffers) {
        if (!buf.empty()) {
            final_vec.insert(final_vec.end(),
                             std::make_move_iterator(buf.begin()),
                             std::make_move_iterator(buf.end()));
        }
    }

    // 5. 结果推送到并行结果队列
    results_queue->push({ func_idx, std::move(final_vec) });
}