// --- 文件路径: src/plot/plotExplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/functions/functions.h"

#include <oneapi/tbb/parallel_for.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iterator>

constexpr double SAMPLING_DENSITY = 1.0;
constexpr int BLOCK_GRAIN_SIZE = 1024; // 稍微调大分块，适应 unrolling

// 强制内联辅助函数，避免函数调用开销
template<typename T>
FORCE_INLINE void write_batch_to_buffer(
    const T& x_batch, const T& y_batch,
    PointData*& out_ptr, int& valid_count,
    unsigned int func_idx)
{
    constexpr int batch_size = T::size;
    alignas(T::arch_type::alignment()) double x_store[batch_size];
    alignas(T::arch_type::alignment()) double y_store[batch_size];

    x_batch.store_aligned(x_store);
    y_batch.store_aligned(y_store);

    // 编译器会自动将此循环展开为 movups/movntps
    for (int k = 0; k < batch_size; ++k) {
        // 利用指针直接写入，避免数组索引乘法
        out_ptr->position.x = x_store[k];
        out_ptr->position.y = y_store[k];
        out_ptr->function_index = func_idx;
        out_ptr++;
    }
    valid_count += batch_size;
}

void process_explicit_chunk(
    double /*y_min_world*/, double /*y_max_world*/,
    double x_start_world, double x_end_world,
    const AlignedVector<RPNToken>& rpn_program,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx,
    double screen_width
) {
    int total_samples = static_cast<int>(std::ceil(screen_width * SAMPLING_DENSITY));
    if (total_samples <= 0) total_samples = 100;

    double step_x = (x_end_world - x_start_world) / static_cast<double>(total_samples);

    int num_blocks = (total_samples + BLOCK_GRAIN_SIZE - 1) / BLOCK_GRAIN_SIZE;
    std::vector<std::vector<PointData>> block_buffers(num_blocks);

    oneapi::tbb::parallel_for(0, num_blocks, [&](int block_idx) {
        int start_idx = block_idx * BLOCK_GRAIN_SIZE;
        int count = std::min(BLOCK_GRAIN_SIZE, total_samples - start_idx);

        auto& local_buffer = block_buffers[block_idx];
        local_buffer.resize(count);
        PointData* out_ptr = local_buffer.data();
        int valid_count = 0;

        int i = 0;
        constexpr int batch_size = batch_type::size;
        // 2x Unrolling: 一次处理两个 batch
        constexpr int unroll_factor = 2;
        constexpr int step_stride = batch_size * unroll_factor;

        const batch_type step_vec = batch_type(step_x);
        const batch_type step_block_vec = batch_type(step_x * step_stride); // 用于外层循环步进
        const batch_type step_offset_vec = batch_type(step_x * batch_size); // 用于 Batch 2 的偏移
        const batch_type index_vec = get_index_vec();

        // 初始 X 向量
        batch_type x_batch_1 = batch_type(x_start_world) +
                               (batch_type((double)start_idx) + index_vec) * step_vec;
        batch_type x_batch_2 = x_batch_1 + step_offset_vec;

        // --- SIMD ILP 路径 ---
        // 每次循环处理 2 * batch_size 个点 (例如 AVX2 下是 8 个 double)
        for (; i + step_stride <= count; i += step_stride) {

            // 1. 并行发射计算指令 (打破数据依赖链)
            // CPU 可以乱序执行这两组计算
            batch_type y_batch_1 = evaluate_rpn<batch_type>(rpn_program, x_batch_1);
            batch_type y_batch_2 = evaluate_rpn<batch_type>(rpn_program, x_batch_2);

            // 2. 并行检查掩码
            auto invalid_1 = xs::isnan(y_batch_1) | xs::isinf(y_batch_1);
            auto invalid_2 = xs::isnan(y_batch_2) | xs::isinf(y_batch_2);

            // 3. Fast Path: 两组都有效 (最常见情况)
            if (xs::none(invalid_1) && xs::none(invalid_2)) {
                write_batch_to_buffer(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx);
                write_batch_to_buffer(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx);
            }
            else {
                // 慢速路径：回退到逐个处理
                // 处理 Batch 1
                if (xs::none(invalid_1)) {
                    write_batch_to_buffer(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx);
                } else if (!xs::all(invalid_1)) {
                     // 部分有效
                     alignas(batch_type::arch_type::alignment()) double x_s[batch_size], y_s[batch_size];
                     x_batch_1.store_aligned(x_s); y_batch_1.store_aligned(y_s);
                     for(int k=0; k<batch_size; ++k) {
                         if(std::isfinite(y_s[k])) {
                             out_ptr->position.x = x_s[k]; out_ptr->position.y = y_s[k];
                             out_ptr->function_index = func_idx; out_ptr++; valid_count++;
                         }
                     }
                }

                // 处理 Batch 2
                if (xs::none(invalid_2)) {
                    write_batch_to_buffer(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx);
                } else if (!xs::all(invalid_2)) {
                     alignas(batch_type::arch_type::alignment()) double x_s[batch_size], y_s[batch_size];
                     x_batch_2.store_aligned(x_s); y_batch_2.store_aligned(y_s);
                     for(int k=0; k<batch_size; ++k) {
                         if(std::isfinite(y_s[k])) {
                             out_ptr->position.x = x_s[k]; out_ptr->position.y = y_s[k];
                             out_ptr->function_index = func_idx; out_ptr++; valid_count++;
                         }
                     }
                }
            }

            // 更新 X 坐标
            x_batch_1 += step_block_vec;
            x_batch_2 += step_block_vec;
        }

        // --- 标量尾部处理 ---
        for (; i < count; ++i) {
            double x = x_start_world + (start_idx + i) * step_x;
            double y = evaluate_rpn<double>(rpn_program, x);
            if (std::isfinite(y)) {
                out_ptr->position.x = x; out_ptr->position.y = y;
                out_ptr->function_index = func_idx; out_ptr++; valid_count++;
            }
        }

        local_buffer.resize(valid_count);
    });

    // 4. 合并结果
    size_t total_valid_points = 0;
    for (const auto& buf : block_buffers) total_valid_points += buf.size();

    auto it = all_points.grow_by(total_valid_points);
    size_t offset = 0;

    for (auto& buf : block_buffers) {
        if (!buf.empty()) {
            std::move(buf.begin(), buf.end(), it + offset);
            offset += buf.size();
        }
    }
}