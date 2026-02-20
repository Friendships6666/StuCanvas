// --- 文件路径: src/plot/plotExplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/functions/functions.h"
#include "../../include/functions/lerp.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
    // 适配极致性能：每次处理 4 个 Batch
    constexpr size_t VM_BLOCK_SIZE = 4;

    /**
     * @brief 极致优化的 RPN 执行引擎 (On-the-fly X 累加法)
     */
    void EvaluateExplicit_SIMD_Accumulate(
        const AlignedVector<RPNToken> &tokens,
        double x_start,
        double x_step,
        batch_type *__restrict out_buffer,
        size_t num_batches,
        batch_type *__restrict workspace
    ) {
        const RPNToken *pc_base = tokens.data();
        batch_type *curr_out = out_buffer;

        // 初始化 X 向量累加器
        batch_type v_x_step(x_step);
        batch_type v_batch_step(x_step * batch_type::size);
        batch_type v_indices = get_index_vec();

        batch_type base_x_v = batch_type(x_start) + v_indices * v_x_step;

        for (size_t b = 0; b < num_batches; b += VM_BLOCK_SIZE) {
            batch_type bx0 = base_x_v;
            batch_type bx1 = bx0 + v_batch_step;
            batch_type bx2 = bx1 + v_batch_step;
            batch_type bx3 = bx2 + v_batch_step;
            base_x_v = bx3 + v_batch_step;

            const RPNToken *pc = pc_base;
            batch_type *__restrict sp = workspace;
            batch_type acc0, acc1, acc2, acc3;

            while (true) {
                const auto &token = *pc++;
                switch (token.type) {
                    case RPNTokenType::PUSH_X: {
                        sp[0] = acc0;
                        sp[1] = acc1;
                        sp[2] = acc2;
                        sp[3] = acc3;
                        sp += 4;
                        acc0 = bx0;
                        acc1 = bx1;
                        acc2 = bx2;
                        acc3 = bx3;
                        break;
                    }
                    case RPNTokenType::PUSH_CONST: {
                        sp[0] = acc0;
                        sp[1] = acc1;
                        sp[2] = acc2;
                        sp[3] = acc3;
                        sp += 4;
                        batch_type vc(token.value);
                        acc0 = vc;
                        acc1 = vc;
                        acc2 = vc;
                        acc3 = vc;
                        break;
                    }
                    case RPNTokenType::ADD: sp -= 4;
                        acc0 += sp[0];
                        acc1 += sp[1];
                        acc2 += sp[2];
                        acc3 += sp[3];
                        break;
                    case RPNTokenType::SUB: sp -= 4;
                        acc0 = sp[0] - acc0;
                        acc1 = sp[1] - acc1;
                        acc2 = sp[2] - acc2;
                        acc3 = sp[3] - acc3;
                        break;
                    case RPNTokenType::MUL: sp -= 4;
                        acc0 *= sp[0];
                        acc1 *= sp[1];
                        acc2 *= sp[2];
                        acc3 *= sp[3];
                        break;
                    case RPNTokenType::DIV: sp -= 4;
                        acc0 = sp[0] / acc0;
                        acc1 = sp[1] / acc1;
                        acc2 = sp[2] / acc2;
                        acc3 = sp[3] / acc3;
                        break;
                    case RPNTokenType::SIN: acc0 = xs::sin(acc0);
                        acc1 = xs::sin(acc1);
                        acc2 = xs::sin(acc2);
                        acc3 = xs::sin(acc3);
                        break;
                    case RPNTokenType::COS: acc0 = xs::cos(acc0);
                        acc1 = xs::cos(acc1);
                        acc2 = xs::cos(acc2);
                        acc3 = xs::cos(acc3);
                        break;
                    case RPNTokenType::STOP: {
                        curr_out[0] = acc0;
                        curr_out[1] = acc1;
                        curr_out[2] = acc2;
                        curr_out[3] = acc3;
                        curr_out += 4;
                        goto block_done;
                    }
                    default: break;
                }
            }
        block_done:;
        }
    }

    /**
     * @brief 世界空间裁剪与补点器 (修正版)
     */
    void clip_and_interpolate_world(
        std::vector<PointData> &out,
        double x1, double y1, double x2, double y2,
        const ViewState &view,
        unsigned int func_idx
    ) {
        // 1. 获取世界坐标系下的 y 边界
        const double margin_y = view.half_h * view.wpp * 1.05;
        const double y_min = view.offset_y - margin_y;
        const double y_max = view.offset_y + margin_y;

        // --- 逻辑 A：Trivial Reject (两点都在同一侧外) ---
        if ((y1 > y_max && y2 > y_max) || (y1 < y_min && y2 < y_min)) return;

        double t0 = 0.0, t1 = 1.0;
        double dy = y2 - y1;

        // --- 逻辑 B：检测是否需要裁剪 (如果有任何一点在屏幕外) ---
        bool trivial_accept = (y1 >= y_min && y1 <= y_max && y2 >= y_min && y2 <= y_max);

        if (!trivial_accept) {
            // 使用 Liang-Barsky 算出线段进入和离开 y 边界的 t 值
            // P(t) = P1 + t * (P2 - P1)
            auto clip_t = [&](double p, double q) -> bool {
                if (std::abs(p) < 1e-15) return q >= 0;
                double r = q / p;
                if (p < 0) {
                    // 进入边界
                    if (r > t1) return false;
                    if (r > t0) t0 = r;
                } else {
                    // 离开边界
                    if (r < t0) return false;
                    if (r < t1) t1 = r;
                }
                return true;
            };

            // 裁剪 y_min 和 y_max
            if (!clip_t(-dy, y1 - y_min)) return;
            if (!clip_t(dy, y_max - y1)) return;
        }

        // 算出裁剪后的实际可见部分世界坐标
        double dx = x2 - x1;
        double cx1 = x1 + t0 * dx;
        double cy1 = y1 + t0 * dy;
        double cx2 = x1 + t1 * dx;
        double cy2 = y1 + t1 * dy;

        // --- 逻辑 C：插值补点 ---
        // 计算可见部分在屏幕上的像素跨度 (主要考察 y 轴)
        double dist_pix = std::abs(cy2 - cy1) * view.inv_wpp;

        // 分段步数：每 0.5 像素补一个点
        int steps = std::max(1, static_cast<int>(std::ceil(dist_pix * 2.0)));
        steps = std::min(steps, 1024); // 安全锁，防止函数斜率趋于无穷时内存爆炸

        double step_inv = 1.0 / steps;
        for (int i = 0; i <= steps; ++i) {
            double t = i * step_inv;
            double wx = cx1 + t * (cx2 - cx1);
            double wy = cy1 + t * (cy2 - cy1);

            // 最终投影到 Clip 空间存储
            Vec2i cp = view.WorldToClip(wx, wy);
            out.push_back({cp.x, cp.y});
        }
    }
}

