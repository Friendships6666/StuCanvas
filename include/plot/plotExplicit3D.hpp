#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <xsimd/xsimd.hpp>
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <Eigen/Dense>
#include "../graph/GeoGraph.h"
#include "../pch.h"
namespace xs = xsimd;
using batch_type = xs::batch<double>;

// --- 极致优化的 4路 VM 引擎 ---
FORCE_INLINE void EvaluateRPN_Pipelined(
    const RPNToken *__restrict pc_start,
    const batch_type &bx0, const batch_type &by0,
    const batch_type &bx1, const batch_type &by1,
    const batch_type &bx2, const batch_type &by2,
    const batch_type &bx3, const batch_type &by3,
    batch_type &out0, batch_type &out1, batch_type &out2, batch_type &out3,
    batch_type *__restrict sp_base
) {
    batch_type *__restrict sp = sp_base;
    batch_type acc0, acc1, acc2, acc3;
    const RPNToken *pc = pc_start;

    while (true) {
        const auto& t = *pc++;
        switch (t.type) {
            case RPNTokenType::PUSH_X:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                acc0=bx0; acc1=bx1; acc2=bx2; acc3=bx3; break;
            case RPNTokenType::PUSH_Y:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                acc0=by0; acc1=by1; acc2=by2; acc3=by3; break;
            case RPNTokenType::PUSH_CONST:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                { batch_type v(t.value); acc0=v; acc1=v; acc2=v; acc3=v; } break;
            case RPNTokenType::ADD: sp-=4; acc0+=sp[0]; acc1+=sp[1]; acc2+=sp[2]; acc3+=sp[3]; break;
            case RPNTokenType::SUB: sp-=4; acc0=sp[0]-acc0; acc1=sp[1]-acc1; acc2=sp[2]-acc2; acc3=sp[3]-acc3; break;
            case RPNTokenType::MUL: sp-=4; acc0*=sp[0]; acc1*=sp[1]; acc2*=sp[2]; acc3*=sp[3]; break;
            case RPNTokenType::DIV: sp-=4; acc0=sp[0]/acc0; acc1=sp[1]/acc1; acc2=sp[2]/acc2; acc3=sp[3]/acc3; break;
            case RPNTokenType::SIN: acc0=xs::sin(acc0); acc1=xs::sin(acc1); acc2=xs::sin(acc2); acc3=xs::sin(acc3); break;
            case RPNTokenType::COS: acc0=xs::cos(acc0); acc1=xs::cos(acc1); acc2=xs::cos(acc2); acc3=xs::cos(acc3); break;
            case RPNTokenType::SQRT:acc0=xs::sqrt(acc0);acc1=xs::sqrt(acc1);acc2=xs::sqrt(acc2);acc3=xs::sqrt(acc3);break;
            case RPNTokenType::STOP: out0=acc0; out1=acc1; out2=acc2; out3=acc3; return;
            default: break;
        }
    }
}

// 单路 Fallback 引擎，用于处理流水线尾部不满足 4 batch 的散点
FORCE_INLINE double EvaluateRPN_Scalar(const RPNToken* pc, double x, double y) {
    double stack[64]; uint32_t sp = 0;
    while(true) {
        const auto& t = *pc++;
        switch(t.type) {
            case RPNTokenType::PUSH_X: stack[sp++] = x; break;
            case RPNTokenType::PUSH_Y: stack[sp++] = y; break;
            case RPNTokenType::PUSH_CONST: stack[sp++] = t.value; break;
            case RPNTokenType::ADD: --sp; stack[sp-1] += stack[sp]; break;
            case RPNTokenType::SUB: --sp; stack[sp-1] -= stack[sp]; break;
            case RPNTokenType::MUL: --sp; stack[sp-1] *= stack[sp]; break;
            case RPNTokenType::DIV: --sp; stack[sp-1] /= stack[sp]; break;
            case RPNTokenType::SIN: stack[sp-1] = std::sin(stack[sp-1]); break;
            case RPNTokenType::COS: stack[sp-1] = std::cos(stack[sp-1]); break;
            case RPNTokenType::SQRT:stack[sp-1] = std::sqrt(stack[sp-1]); break;
            case RPNTokenType::STOP: return stack[0];
            default: break;
        }
    }
}

struct ThreadLocalCache {
    std::vector<PointData3D> points;
    AlignedVector<batch_type> vm_stack;
    ThreadLocalCache() : vm_stack(128) { points.reserve(65536); }
};

