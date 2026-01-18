// --- 文件路径: include/functions/lerp.h ---

#ifndef LERP_H
#define LERP_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include <xsimd/xsimd.hpp>
#include "../include/graph/GeoGraph.h"
// ========================================================================
//          ↓↓↓ 数据结构定义 ↓↓↓
// ========================================================================

/**
 * @brief NDC (Normalized Device Coordinates) 映射参数包
 *
 * 用于将 World Space (double) 转换为 Clip Space (float)
 * Clip Space 范围通常为 [-1, 1]
 */
struct NDCMap {
    double center_x; // 视图中心的世界坐标 (World Center X)
    double center_y; // 视图中心的世界坐标 (World Center Y)
    double scale_x;  // X轴缩放因子: 2.0 / (ScreenW * WPPX)
    double scale_y;  // Y轴缩放因子: 2.0 / (ScreenH * WPPY)
};

// ========================================================================
//          ↓↓↓ 1. 屏幕 -> 世界 (Screen -> World) ↓↓↓
// ========================================================================
// 场景：处理鼠标事件、光栅化前的像素遍历
// 逻辑：World = Origin + Pixel * WPP

FORCE_INLINE inline Vec2 screen_to_world_inline(const Vec2& scr, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + scr.x * wppx, origin.y + scr.y * wppy };
}

FORCE_INLINE inline std::pair<batch_type, batch_type> screen_to_world_batch(const batch_type& sx, double sy, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + sx * batch_type(wppx), origin.y + batch_type(sy * wppy) };
}

// ========================================================================
//          ↓↓↓ 2. 世界 -> 裁切/NDC (World -> Clip) 核心存储函数 ↓↓↓
// ========================================================================
// 场景：渲染计算输出。
// 核心：利用 double 做减法消除大数误差，最后转 float 存入显存。

/**
 * @brief [标量版] 世界转NDC并存储 (Double Calculation -> Float Storage)
 *
 * @param out 目标 PointData 引用 (float 成员)
 * @param wx 世界坐标 X (double)
 * @param wy 世界坐标 Y (double)
 * @param map NDC 映射参数
 * @param func_idx 函数索引
 */
FORCE_INLINE inline void world_to_clip_store(
    PointData& out,
    double wx, double wy,
    const NDCMap& map,
    unsigned int func_idx
) {
    // 1. 高精度减法 (Double Precision Subtraction)
    // 这一步必须在 double 下进行，防止 "大数吃小数"
    // 例如：100000.0005 - 100000.0 = 0.0005 (Double 能保住，Float 会丢失)
    double dx = wx - map.center_x;
    double dy = wy - map.center_y;

    // 2. 缩放并转换 (Cast to Float)
    // dx * scale 的结果通常在 [-1, 1] 之间，Float 精度足以表达
    out.position.x = static_cast<float>(dx * map.scale_x);
    out.position.y = -static_cast<float>(dy * map.scale_y);
    out.function_index = func_idx;
}

/**
 * @brief [SIMD版] 批量世界转NDC并存储
 *
 * @param out_ptr 目标 PointData 数组指针
 * @param wx_batch 世界坐标 X (SIMD Double)
 * @param wy_batch 世界坐标 Y (SIMD Double)
 * @param map NDC 映射参数
 * @param func_idx 函数索引
 */
FORCE_INLINE inline void world_to_clip_store_batch(
    PointData* out_ptr,
    const batch_type& wx_batch,
    const batch_type& wy_batch,
    const NDCMap& map,
    unsigned int func_idx
) {
    // 1. 加载常量到寄存器
    batch_type b_center_x(map.center_x);
    batch_type b_center_y(map.center_y);
    batch_type b_scale_x(map.scale_x);
    batch_type b_scale_y(map.scale_y);

    // 2. SIMD 高精度计算
    batch_type ndc_x = (wx_batch - b_center_x) * b_scale_x;
    batch_type ndc_y = (wy_batch - b_center_y) * b_scale_y;

    // 3. 存储转换 (Store & Cast)
    // 由于 PointData 结构体内存布局是 {float, float, uint} (Stride=12/16)，
    // 且源数据是 double，无法直接使用 SIMD Scatter 写入。
    // 最高效的方法是先存入栈上的对齐数组，再标量写入。

    constexpr std::size_t N = batch_type::size;
    alignas(batch_type::arch_type::alignment()) double buf_x[N];
    alignas(batch_type::arch_type::alignment()) double buf_y[N];

    ndc_x.store_aligned(buf_x);
    ndc_y.store_aligned(buf_y);

    // 编译器会自动对这个短循环进行 Loop Unrolling 优化
    for (std::size_t k = 0; k < N; ++k) {
        // Double -> Float 隐式或显式转换
        out_ptr[k].position.x = static_cast<float>(buf_x[k]);
        out_ptr[k].position.y = static_cast<float>(buf_y[k]);
        out_ptr[k].function_index = func_idx;
    }
}

// ========================================================================
//          ↓↓↓ 3. 裁切/NDC -> 世界 (Clip -> World) ↓↓↓
// ========================================================================
// 场景：隐函数求根时，需要将 Clip Space 的四叉树区间映射回 World Space 代入方程。
// NDCMap 构建辅助
inline NDCMap BuildNDCMap(const ViewState& view) {
    double half_w = view.screen_width * 0.5;
    double half_h = view.screen_height * 0.5;
    Vec2 center_world = screen_to_world_inline({half_w, half_h}, view.world_origin, view.wppx, view.wppy);

    NDCMap map{};
    map.center_x = center_world.x;
    map.center_y = center_world.y;
    map.scale_x = 2.0 / (view.screen_width * view.wppx);
    map.scale_y = 2.0 / (view.screen_height * view.wppy);
    return map;
}
FORCE_INLINE inline Vec2 clip_to_world_inline(const Vec2& clip, const NDCMap& map) {
    // World = Center + NDC / Scale
    return {
        map.center_x + static_cast<double>(clip.x) / map.scale_x,
        map.center_y + static_cast<double>(clip.y) / map.scale_y
    };
}

FORCE_INLINE inline std::pair<batch_type, batch_type> clip_to_world_batch(
    const batch_type& cx,
    const batch_type& cy,
    const NDCMap& map)
{
    return {
        batch_type(map.center_x) + cx / batch_type(map.scale_x),
        batch_type(map.center_y) + cy / batch_type(map.scale_y)
    };
}

// ========================================================================
//          ↓↓↓ 隐函数求交辅助 (Marching Squares) ↓↓↓
// ========================================================================

// 隐函数求交点
FORCE_INLINE inline bool try_get_intersection_point(Vec2& out, const Vec2& p1, const Vec2& p2, double v1, double v2, const AlignedVector<RPNToken>& prog_check) {
    if ((v1 * v2 > 0.0) && (std::signbit(v1) == std::signbit(v2))) return false;
    if (std::abs(v1) >= 1e268 && std::abs(v2) >= 1e268) return false;
    double t = -v1 / (v2 - v1);
    out = {p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
    // 更新调用: 明确指定模板参数为 <double>
    double check_val = evaluate_rpn<double>(prog_check, out.x, out.y);
    return std::isfinite(check_val) && std::abs(check_val) < 1e200;
}

#endif //LERP_H