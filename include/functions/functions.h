#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "../../pch.h"

FORCE_INLINE double safe_exp_scalar(double x) {
    if (x >= 1) return 1e270;
    if (x <= -100) return 1e-270;
    return std::exp(x);
}

FORCE_INLINE batch_type safe_exp_batch(const batch_type& x) {
    auto is_large_mask = x >= batch_type(1);
    auto is_small_mask = x <= batch_type(-100);
    batch_type normal_result = xs::exp(x);
    batch_type intermediate_result = xs::select(is_small_mask, batch_type(1e-270), normal_result);
    return xs::select(is_large_mask, batch_type(1e270), intermediate_result);
}

FORCE_INLINE double check_ln_scalar(double x) {
    if (x <= 0.0) return std::numeric_limits<double>::quiet_NaN();
    return std::log(x);
}

FORCE_INLINE batch_type check_ln_batch(const batch_type& x) {
    auto is_positive_mask = x > batch_type(0.0);
    batch_type log_result = xs::log(x);
    return xs::select(is_positive_mask, log_result, batch_type(std::numeric_limits<double>::quiet_NaN()));
}

FORCE_INLINE double safe_ln_scalar(double x) {
    if (x > 0.0) return std::log(x);
    return -1e270;
}

FORCE_INLINE batch_type safe_ln_batch(const batch_type& x) {
    auto is_positive_mask = x > batch_type(0.0);
    batch_type log_result = xs::log(x);
    return xs::select(is_positive_mask, log_result, batch_type(-1e270));
}

FORCE_INLINE const xs::batch<double>& get_index_vec() {
    using batch_type = xs::batch<double>;
    static const auto index_vec = [] {
        constexpr std::size_t batch_size = batch_type::size;
        alignas(batch_type::arch_type::alignment()) std::array<double, batch_size> indices{};
        for (std::size_t i = 0; i < batch_size; ++i) {
            indices[i] = static_cast<double>(i);
        }
        return xs::load_aligned(indices.data());
    }();
    return index_vec;
}

#endif //FUNCTIONS_H