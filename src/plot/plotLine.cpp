// --- 文件路径: src/plot/plotLine.cpp ---

#include "../../include/plot/plotLine.h"
#include <xsimd/xsimd.hpp>
#include <oneapi/tbb/parallel_invoke.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>
#include <array>

namespace {
    // =========================================================
    // 局部类型定义 (避免与 pch.h 中的 batch_type 冲突)
    // =========================================================

    // 强制使用 128-bit (2 doubles) 的 SIMD 类型
    // 无论全局架构是 AVX512 还是别的，这里只用 2 个通道
    using LineSimd2 = xsimd::make_sized_batch_t<double, 2>;

    // 对应的常量
    constexpr std::size_t LINE_SIMD_SIZE = 2;

    // 专用的内存对齐辅助结构
    // 注意：使用 LineSimd2::arch_type 来获取对齐要求
    template<typename T, size_t N>
    struct alignas(LineSimd2::arch_type::alignment()) AlignedArray2 {
        std::array<T, N> data;

        FORCE_INLINE T& operator[](size_t i) { return data[i]; }
        FORCE_INLINE const T& operator[](size_t i) const { return data[i]; }
        FORCE_INLINE T* ptr() { return data.data(); }
        FORCE_INLINE const T* ptr() const { return data.data(); }
    };

    struct RawPoint {
        double x, y;
    };

    inline double dist_sq(const RawPoint& a, const RawPoint& b) {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
}

void process_line_equation(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double A, double B, double C,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
) {
    // 1. 视口边界准备 (标量计算)
    double x1 = world_origin.x;
    double x2 = world_origin.x + screen_width * wppx;
    double y1 = world_origin.y;
    double y2 = world_origin.y + screen_height * wppy;

    double x_min = std::min(x1, x2);
    double x_max = std::max(x1, x2);
    double y_min = std::min(y1, y2);
    double y_max = std::max(y1, y2);

    const double EPS = 1e-5 * (x_max - x_min);
    double chk_x_min = x_min - EPS, chk_x_max = x_max + EPS;
    double chk_y_min = y_min - EPS, chk_y_max = y_max + EPS;

    // 线程局部存储结果
    RawPoint res_vertical[2];
    int count_v = 0;

    RawPoint res_horizontal[2];
    int count_h = 0;

    // =========================================================
    // 2. 并行 Invoke
    // =========================================================
    tbb::parallel_invoke(
        // -----------------------------------------------------
        // 任务 A: 计算左右边界 (Vertical) -> 求 Y
        // -----------------------------------------------------
        [&]() {
            // 使用我们重命名的 LineSimd2 和常量
            constexpr size_t ARR_SIZE = LINE_SIMD_SIZE;

            AlignedArray2<double, ARR_SIZE> v_neg_c;
            AlignedArray2<double, ARR_SIZE> v_A;
            AlignedArray2<double, ARR_SIZE> v_bound;
            AlignedArray2<double, ARR_SIZE> v_B;
            AlignedArray2<double, ARR_SIZE> v_res;

            // 填充数据：(-C - A*x) / B
            v_neg_c[0] = -C;    v_A[0] = A;    v_bound[0] = x_min; v_B[0] = B; // Left
            v_neg_c[1] = -C;    v_A[1] = A;    v_bound[1] = x_max; v_B[1] = B; // Right

            // SIMD 计算
            auto b_nc = xsimd::load_aligned(v_neg_c.ptr());
            auto b_a  = xsimd::load_aligned(v_A.ptr());
            auto b_x  = xsimd::load_aligned(v_bound.ptr());
            auto b_b  = xsimd::load_aligned(v_B.ptr());

            auto res  = (b_nc - b_a * b_x) / b_b;

            res.store_aligned(v_res.ptr());

            // 标量过滤
            double y = v_res[0];
            if (std::isfinite(y) && y >= chk_y_min && y <= chk_y_max) {
                res_vertical[count_v++] = {x_min, y};
            }
            y = v_res[1];
            if (std::isfinite(y) && y >= chk_y_min && y <= chk_y_max) {
                res_vertical[count_v++] = {x_max, y};
            }
        },

        // -----------------------------------------------------
        // 任务 B: 计算上下边界 (Horizontal) -> 求 X
        // -----------------------------------------------------
        [&]() {
            constexpr size_t ARR_SIZE = LINE_SIMD_SIZE;

            AlignedArray2<double, ARR_SIZE> v_neg_c;
            AlignedArray2<double, ARR_SIZE> v_B;
            AlignedArray2<double, ARR_SIZE> v_bound;
            AlignedArray2<double, ARR_SIZE> v_A;
            AlignedArray2<double, ARR_SIZE> v_res;

            // 填充数据：(-C - B*y) / A
            v_neg_c[0] = -C;    v_B[0] = B;    v_bound[0] = y_min; v_A[0] = A; // Top
            v_neg_c[1] = -C;    v_B[1] = B;    v_bound[1] = y_max; v_A[1] = A; // Bottom

            // SIMD 计算
            auto b_nc = xsimd::load_aligned(v_neg_c.ptr());
            auto b_b  = xsimd::load_aligned(v_B.ptr());
            auto b_y  = xsimd::load_aligned(v_bound.ptr());
            auto b_a  = xsimd::load_aligned(v_A.ptr());

            auto res  = (b_nc - b_b * b_y) / b_a;

            res.store_aligned(v_res.ptr());

            // 标量过滤
            double x = v_res[0];
            if (std::isfinite(x) && x >= chk_x_min && x <= chk_x_max) {
                res_horizontal[count_h++] = {x, y_min};
            }
            x = v_res[1];
            if (std::isfinite(x) && x >= chk_x_min && x <= chk_x_max) {
                res_horizontal[count_h++] = {x, y_max};
            }
        }
    );

    // =========================================================
    // 3. 结果合并与去重
    // =========================================================
    RawPoint candidates[4];
    int total_count = 0;

    for(int i=0; i<count_v; ++i) candidates[total_count++] = res_vertical[i];
    for(int i=0; i<count_h; ++i) candidates[total_count++] = res_horizontal[i];

    if (total_count == 0) {
        results_queue->push({func_idx, {}});
        return;
    }

    std::vector<PointData> final_points;
    final_points.reserve(2);

    auto add_p = [&](const RawPoint& p) {
        final_points.push_back({p.x - offset_x, p.y - offset_y, func_idx});
    };

    if (total_count <= 2) {
        if (total_count == 2 && dist_sq(candidates[0], candidates[1]) < 1e-10) total_count = 1;
        for (int i = 0; i < total_count; ++i) add_p(candidates[i]);
    } else {
        // 找出最远点对 (合并角点)
        double max_d2 = -1.0;
        int idx1 = 0, idx2 = 0;
        bool found = false;

        for(int i=0; i<total_count; ++i) {
            for(int j=i+1; j<total_count; ++j) {
                double d2 = dist_sq(candidates[i], candidates[j]);
                if (d2 > max_d2) {
                    max_d2 = d2;
                    idx1 = i; idx2 = j;
                    found = true;
                }
            }
        }

        if (found && max_d2 > 1e-10) {
            add_p(candidates[idx1]);
            add_p(candidates[idx2]);
        } else {
            add_p(candidates[0]);
        }
    }

    results_queue->push({func_idx, std::move(final_points)});
}