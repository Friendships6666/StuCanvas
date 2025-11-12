// --- 文件路径: src/interval/IntervalEvaluate.cpp ---

#include "../../pch.h"
#include "../../include/interval/interval.h"


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
    // 假设 1: 幂指数是常量，这与您之前的逻辑一致，也是常见的优化。
    if (exp.min != exp.max) {
        // 如果指数本身是一个区间，情况会变得极其复杂，返回最保守的结果。
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
    const double e = exp.min;

    // 假设 2: CAS 已经重写了表达式，确保 `base` 区间对于 `std::pow` 是有效的。
    // 例如：如果 e 不是整数，CAS 保证 base.min >= 0。

    // 唯一需要特殊处理的 intrinsic 情况：偶数次正整数幂。
    // 这个函数的形状 (类似抛物线) 有一个在 base = 0 处的极小值，
    // 这是 `pow` 函数自身的性质，CAS 无法改变。
    if (e > 0 && e == std::round(e) && static_cast<long long>(e) % 2 == 0) {
        const auto n = static_cast<long long>(e);

        if (base.min >= 0.0) {
            // 区间完全在 y 轴右侧，函数单调递增。
            return {std::pow(base.min, n), std::pow(base.max, n)};
        }
        if (base.max < 0.0) {
            // 区间完全在 y 轴左侧，函数单调递减。
            return {std::pow(base.max, n), std::pow(base.min, n)};
        }
        // 区间跨越了 y 轴，最小值必然是 0。
        return {0.0, std::max(std::pow(base.min, n), std::pow(base.max, n))};
    }

    // 对于所有其他情况 (奇数次幂、负数次幂、分数次幂)，
    // 在 CAS 处理过的 "良性" base 区间上，函数是单调的。
    // 因此，我们只需要计算两个端点的值，然后取其最小/最大值即可。
    double p1 = std::pow(base.min, e);
    double p2 = std::pow(base.max, e);

    // 添加一个安全检查，以防万一 CAS 仍传入无效域 (例如 pow(-1, 0.5))。
    if (std::isnan(p1) || std::isnan(p2)) {
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }

    // std::minmax 可以在某些架构上被优化
    const auto [min_val, max_val] = std::minmax(p1, p2);
    return {min_val, max_val};
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

// --- 辅助常量 (放在文件顶部或匿名命名空间内) ---
namespace {
    // 使用 xs::batch<double> 来定义 SIMD 常量
    const auto B_ZERO    = xs::batch<double>(0.0);
    const auto B_ONE     = xs::batch<double>(1.0);
    const auto B_NEG_ONE = xs::batch<double>(-1.0);
    const auto B_INF     = xs::batch<double>(std::numeric_limits<double>::infinity());
    const auto B_NEG_INF = xs::batch<double>(-std::numeric_limits<double>::infinity());
    const auto B_PI      = xs::batch<double>(3.14159265358979323846);
    const auto B_TWO_PI  = B_PI * 2.0;
    const auto B_PI_2    = B_PI / 2.0;
    const auto B_PI_3_2  = B_PI * 1.5;
}





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

Interval_Batch interval_div_batch(const Interval_Batch& a, const Interval_Batch& b) {
    auto crosses_zero_mask = (b.min <= B_ZERO) & (b.max >= B_ZERO);
    Interval_Batch b_inv = {B_ONE / b.max, B_ONE / b.min};
    Interval_Batch normal_result = interval_mul_batch(a, b_inv);
    batch_type final_min = xs::select(crosses_zero_mask, B_NEG_INF, normal_result.min);
    batch_type final_max = xs::select(crosses_zero_mask, B_INF, normal_result.max);
    return {final_min, final_max};
}

Interval_Batch interval_exp_batch(const Interval_Batch& i) {
    return {xs::exp(i.min), xs::exp(i.max)};
}

Interval_Batch interval_ln_batch(const Interval_Batch& i) {
    auto max_is_non_positive_mask = (i.max <= B_ZERO);
    auto min_is_non_positive_mask = (i.min <= B_ZERO);
    auto log_of_min = xs::log(i.min);
    auto log_of_max = xs::log(i.max);
    batch_type final_max = xs::select(max_is_non_positive_mask, B_NEG_INF, log_of_max);
    batch_type min_result_if_max_is_positive = xs::select(min_is_non_positive_mask, B_NEG_INF, log_of_min);
    batch_type final_min = xs::select(max_is_non_positive_mask, B_NEG_INF, min_result_if_max_is_positive);
    return {final_min, final_max};
}

Interval_Batch interval_abs_batch(const Interval_Batch& i) {
    auto min_is_positive_mask = (i.min >= B_ZERO);
    auto max_is_negative_mask = (i.max < B_ZERO);

    // 各种情况下的结果
    Interval_Batch positive_case_result = i;
    Interval_Batch negative_case_result = {-i.max, -i.min};
    Interval_Batch crosses_zero_result = {B_ZERO, xs::max(-i.min, i.max)};

    // --- 修正点 ---
    // 先处理内层 select
    batch_type inner_selected_min = xs::select(max_is_negative_mask, negative_case_result.min, crosses_zero_result.min);
    batch_type inner_selected_max = xs::select(max_is_negative_mask, negative_case_result.max, crosses_zero_result.max);

    // 再处理外层 select
    batch_type final_min = xs::select(min_is_positive_mask, positive_case_result.min, inner_selected_min);
    batch_type final_max = xs::select(min_is_positive_mask, positive_case_result.max, inner_selected_max);

    return {final_min, final_max};
}

Interval_Batch interval_sign_batch(const Interval_Batch& i) {
    auto min_is_positive_mask = (i.min > B_ZERO);
    auto max_is_negative_mask = (i.max < B_ZERO);
    batch_type final_min = xs::select(min_is_positive_mask, B_ONE, B_NEG_ONE);
    batch_type final_max = xs::select(max_is_negative_mask, B_NEG_ONE, B_ONE);
    return {final_min, final_max};
}

Interval_Batch interval_sin_batch(const Interval_Batch& i) {
    auto width_ge_2pi_mask = (i.max - i.min) >= B_TWO_PI;
    auto sin_min = xs::sin(i.min);
    auto sin_max = xs::sin(i.max);
    auto initial_lower_bound = xs::min(sin_min, sin_max);
    auto initial_upper_bound = xs::max(sin_min, sin_max);
    auto k1 = xs::ceil((i.min - B_PI_2) / B_TWO_PI);
    auto peak = B_PI_2 + k1 * B_TWO_PI;
    auto crosses_peak_mask = (peak >= i.min) & (peak <= i.max);
    auto k2 = xs::ceil((i.min - B_PI_3_2) / B_TWO_PI);
    auto trough = B_PI_3_2 + k2 * B_TWO_PI;
    auto crosses_trough_mask = (trough >= i.min) & (trough <= i.max);
    auto bound_after_peak_check = xs::select(crosses_peak_mask, B_ONE, initial_upper_bound);
    auto bound_after_trough_check = xs::select(crosses_trough_mask, B_NEG_ONE, initial_lower_bound);
    batch_type final_min = xs::select(width_ge_2pi_mask, B_NEG_ONE, bound_after_trough_check);
    batch_type final_max = xs::select(width_ge_2pi_mask, B_ONE, bound_after_peak_check);
    return {final_min, final_max};
}

Interval_Batch interval_cos_batch(const Interval_Batch& i) {
    auto width_ge_2pi_mask = (i.max - i.min) >= B_TWO_PI;
    auto cos_min = xs::cos(i.min);
    auto cos_max = xs::cos(i.max);
    auto initial_lower_bound = xs::min(cos_min, cos_max);
    auto initial_upper_bound = xs::max(cos_min, cos_max);
    auto k1 = xs::ceil(i.min / B_TWO_PI);
    auto peak = k1 * B_TWO_PI;
    auto crosses_peak_mask = (peak >= i.min) & (peak <= i.max);
    auto k2 = xs::ceil((i.min - B_PI) / B_TWO_PI);
    auto trough = B_PI + k2 * B_TWO_PI;
    auto crosses_trough_mask = (trough >= i.min) & (trough <= i.max);
    auto bound_after_peak_check = xs::select(crosses_peak_mask, B_ONE, initial_upper_bound);
    auto bound_after_trough_check = xs::select(crosses_trough_mask, B_NEG_ONE, initial_lower_bound);
    batch_type final_min = xs::select(width_ge_2pi_mask, B_NEG_ONE, bound_after_trough_check);
    batch_type final_max = xs::select(width_ge_2pi_mask, B_ONE, bound_after_peak_check);
    return {final_min, final_max};
}

Interval_Batch interval_tan_batch(const Interval_Batch& i) {
    auto k = xs::floor(i.min / B_PI - 0.5);
    auto asymptote1 = (k + 0.5) * B_PI;
    auto has_asymptote1_mask = (asymptote1 >= i.min) & (asymptote1 <= i.max);
    auto asymptote2 = asymptote1 + B_PI;
    auto has_asymptote2_mask = (asymptote2 >= i.min) & (asymptote2 <= i.max);
    auto has_asymptote_mask = has_asymptote1_mask | has_asymptote2_mask;

    // 各种情况下的结果
    Interval_Batch normal_result = {xs::tan(i.min), xs::tan(i.max)};
    Interval_Batch asymptote_result = {B_NEG_INF, B_INF};

    // --- 修正点 ---
    // 分别选择 min 和 max 成员
    batch_type final_min = xs::select(has_asymptote_mask, asymptote_result.min, normal_result.min);
    batch_type final_max = xs::select(has_asymptote_mask, asymptote_result.max, normal_result.max);

    return {final_min, final_max};
}

/**
 * @brief 统一的、智能的区间幂函数 SIMD 版本 (已修正)。
 * 该函数内部会判断指数的性质，并自动应用正确的区间计算逻辑。
 * 它假设 CAS 已经处理了定义域问题（例如，为非整数次幂确保底数为正）。
 */
Interval_Batch interval_pow_batch(const Interval_Batch& base, const Interval_Batch& exp) {
    // ====================================================================
    // 步骤 1: 创建掩码，判断指数性质
    // ====================================================================
    auto is_constant_mask = (exp.min == exp.max);
    auto is_positive_mask = (exp.min > B_ZERO);
    auto is_integer_mask = (xs::floor(exp.min) == exp.min);
    auto is_even_mask = (xs::floor(exp.min * 0.5) * 2.0 == exp.min);
    auto is_positive_even_integer_mask = is_constant_mask & is_positive_mask & is_integer_mask & is_even_mask;

    // ====================================================================
    // 步骤 2: 计算两种可能路径的 min 和 max 分量
    // ====================================================================

    // --- 路径 A (偶数版本) 的 min/max 分量 ---
    batch_type even_case_min, even_case_max;
    {
        auto min_is_pos = (base.min >= B_ZERO);
        auto max_is_neg = (base.max < B_ZERO);
        auto pow_min = xs::pow(base.min, exp.min);
        auto pow_max = xs::pow(base.max, exp.min);

        // --- 修正点: 不再创建临时的 Interval_Batch，直接计算 min/max 分量 ---
        batch_type pos_case_min = pow_min;
        batch_type pos_case_max = pow_max;

        batch_type neg_case_min = pow_max;
        batch_type neg_case_max = pow_min;

        batch_type cross_case_min = B_ZERO;
        batch_type cross_case_max = xs::max(pow_min, pow_max);

        batch_type inner_selected_min = xs::select(max_is_neg, neg_case_min, cross_case_min);
        batch_type inner_selected_max = xs::select(max_is_neg, neg_case_max, cross_case_max);

        even_case_min = xs::select(min_is_pos, pos_case_min, inner_selected_min);
        even_case_max = xs::select(min_is_pos, pos_case_max, inner_selected_max);
    }

    // --- 路径 B (单调版本) 的 min/max 分量 ---
    batch_type monotonic_case_min, monotonic_case_max;
    {
        auto p1 = xs::pow(base.min, exp.min);
        auto p2 = xs::pow(base.max, exp.min);
        monotonic_case_min = xs::min(p1, p2);
        monotonic_case_max = xs::max(p1, p2);
    }

    // ====================================================================
    // 步骤 3: 使用掩码选择最终的 min 和 max 分量
    // ====================================================================
    batch_type final_min = xs::select(is_positive_even_integer_mask, even_case_min, monotonic_case_min);
    batch_type final_max = xs::select(is_positive_even_integer_mask, even_case_max, monotonic_case_max);

    // 最后，应用常量检查。如果指数不是一个常量，则返回最保守的结果。
    final_min = xs::select(is_constant_mask, final_min, B_NEG_INF);
    final_max = xs::select(is_constant_mask, final_max, B_INF);

    // 一次性构造并返回，解决了所有编译错误和警告。
    return {final_min, final_max};
}