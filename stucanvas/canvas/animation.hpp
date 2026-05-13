// stucanvas/canvas/animation.hpp
#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace StuCanvas {

/**
 * @brief 数值钳制
 */
template <typename T>
T clamp(T value, T min, T max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief 三次贝塞尔缓动函数（使用浮点类型）
 * @param progress 线性进度 [0, 1]
 * @param x1, y1, x2, y2 贝塞尔控制点
 * @return 缓动后的进度 [0, 1]
 */
template <typename T>
T cubic_bezier_easing(T progress, T x1, T y1, T x2, T y2) {
    if (progress <= T(0)) return T(0);
    if (progress >= T(1)) return T(1);

    T t = progress;
    const T eps = static_cast<T>(1e-5);
    for (int i = 0; i < 10; ++i) {
        T omt  = 1 - t;
        T omt2 = omt * omt;
        T t2   = t * t;
        T t3   = t2 * t;

        // Bx(t)
        T bx = 3 * omt2 * t * x1 + 3 * omt * t2 * x2 + t3;
        // dBx/dt
        T dbx = 3 * (omt * (1 - 3 * t) * x1 + t * (2 - 3 * t) * x2 + t2);

        if (std::abs(dbx) < eps) break;
        t = clamp(t - (bx - progress) / dbx, T(0), T(1));
    }

    // By(t)
    T omt  = 1 - t;
    T omt2 = omt * omt;
    T t2   = t * t;
    T t3   = t2 * t;
    return 3 * omt2 * t * y1 + 3 * omt * t2 * y2 + t3;
}

/**
 * @brief 标量贝塞尔动画函数（双模板参数）
 * @tparam TimeType  时间类型（可以是整数，如 uint64_t）
 * @tparam ValueType 返回值类型（通常为 float/double）
 * @param t         当前时间
 * @param startVal  起始值
 * @param endVal    终止值
 * @param tMin      动画起始时间
 * @param tMax      动画结束时间
 * @param x1, y1, x2, y2 贝塞尔缓动控制点（使用 ValueType）
 * @return 在 t 时刻的插值结果
 */
template <typename TimeType, typename ValueType>
ValueType bezier_animation(TimeType t, ValueType startVal, ValueType endVal,
                           TimeType tMin, TimeType tMax,
                           ValueType x1, ValueType y1, ValueType x2, ValueType y2) {
    t = clamp(t, tMin, tMax);
    if (tMax == tMin) return startVal;

    // 时间差转为浮点进度
    ValueType progress = static_cast<ValueType>(t - tMin) / static_cast<ValueType>(tMax - tMin);
    ValueType eased = cubic_bezier_easing(progress, x1, y1, x2, y2);
    return startVal + (endVal - startVal) * eased;
}

// ---------- 预定义的通用缓动控制点 ----------
// 控制点数值均为 ValueType 字面量，由调用处推导

template <typename TimeType, typename ValueType>
ValueType ease_in(TimeType t, ValueType startVal, ValueType endVal,
                  TimeType tMin, TimeType tMax) {
    return bezier_animation(t, startVal, endVal, tMin, tMax,
                            ValueType(0.42), ValueType(0.0),
                            ValueType(1.0),  ValueType(1.0));
}

template <typename TimeType, typename ValueType>
ValueType ease_out(TimeType t, ValueType startVal, ValueType endVal,
                   TimeType tMin, TimeType tMax) {
    return bezier_animation(t, startVal, endVal, tMin, tMax,
                            ValueType(0.0),  ValueType(0.0),
                            ValueType(0.58), ValueType(1.0));
}

template <typename TimeType, typename ValueType>
ValueType ease_in_out(TimeType t, ValueType startVal, ValueType endVal,
                      TimeType tMin, TimeType tMax) {
    return bezier_animation(t, startVal, endVal, tMin, tMax,
                            ValueType(0.42), ValueType(0.0),
                            ValueType(0.58), ValueType(1.0));
}

} // namespace StuCanvas