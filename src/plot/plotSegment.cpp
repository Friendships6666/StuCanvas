// --- 文件路径: src/plot/plotSegment.cpp ---

#include "../../include/plot/plotCall.h"
#include "../../include/functions/lerp.h"
#include <xsimd/xsimd.hpp>
#include <oneapi/tbb/parallel_invoke.h>
#include <algorithm>
#include <vector>
#include <cmath>

namespace {
    // 强制使用 128-bit (2 doubles) 适配 WASM
    using Simd2 = xsimd::make_sized_batch_t<double, 2>;

    template<typename T, size_t N>
    struct alignas(Simd2::arch_type::alignment()) AlignedArray2 {
        std::array<T, N> data;
        FORCE_INLINE T& operator[](size_t i) { return data[i]; }
        T* ptr() { return data.data(); }
    };

    // 辅助：Liang-Barsky 判定结果存储
    struct LBResult {
        bool rejected = false;
        double t_enter = -1e15;
        double t_exit = 1e15;
    };
}

/**
 * @brief 修正后的 process_two_point_line
 * 放弃 ABC 模式，使用参数化两点式裁剪 + 密度插值
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
    double t_start = is_segment ? 0.0 : -1.0e9;
    double t_end   = is_segment ? 1.0 : 1.0e9;

    LBResult res_x, res_y;

    // =========================================================
    // 3. TBB 并行判定：Liang-Barsky 核心
    // =========================================================
    tbb::parallel_invoke(
        // Task A: 处理 X 轴左右边界
        [&]() {
            AlignedArray2<double, 2> p, q;
            p[0] = -dx;          q[0] = x1 - x_min; // 左边界
            p[1] = dx;           q[1] = x_max - x1; // 右边界

            auto v_p = xsimd::load_aligned(p.ptr());
            auto v_q = xsimd::load_aligned(q.ptr()); // 注意：此处根据实际变量名应为 q.ptr()
            auto v_r = xsimd::load_aligned(q.ptr()) / v_p;

            for (int i = 0; i < 2; ++i) {
                double pk = p[i];
                double qk = q[i];
                double rk = v_r.get(i);
                if (pk < 0) { // 进入
                    if (rk > res_x.t_exit) { res_x.rejected = true; return; }
                    if (rk > res_x.t_enter) res_x.t_enter = rk;
                } else if (pk > 0) { // 离开
                    if (rk < res_x.t_enter) { res_x.rejected = true; return; }
                    if (rk < res_x.t_exit) res_x.t_exit = rk;
                } else if (qk < 0) { // 平行且在外
                    res_x.rejected = true; return;
                }
            }
        },
        // Task B: 处理 Y 轴上下边界
        [&]() {
            AlignedArray2<double, 2> p, q;
            p[0] = -dy;          q[0] = y1 - y_min; // 下边界
            p[1] = dy;           q[1] = y_max - y1; // 上边界

            auto v_p = xsimd::load_aligned(p.ptr());
            auto v_r = xsimd::load_aligned(q.ptr()) / v_p;

            for (int i = 0; i < 2; ++i) {
                double pk = p[i];
                double qk = q[i];
                double rk = v_r.get(i);
                if (pk < 0) {
                    if (rk > res_y.t_exit) { res_y.rejected = true; return; }
                    if (rk > res_y.t_enter) res_y.t_enter = rk;
                } else if (pk > 0) {
                    if (rk < res_y.t_enter) { res_y.rejected = true; return; }
                    if (rk < res_y.t_exit) res_y.t_exit = rk;
                } else if (qk < 0) {
                    res_y.rejected = true; return;
                }
            }
        }
    );

    // =========================================================
    // 4. 合并裁剪范围
    // =========================================================
    if (res_x.rejected || res_y.rejected) {
        results_queue->push({func_idx, {}});
        return;
    }

    double final_t0 = std::max({t_start, res_x.t_enter, res_y.t_enter});
    double final_t1 = std::min({t_end, res_x.t_exit, res_y.t_exit});

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

    // 算出该线段在屏幕上占据的像素长度
    float dx_pixel = (cx2 - cx1) * (float)screen_width * 0.5f;
    float dy_pixel = (cy2 - cy1) * (float)screen_height * 0.5f;
    float pixel_dist = 0.5*std::sqrt(dx_pixel * dx_pixel + dy_pixel * dy_pixel);

    // 步长：0.4 像素 (确保满足 < 0.5 像素的要求)
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