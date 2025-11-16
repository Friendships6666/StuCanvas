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



Interval_Batch interval_sqrt_batch(const Interval_Batch& i) {
    // 创建一个掩码，检查区间的上限是否小于0
    auto max_is_negative_mask = (i.max < xs::batch<double>(0.0));

    // 如果区间的下限小于0，则将其裁剪到0
    batch_type new_min = xs::max(xs::batch<double>(0.0), i.min);

    // 计算正常情况下的结果
    Interval_Batch normal_result = {xs::sqrt(new_min), xs::sqrt(i.max)};

    // 如果定义域无效 (max < 0)，则返回一个无效区间 (例如 [NaN, NaN])
    batch_type nan_val = xs::batch<double>(std::numeric_limits<double>::quiet_NaN());
    Interval_Batch error_result = {nan_val, nan_val};

    // 根据掩码选择最终结果
    return {
        xs::select(max_is_negative_mask, error_result.min, normal_result.min),
        xs::select(max_is_negative_mask, error_result.max, normal_result.max)
    };
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