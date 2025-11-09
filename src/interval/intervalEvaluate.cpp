// --- 文件路径: src/interval/IntervalEvaluate.cpp ---

#include "../../pch.h"
#include "../../include/interval/interval.h"
#include <algorithm> // For std::min/max

// ====================================================================
//  使用不会与系统宏冲突的自定义常量名
// ====================================================================
namespace { // 使用匿名命名空间，使常量仅在此文件内可见
    constexpr double PI = 3.14159265358979323846;
    constexpr double PI_2 = 1.57079632679489661923;     // PI / 2
    constexpr double PI_3_2 = 4.71238898038468985769;   // 3 * PI / 2
}
// ====================================================================


// --- 二元运算符实现 ---

Interval interval_add(const Interval& a, const Interval& b) {
    return {a.min + b.min, a.max + b.max};
}

Interval interval_sub(const Interval& a, const Interval& b) {
    return {a.min - b.max, a.max - b.min};
}

Interval interval_mul(const Interval& a, const Interval& b) {
    double p1 = a.min * b.min;
    double p2 = a.min * b.max;
    double p3 = a.max * b.min;
    double p4 = a.max * b.max;
    return {std::min({p1, p2, p3, p4}), std::max({p1, p2, p3, p4})};
}

Interval interval_div(const Interval& a, const Interval& b) {
    if (b.min <= 0.0 && b.max >= 0.0) {
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
    Interval b_inv = {1.0 / b.max, 1.0 / b.min};
    return interval_mul(a, b_inv);
}

Interval interval_pow(const Interval& base, const Interval& exp) {
    // 仅处理指数是常数且为正整数的情况
    if (exp.min == exp.max && exp.min > 0 && exp.min == std::round(exp.min)) {
        int n = static_cast<int>(exp.min);

        // 如果 n 是奇数 (e.g., 1, 3, 5...)
        if (n % 2 == 1) {
            return {std::pow(base.min, n), std::pow(base.max, n)};
        }
        // 如果 n 是偶数 (e.g., 2, 4, 6...)
        else {
            if (base.min >= 0) {
                return {std::pow(base.min, n), std::pow(base.max, n)};
            }
            if (base.max < 0) {
                return {std::pow(base.max, n), std::pow(base.min, n)};
            }
            // 区间跨越 0
            return {0.0, std::max(std::pow(base.min, n), std::pow(base.max, n))};
        }
    }

    // 对于非整数、负数或区间指数等复杂情况，返回最保守的结果
    return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
}


// --- 一元运算符实现 ---

Interval interval_exp(const Interval& i) {
    return {std::exp(i.min), std::exp(i.max)};
}

// ====================================================================
//  MODIFIED: interval_ln
//  - 行为已更新。
//  - 如果整个输入区间 i.max <= 0，则返回 [-inf, -inf] 而不是 NaN。
//  - 如果区间包含 0 (i.min <= 0)，则下界为 -inf。
//  - 这提供了更健壮的行为，避免了 NaN 污染。
// ====================================================================
Interval interval_ln(const Interval& i) {
    if (i.max <= 0.0) {
        return {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    }
    double min_val = (i.min <= 0.0) ? -std::numeric_limits<double>::infinity() : std::log(i.min);
    return {min_val, std::log(i.max)};
}

Interval interval_sin(const Interval& i) {
    if (i.max - i.min >= 2.0 * PI) {
        return {-1.0, 1.0};
    }
    double sin_min = std::sin(i.min);
    double sin_max = std::sin(i.max);
    if (sin_min > sin_max) std::swap(sin_min, sin_max);

    double k1 = std::ceil((i.min - PI_2) / (2.0 * PI));
    double peak = PI_2 + k1 * 2.0 * PI;
    if (peak >= i.min && peak <= i.max) {
        sin_max = 1.0;
    }

    double k2 = std::ceil((i.min - PI_3_2) / (2.0 * PI));
    double trough = PI_3_2 + k2 * 2.0 * PI;
    if (trough >= i.min && trough <= i.max) {
        sin_min = -1.0;
    }
    return {sin_min, sin_max};
}

Interval interval_cos(const Interval& i) {
    if (i.max - i.min >= 2.0 * PI) {
        return {-1.0, 1.0};
    }
    double cos_min = std::cos(i.min);
    double cos_max = std::cos(i.max);
    if (cos_min > cos_max) std::swap(cos_min, cos_max);

    double k1 = std::ceil(i.min / (2.0 * PI));
    double peak = k1 * 2.0 * PI;
    if (peak >= i.min && peak <= i.max) {
        cos_max = 1.0;
    }

    double k2 = std::ceil((i.min - PI) / (2.0 * PI));
    double trough = PI + k2 * 2.0 * PI;
    if (trough >= i.min && trough <= i.max) {
        cos_min = -1.0;
    }
    return {cos_min, cos_max};
}

Interval interval_tan(const Interval& i) {
    double k = std::floor(i.min / PI - 0.5);
    double asymptote = (k + 0.5) * PI;
    if (asymptote >= i.min && asymptote <= i.max) {
         return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
     asymptote += PI;
    if (asymptote >= i.min && asymptote <= i.max) {
         return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
    return {std::tan(i.min), std::tan(i.max)};
}

Interval interval_abs(const Interval& i) {
    if (i.min >= 0) return i;
    if (i.max < 0) return {-i.max, -i.min};
    return {0.0, std::max(-i.min, i.max)};
}

Interval interval_sign(const Interval& i) {
    if (i.min > 0) return {1.0, 1.0};
    if (i.max < 0) return {-1.0, -1.0};
    return {-1.0, 1.0};
}

// --- SIMD 批处理版本实现 ---

Interval_Batch interval_add_batch(const Interval_Batch& a, const Interval_Batch& b) {
    return {a.min + b.min, a.max + b.max};
}

Interval_Batch interval_sub_batch(const Interval_Batch& a, const Interval_Batch& b) {
    return {a.min - b.max, a.max - b.min};
}

Interval_Batch interval_mul_batch(const Interval_Batch& a, const Interval_Batch& b) {
    batch_type p1 = a.min * b.min;
    batch_type p2 = a.min * b.max;
    batch_type p3 = a.max * b.min;
    batch_type p4 = a.max * b.max;
    batch_type final_min = xs::min(p1, xs::min(p2, xs::min(p3, p4)));
    batch_type final_max = xs::max(p1, xs::max(p2, xs::max(p3, p4)));
    return {final_min, final_max};
}