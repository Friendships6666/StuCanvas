// --- 文件路径: include/functions/lerp.h ---

#ifndef LERP_H
#define LERP_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"

// ========================================================================
//          ↓↓↓ 数据结构定义 ↓↓↓
// ========================================================================

/**
 * @brief NDC (Normalized Device Coordinates) 映射参数
 * 用于在 World Space (double) 和 Clip Space (float/double) 之间转换
 * Clip Space 范围通常为 [-1, 1]
 */
struct NDCMap {
    double center_x; // 视图中心的世界坐标
    double center_y;
    double scale_x;  // 缩放因子 X: 2.0 / (ScreenW * WPPX)
    double scale_y;  // 缩放因子 Y: 2.0 / (ScreenH * WPPY)
};

// ========================================================================
//          ↓↓↓ 1. 屏幕 -> 世界 (Screen -> World) ↓↓↓
// ========================================================================
// 场景：鼠标事件处理、像素遍历

FORCE_INLINE Vec2 screen_to_world_inline(const Vec2& scr, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + scr.x * wppx, origin.y + scr.y * wppy };
}

FORCE_INLINE std::pair<batch_type, batch_type> screen_to_world_batch(const batch_type& sx, double sy, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + sx * batch_type(wppx), origin.y + batch_type(sy * wppy) };
}

// ========================================================================
//          ↓↓↓ 2. 世界 -> 裁切/NDC (World -> Clip) ↓↓↓
// ========================================================================
// 场景：渲染计算。核心在于利用 double 做减法，消除大数误差。
// 公式: NDC = (World - Center) * Scale

// [函数 1] 标量版 World -> Clip
FORCE_INLINE Vec2 world_to_clip_inline(const Vec2& world_pos, const NDCMap& map) {
    return {
        (world_pos.x - map.center_x) * map.scale_x,
        (world_pos.y - map.center_y) * map.scale_y
    };
}

// [函数 2] SIMD版 World -> Clip
FORCE_INLINE std::pair<batch_type, batch_type> world_to_clip_batch(
    const batch_type& wx,
    const batch_type& wy,
    const NDCMap& map)
{
    // 1. 高精度减法 (double precision subtraction)
    // 这一步消除了 Offset 的影响，将大坐标变成了相对于屏幕中心的小坐标
    batch_type dx = wx - batch_type(map.center_x);
    batch_type dy = wy - batch_type(map.center_y);

    // 2. 缩放映射到 [-1, 1]
    return {
        dx * batch_type(map.scale_x),
        dy * batch_type(map.scale_y)
    };
}

// [辅助函数] 专用存储函数：World(double) -> PointData(float)
// 这是为了方便 plotExplicit 等模块直接写入结果
FORCE_INLINE void world_to_clip_store(
    PointData& out,
    double wx, double wy,
    const NDCMap& map,
    unsigned int func_idx
) {
    // 必须先在 double 精度下计算差值
    double dx = wx - map.center_x;
    double dy = wy - map.center_y;

    // 然后安全地转为 float，因为此时数值范围已经很小了 (约 -1 到 1)
    out.position.x = static_cast<float>(dx * map.scale_x);
    out.position.y = static_cast<float>(dy * map.scale_y);
    out.function_index = func_idx;
}

// ========================================================================
//          ↓↓↓ 3. 裁切/NDC -> 世界 (Clip -> World) ↓↓↓
// ========================================================================
// 场景：隐函数区间估算 (Clip Space 遍历 -> World Space 求值)
// 公式: World = Center + NDC / Scale

// [函数 3] 标量版 Clip -> World
FORCE_INLINE Vec2 clip_to_world_inline(const Vec2& clip, const NDCMap& map) {
    return {
        map.center_x + clip.x / map.scale_x,
        map.center_y + clip.y / map.scale_y
    };
}

// [函数 4] SIMD版 Clip -> World
FORCE_INLINE std::pair<batch_type, batch_type> clip_to_world_batch(
    const batch_type& cx,
    const batch_type& cy,
    const NDCMap& map)
{
    return {
        batch_type(map.center_x) + cx / batch_type(map.scale_x),
        batch_type(map.center_y) + cy / batch_type(map.scale_y)
    };
}

// 隐函数求交点
FORCE_INLINE bool try_get_intersection_point(Vec2& out, const Vec2& p1, const Vec2& p2, double v1, double v2, const AlignedVector<RPNToken>& prog_check) {
    if ((v1 * v2 > 0.0) && (std::signbit(v1) == std::signbit(v2))) return false;
    if (std::abs(v1) >= 1e268 && std::abs(v2) >= 1e268) return false;
    double t = -v1 / (v2 - v1);
    out = {p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
    // 更新调用: 明确指定模板参数为 <double>
    double check_val = evaluate_rpn<double>(prog_check, out.x, out.y);
    return std::isfinite(check_val) && std::abs(check_val) < 1e200;
}

#endif //LERP_H