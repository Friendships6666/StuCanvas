// --- 文件路径: src/plot/plotParametric.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/functions/functions.h"

#include <oneapi/tbb/parallel_for.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iterator>

// 采样密度：每单位 t 长度采样 20 个点
constexpr double SAMPLES_PER_UNIT_T = 20.0;

// 分块大小：1024 保证有足够的计算量来摊销线程调度开销，同时适配 Loop Unrolling
constexpr int BLOCK_GRAIN_SIZE = 1024;

// 强制内联辅助函数：将 SIMD 寄存器数据写入内存
template<typename T>
FORCE_INLINE void write_parametric_batch(
    const T& x_batch, const T& y_batch,
    PointData*& out_ptr, int& valid_count,
    unsigned int func_idx)
{
    constexpr int batch_size = T::size;
    alignas(T::arch_type::alignment()) double x_store[batch_size];
    alignas(T::arch_type::alignment()) double y_store[batch_size];

    x_batch.store_aligned(x_store);
    y_batch.store_aligned(y_store);

    // 编译器会自动将此循环展开为连续的内存写入指令
    for (int k = 0; k < batch_size; ++k) {
        out_ptr->position.x = x_store[k];
        out_ptr->position.y = y_store[k];
        out_ptr->function_index = func_idx;
        out_ptr++;
    }
    valid_count += batch_size;
}

void process_parametric_chunk(
    const AlignedVector<RPNToken>& rpn_x,
    const AlignedVector<RPNToken>& rpn_y,
    double t_min, double t_max,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx
) {
    // 1. 计算总采样点数
    // 逻辑：区间长度 * 20 取整
    double t_range = t_max - t_min;
    if (t_range <= 0) return; // 范围无效直接返回

    int total_samples = static_cast<int>(std::floor(t_range * SAMPLES_PER_UNIT_T));
    if (total_samples <= 0) total_samples = 100; // 兜底，防止过少

    // 计算 t 的步长
    double step_t = t_range / static_cast<double>(total_samples);

    // 2. 准备分块缓冲区
    int num_blocks = (total_samples + BLOCK_GRAIN_SIZE - 1) / BLOCK_GRAIN_SIZE;
    std::vector<std::vector<PointData>> block_buffers(num_blocks);

    // 3. TBB 并行计算
    oneapi::tbb::parallel_for(0, num_blocks, [&](int block_idx) {

        int start_idx = block_idx * BLOCK_GRAIN_SIZE;
        int count = std::min(BLOCK_GRAIN_SIZE, total_samples - start_idx);

        auto& local_buffer = block_buffers[block_idx];
        local_buffer.resize(count); // 直接分配最大可能空间
        PointData* out_ptr = local_buffer.data();
        int valid_count = 0;

        int i = 0;
        constexpr int batch_size = batch_type::size;

        // 2x Loop Unrolling 参数
        constexpr int unroll_factor = 2;
        constexpr int step_stride = batch_size * unroll_factor;

        // 预计算 SIMD 向量
        const batch_type step_vec = batch_type(step_t);
        const batch_type step_block_vec = batch_type(step_t * step_stride); // 外层循环步进
        const batch_type step_offset_vec = batch_type(step_t * batch_size); // Batch 2 的 t 偏移
        const batch_type index_vec = get_index_vec(); // [0, 1, 2...]

        // 初始 t 向量: t_min + (BlockStart + [0,1..]) * step
        batch_type t_batch_1 = batch_type(t_min) +
                               (batch_type((double)start_idx) + index_vec) * step_vec;
        batch_type t_batch_2 = t_batch_1 + step_offset_vec;

        // --- SIMD ILP 路径 ---
        // 每次循环处理 2 * batch_size 个点
        for (; i + step_stride <= count; i += step_stride) {

            // 1. 并行计算 X 坐标
            batch_type x_batch_1 = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_batch_1);
            batch_type x_batch_2 = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_batch_2);

            // 2. 并行计算 Y 坐标 (利用 ILP 掩盖 X 的计算延迟)
            batch_type y_batch_1 = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_batch_1);
            batch_type y_batch_2 = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_batch_2);

            // 3. 检查有效性 (x 和 y 都必须是 finite)
            auto invalid_1 = xs::isnan(x_batch_1) | xs::isinf(x_batch_1) |
                             xs::isnan(y_batch_1) | xs::isinf(y_batch_1);

            auto invalid_2 = xs::isnan(x_batch_2) | xs::isinf(x_batch_2) |
                             xs::isnan(y_batch_2) | xs::isinf(y_batch_2);

            // 4. Fast Path: 全部有效
            if (xs::none(invalid_1) && xs::none(invalid_2)) {
                write_parametric_batch(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx);
                write_parametric_batch(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx);
            }
            else {
                // Slow Path: 存在无效值，需逐个处理
                // 处理 Batch 1
                if (xs::none(invalid_1)) {
                    write_parametric_batch(x_batch_1, y_batch_1, out_ptr, valid_count, func_idx);
                } else if (!xs::all(invalid_1)) {
                    alignas(batch_type::arch_type::alignment()) double x_s[batch_size], y_s[batch_size];
                    x_batch_1.store_aligned(x_s);
                    y_batch_1.store_aligned(y_s);
                    for(int k=0; k<batch_size; ++k) {
                        if(std::isfinite(x_s[k]) && std::isfinite(y_s[k])) {
                            out_ptr->position.x = x_s[k];
                            out_ptr->position.y = y_s[k];
                            out_ptr->function_index = func_idx;
                            out_ptr++; valid_count++;
                        }
                    }
                }

                // 处理 Batch 2
                if (xs::none(invalid_2)) {
                    write_parametric_batch(x_batch_2, y_batch_2, out_ptr, valid_count, func_idx);
                } else if (!xs::all(invalid_2)) {
                    alignas(batch_type::arch_type::alignment()) double x_s[batch_size], y_s[batch_size];
                    x_batch_2.store_aligned(x_s);
                    y_batch_2.store_aligned(y_s);
                    for(int k=0; k<batch_size; ++k) {
                        if(std::isfinite(x_s[k]) && std::isfinite(y_s[k])) {
                            out_ptr->position.x = x_s[k];
                            out_ptr->position.y = y_s[k];
                            out_ptr->function_index = func_idx;
                            out_ptr++; valid_count++;
                        }
                    }
                }
            }

            // 更新 t
            t_batch_1 += step_block_vec;
            t_batch_2 += step_block_vec;
        }

        // --- 标量尾部处理 ---
        for (; i < count; ++i) {
            double t = t_min + (start_idx + i) * step_t;
            // 注意：evaluate_rpn 最后一个参数是 t_param
            double x = evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t);
            double y = evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t);

            if (std::isfinite(x) && std::isfinite(y)) {
                out_ptr->position.x = x;
                out_ptr->position.y = y;
                out_ptr->function_index = func_idx;
                out_ptr++; valid_count++;
            }
        }

        // 收缩 buffer 大小
        local_buffer.resize(valid_count);
    });

    // 4. 合并结果 (保持 t 的有序性)
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