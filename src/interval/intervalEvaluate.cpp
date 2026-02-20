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

// --- 文件路径: src/interval/intervalEvaluate.cpp ---

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

    // --- 路径 A (偶数版本: x^2, x^4) ---
    // 处理底数跨越0的情况，结果必定非负
    batch_type even_case_min, even_case_max;
    {
        auto min_is_pos = (base.min >= B_ZERO);
        auto max_is_neg = (base.max < B_ZERO);
        auto pow_min = xs::pow(base.min, exp.min);
        auto pow_max = xs::pow(base.max, exp.min);

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

    // --- 路径 B (常规单调版本: n^x, x^3, x^2.5 等) ---
    // 直接计算端点。注意：如果 base < 0 且 exp 不是整数，xs::pow 会产生 NaN
    batch_type monotonic_case_min, monotonic_case_max;
    {
        auto p1 = xs::pow(base.min, exp.min);
        auto p2 = xs::pow(base.max, exp.max); // 注意这里用 max 对 max，交叉组合在 swap 处理

        // 修正：对于 n^x (n>1)，min^min 是下界。对于 n^x (0<n<1)，min^max 是下界。
        // 为了通用，我们计算全部四个组合看似太慢。
        // 实际上 xs::pow 对于 n^x 是单调的。
        // 但为了安全处理 exp 是区间的情况 (3^[-1, 1]) -> [0.33, 3]
        // 我们需要计算交叉项

        auto p1_min = xs::pow(base.min, exp.min);
        auto p2_min = xs::pow(base.max, exp.min);
        auto p1_max = xs::pow(base.min, exp.max);
        auto p2_max = xs::pow(base.max, exp.max);

        // 如果底数 > 0，则结果有效。我们取所有组合的极值。
        // 这里的开销比标量小，因为是 SIMD 并行
        monotonic_case_min = xs::min(xs::min(p1_min, p2_min), xs::min(p1_max, p2_max));
        monotonic_case_max = xs::max(xs::max(p1_min, p2_min), xs::max(p1_max, p2_max));
    }

    // ====================================================================
    // 步骤 3: 选择结果
    // ====================================================================

    // 如果是偶数整数次幂，使用路径 A，否则使用路径 B
    batch_type final_min = xs::select(is_positive_even_integer_mask, even_case_min, monotonic_case_min);
    batch_type final_max = xs::select(is_positive_even_integer_mask, even_case_max, monotonic_case_max);

    // ====================================================================
    // 步骤 4: 最终安全性检查 (关键修复点)
    // ====================================================================

    // 我们不再简单地检查 is_constant_mask。
    // 只要满足以下任一条件，结果就是有效的：
    // 1. 底数全 > 0 (base.min > 0) -> 这涵盖了 3^x
    // 2. 指数是整数 (is_integer_mask) -> 这涵盖了 (-3)^2

    auto base_is_positive = (base.min > B_ZERO);
    // 只要底数大于0，或者指数是整数，就是合法的
    auto is_valid_domain = base_is_positive | is_integer_mask;

    // 如果定义域无效 (例如底数含负数 且 指数非整数)，返回无穷大区间
    final_min = xs::select(is_valid_domain, final_min, B_NEG_INF);
    final_max = xs::select(is_valid_domain, final_max, B_INF);

    return {final_min, final_max};
}