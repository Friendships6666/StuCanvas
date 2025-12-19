// --- 文件路径: src/plot/plotSegment.cpp ---

#include "../../include/plot/plotSegment.h"
#include "../../include/functions/lerp.h" // 包含 NDCMap 和 world_to_clip_store
#include <xsimd/xsimd.hpp>
#include <oneapi/tbb/parallel_invoke.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>
#include <array>

namespace {
    // =========================================================
    // SIMD 类型定义 (强制 128-bit / Batch=2，适配 WASM)
    // =========================================================
    using Simd2 = xsimd::make_sized_batch_t<double, 2>;

    // 内存对齐辅助
    template<typename T, size_t N>
    struct alignas(Simd2::arch_type::alignment()) AlignedArray2 {
        std::array<T, N> data;
        FORCE_INLINE T& operator[](size_t i) { return data[i]; }
        FORCE_INLINE T* ptr() { return data.data(); }
    };
}

void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2,
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
) {
    // 1. 视口边界准备 (用于 Liang-Barsky 裁剪)
    double wx_start = world_origin.x;
    double wy_start = world_origin.y;
    double wx_end = world_origin.x + screen_width * wppx;
    double wy_end = world_origin.y + screen_height * wppy;

    double x_min = std::min(wx_start, wx_end);
    double x_max = std::max(wx_start, wx_end);
    double y_min = std::min(wy_start, wy_end);
    double y_max = std::max(wy_start, wy_end);

    // 2. 准备 NDC 映射参数 (利用 lerp.h 中的结构)
    double half_w = screen_width * 0.5;
    double half_h = screen_height * 0.5;

    // 计算屏幕中心的世界坐标
    Vec2 center_world = screen_to_world_inline({half_w, half_h}, world_origin, wppx, wppy);

    NDCMap ndc_map;
    ndc_map.center_x = center_world.x;
    ndc_map.center_y = center_world.y;
    ndc_map.scale_x = 2.0 / (screen_width * wppx);
    // WebGPU NDC Y轴向上，配合 wppy (通常为负) 实现自动翻转
    ndc_map.scale_y = 2.0 / (screen_height * wppy);

    // 3. Liang-Barsky 初始化
    double t0 = is_segment ? 0.0 : -1.0e12; // 极大值代替无穷
    double t1 = is_segment ? 1.0 :  1.0e12;

    double dx = x2 - x1;
    double dy = y2 - y1;

    // 线程局部状态
    struct TaskResult { bool valid = true; double t_enter = -1.0e13; double t_exit = 1.0e13; };
    TaskResult res_x, res_y;

    // =========================================================
    // 4. 并行 Invoke：Liang-Barsky 核心计算
    // =========================================================
    tbb::parallel_invoke(
        // Task A: X轴裁剪 (Left / Right)
        [&]() {
            AlignedArray2<double, 2> p_arr, q_arr;
            p_arr[0] = -dx;           p_arr[1] = dx;
            q_arr[0] = x1 - x_min;    q_arr[1] = x_max - x1;

            auto v_p = xsimd::load_aligned(p_arr.ptr());
            auto v_q = xsimd::load_aligned(q_arr.ptr());

            // SIMD 除法
            auto v_r = v_q / v_p;

            for(int i=0; i<2; ++i) {
                double p_val = p_arr[i];
                double q_val = q_arr[i];
                double r_val = v_r.get(i);

                if (p_val == 0) {
                    if (q_val < 0) { res_x.valid = false; return; }
                } else if (p_val < 0) {
                    if (r_val > res_x.t_enter) res_x.t_enter = r_val;
                } else {
                    if (r_val < res_x.t_exit) res_x.t_exit = r_val;
                }
            }
        },

        // Task B: Y轴裁剪 (Bottom / Top)
        [&]() {
            AlignedArray2<double, 2> p_arr, q_arr;
            p_arr[0] = -dy;           p_arr[1] = dy;
            q_arr[0] = y1 - y_min;    q_arr[1] = y_max - y1;

            auto v_p = xsimd::load_aligned(p_arr.ptr());
            auto v_q = xsimd::load_aligned(q_arr.ptr());
            auto v_r = v_q / v_p;

            for(int i=0; i<2; ++i) {
                double p_val = p_arr[i];
                double q_val = q_arr[i];
                double r_val = v_r.get(i);

                if (p_val == 0) {
                    if (q_val < 0) { res_y.valid = false; return; }
                } else if (p_val < 0) {
                    if (r_val > res_y.t_enter) res_y.t_enter = r_val;
                } else {
                    if (r_val < res_y.t_exit) res_y.t_exit = r_val;
                }
            }
        }
    );

    // =========================================================
    // 5. 合并结果
    // =========================================================
    if (!res_x.valid || !res_y.valid) {
        results_queue->push({func_idx, {}});
        return;
    }

    double final_t0 = t0;
    double final_t1 = t1;

    if (res_x.t_enter > final_t0) final_t0 = res_x.t_enter;
    if (res_x.t_exit < final_t1)  final_t1 = res_x.t_exit;

    if (res_y.t_enter > final_t0) final_t0 = res_y.t_enter;
    if (res_y.t_exit < final_t1)  final_t1 = res_y.t_exit;

    if (final_t0 > final_t1) {
        results_queue->push({func_idx, {}});
        return;
    }

    // =========================================================
    // 6. 输出结果 (转为 CLIP 坐标)
    // =========================================================
    std::vector<PointData> final_points;
    final_points.reserve(2);

    // 计算骨架点 1 (Start)
    double p1_wx = x1 + final_t0 * dx;
    double p1_wy = y1 + final_t0 * dy;

    PointData out1;
    // 使用 lerp.h 中的通用转换函数，确保精度
    world_to_clip_store(out1, p1_wx, p1_wy, ndc_map, func_idx);
    final_points.push_back(out1);

    // 计算骨架点 2 (End) - 仅当长度足够时
    if (final_t1 - final_t0 > 1e-9) {
        double p2_wx = x1 + final_t1 * dx;
        double p2_wy = y1 + final_t1 * dy;

        PointData out2;
        world_to_clip_store(out2, p2_wx, p2_wy, ndc_map, func_idx);
        final_points.push_back(out2);
    }

    results_queue->push({func_idx, std::move(final_points)});
}