/**
 * @brief 显函数 Plotter 主入口
 */
void process_explicit_chunk(
    double x_start_world, double x_end_world,
    const AlignedVector<RPNToken> &rpn_program,
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData> > &results_queue,
    unsigned int func_idx,
    const ViewState &view
) {
    double x_step = view.wpp;
    int pixel_width = static_cast<int>(std::ceil((x_end_world - x_start_world) / x_step));
    if (pixel_width <= 0) return;

    // 1. 内存准备：向上对齐到 VM_BLOCK_SIZE
    size_t num_batches = (pixel_width + batch_type::size - 1) / batch_type::size;
    num_batches = (num_batches + VM_BLOCK_SIZE - 1) / VM_BLOCK_SIZE * VM_BLOCK_SIZE;

    AlignedVector<batch_type> y_results(num_batches);
    std::array<batch_type, 256> vm_stack;

    // 2. 寄存器 X 累加 + SIMD VM 解算
    EvaluateExplicit_SIMD_Accumulate(
        rpn_program, x_start_world, x_step,
        y_results.data(), num_batches, vm_stack.data()
    );

    // 3. 处理解算出的“珍珠链”
    std::vector<PointData> final_points;
    // 考虑到插值，预留 2 倍空间
    final_points.reserve(pixel_width * 2);

    double *raw_y = reinterpret_cast<double *>(y_results.data());

    double last_wx = x_start_world;
    double last_wy = raw_y[0];
    bool last_valid = std::isfinite(last_wy);

    // 遍历采样点线段 [P_i, P_{i+1}]
    for (int i = 1; i < pixel_width; ++i) {
        double curr_wx = x_start_world + i * x_step;
        double curr_wy = raw_y[i];
        bool curr_valid = std::isfinite(curr_wy);

        if (last_valid && curr_valid) {
            // 线段两端数学上连续：执行带穿透检测的世界裁剪
            clip_and_interpolate_world(final_points, last_wx, last_wy, curr_wx, curr_wy, view, func_idx);
        } else if (!last_valid && curr_valid) {
            // 数学断裂点（从 NaN 恢复）：仅投射当前采样点
            Vec2i p = view.WorldToClip(curr_wx, curr_wy);
            if (std::abs(p.y) <= 32767) final_points.push_back({p.x, p.y});
        }

        last_wx = curr_wx;
        last_wy = curr_wy;
        last_valid = curr_valid;
    }

    // 4. 提交结果
    if (!final_points.empty()) {
        results_queue.push(std::move(final_points));
    }
}
