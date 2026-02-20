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