inline void plotExplicit3D(
    const AlignedVector<RPNToken> &rpn_program,
    tbb::concurrent_bounded_queue<std::vector<PointData3D>> &results_queue,
    unsigned int func_idx,
    const ViewState3D &view,
    bool use_multicore
) {
    constexpr double HALF_SIDE = 50.0;
    constexpr double WORLD_STEP = 0.3;

    const double cam_z = view.eye.z();
    if (HALF_SIDE <= 1e-6 || std::abs(cam_z) > HALF_SIDE) {
        results_queue.push({});
        return;
    }

    // --- 修正 1：计算精确的格点数量，使用整数索引控制循环 ---
    const double start_x_base = std::floor((view.eye.x() - HALF_SIDE) / WORLD_STEP) * WORLD_STEP;
    const double start_y_base = std::floor((view.eye.y() - HALF_SIDE) / WORLD_STEP) * WORLD_STEP;

    const int x_count = static_cast<int>((HALF_SIDE * 2.0) / WORLD_STEP) + 1;
    const int y_count = static_cast<int>((HALF_SIDE * 2.0) / WORLD_STEP) + 1;

    auto core_logic = [&](int x_idx_start, int x_idx_end, ThreadLocalCache& cache) {
        constexpr size_t N = batch_type::size;
        constexpr size_t BLOCK_SIZE = N * 4; // 32个点一组流水线

        alignas(64) double rx[N], ry[N], rz[N];
        const batch_type v_step_y(WORLD_STEP);
        const batch_type v_step_y_batch(WORLD_STEP * N);
        alignas(64) double idx_raw[N];
        for(size_t i=0; i<N; ++i) idx_raw[i] = (double)i;
        const batch_type v_indices = batch_type::load_aligned(idx_raw);

        for (int ix = x_idx_start; ix < x_idx_end; ++ix) {
            double cur_x = start_x_base + ix * WORLD_STEP;
            batch_type bx(cur_x);

            int iy = 0;
            // --- 修正 2：流水线主循环（处理每列的 32 整数倍点） ---
            for (; iy <= y_count - (int)BLOCK_SIZE; iy += BLOCK_SIZE) {
                double base_y = start_y_base + iy * WORLD_STEP;
                batch_type by0 = batch_type(base_y) + v_indices * v_step_y;
                batch_type by1 = by0 + v_step_y_batch;
                batch_type by2 = by1 + v_step_y_batch;
                batch_type by3 = by2 + v_step_y_batch;

                batch_type bz0, bz1, bz2, bz3;
                EvaluateRPN_Pipelined(rpn_program.data(), bx, by0, bx, by1, bx, by2, bx, by3, bz0, bz1, bz2, bz3, cache.vm_stack.data());

                auto process_b = [&](const batch_type& bX, const batch_type& bY, const batch_type& bZ) {
                    bX.store_aligned(rx); bY.store_aligned(ry); bZ.store_aligned(rz);
                    for(size_t i=0; i<N; ++i) {
                        if (rz[i] >= cam_z - HALF_SIDE && rz[i] <= cam_z + HALF_SIDE) {
                            auto clip = view.WorldToClip({rx[i], ry[i], rz[i]});
                            if (clip.z != -32768) cache.points.push_back({clip.x, clip.y, clip.z, 0});
                        }
                    }
                };
                process_b(bx, by0, bz0); process_b(bx, by1, bz1);
                process_b(bx, by2, bz2); process_b(bx, by3, bz3);
            }

            // --- 修正 3：流水线尾部清理（补全这一列最后剩下的散点） ---
            for (; iy < y_count; ++iy) {
                double cur_y = start_y_base + iy * WORLD_STEP;
                double cur_z = EvaluateRPN_Scalar(rpn_program.data(), cur_x, cur_y);
                if (cur_z >= cam_z - HALF_SIDE && cur_z <= cam_z + HALF_SIDE) {
                    auto clip = view.WorldToClip({cur_x, cur_y, cur_z});
                    if (clip.z != -32768) cache.points.push_back({clip.x, clip.y, clip.z, 0});
                }
            }
        }
    };

    if (!use_multicore) {
        thread_local static ThreadLocalCache single_cache;
        single_cache.points.clear();
        core_logic(0, x_count, single_cache);
        results_queue.push(single_cache.points);
    }
    else {
        // 使用非静态 ETS 避免线程复用时的跨帧污染
        tbb::enumerable_thread_specific<ThreadLocalCache> ets_cache;
        tbb::parallel_for(tbb::blocked_range<int>(0, x_count, 8), [&](const tbb::blocked_range<int>& r) {
            core_logic(r.begin(), r.end(), ets_cache.local());
        });

        std::vector<PointData3D> combined;
        size_t total = 0;
        for (const auto& c : ets_cache) total += c.points.size();
        combined.reserve(total);
        for (auto& c : ets_cache) {
            combined.insert(combined.end(), c.points.begin(), c.points.end());
            c.points.clear();
        }
        results_queue.push(std::move(combined));
    }
}