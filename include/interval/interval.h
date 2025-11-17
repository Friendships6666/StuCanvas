// --- 文件路径: include/interval/interval.h ---

#ifndef WASMTBBTEST_INTERVAL_H
#define WASMTBBTEST_INTERVAL_H

#include "../../pch.h"
#include <boost/multiprecision/mpfr.hpp>
#include <boost/math/constants/constants.hpp>
#include <limits> // 用于 std::numeric_limits
#include <stdexcept> // 用于 std::domain_error
#include <algorithm> // 包含 std::swap 等

// --- 关键修正: 避免与Windows.h中的min/max宏冲突 ---
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
// ====================================================================
//          ↓↓↓ 类型别名和核心辅助函数 ↓↓↓
// ====================================================================

// 使用基础的 mpfr_float 类型别名。精度将在运行时于每个变量上设置。
using hp_float = boost::multiprecision::mpfr_float;

// 前向声明 Interval 结构体
template<typename T>
struct Interval;

/**
 * @brief 根据给定的精度，获取一个表示“整个数轴”的区间 [-MAX, +MAX]。
 * @tparam T 数值类型 (double, hp_float等)。
 * @param precision_bits 对于高精度类型，需要指定的二进制位数。
 * @return 一个覆盖最大范围的 Interval。
 */
template<typename T>
Interval<T> get_infinity_interval(unsigned int precision_bits = 53) {
    T max_val;
    if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
        max_val = std::numeric_limits<T>::max();
    } else if constexpr (std::is_same_v<T, hp_float>) {
        hp_float calculated_max;
        calculated_max.precision(precision_bits);
        calculated_max = std::numeric_limits<hp_float>::max();
        max_val = calculated_max;
    }
    return Interval<T>{-max_val, max_val};
}


// ====================================================================
//      ↓↓↓ 1. 模板化的 Interval 结构体 ↓↓↓
// ====================================================================
template<typename T>
struct Interval {
    T min;
    T max;

    Interval() : min(T(0.0)), max(T(0.0)) {}

    explicit Interval(const T& val) : min(val), max(val) {}

    Interval(const T& v_min, const T& v_max) : min(v_min), max(v_max) {}
};

// ====================================================================
//      ↓↓↓ 2. 模板化的标量区间算术函数 (已应用最终修复) ↓↓↓
// ====================================================================

// --- 基本运算 ---
template<typename T>
Interval<T> interval_add(const Interval<T>& a, const Interval<T>& b) {
    return Interval<T>{a.min + b.min, a.max + b.max};
}

template<typename T>
Interval<T> interval_sub(const Interval<T>& a, const Interval<T>& b) {
    return Interval<T>{a.min - b.max, a.max - b.min};
}

template<typename T>
Interval<T> interval_mul(const Interval<T>& a, const Interval<T>& b) {
    T p1 = a.min * b.min;
    T p2 = a.min * b.max;
    T p3 = a.max * b.min;
    T p4 = a.max * b.max;
    // --- 最终修复: 使用ADL让编译器选择正确的min/max ---
    using std::min;
    using std::max;
    using boost::multiprecision::min;
    using boost::multiprecision::max;
    return Interval<T>{min(min(p1, p2), min(p3, p4)), max(max(p1, p2), max(p3, p4))};
}

template<typename T>
Interval<T> interval_div(const Interval<T>& a, const Interval<T>& b, unsigned int precision_bits = 53) {
    if (b.min <= T(0.0) && b.max >= T(0.0)) {
        return get_infinity_interval<T>(precision_bits);
    }
    Interval<T> b_inv = {T(1.0) / b.max, T(1.0) / b.min};
    return interval_mul(a, b_inv);
}

// --- 幂、根、指数、对数 ---
template<typename T>
Interval<T> interval_pow(const Interval<T>& base, const Interval<T>& exp, unsigned int precision_bits = 53) {
    if (exp.min != exp.max) {
       return get_infinity_interval<T>(precision_bits);
    }
    const T e = exp.min;

    if (floor(e) == e && e > 0) {
        long long n;
        if constexpr (std::is_same_v<T, hp_float>) {
            n = e.template convert_to<long long>();
        } else {
            n = static_cast<long long>(e);
        }

        if (n % 2 == 0) {
            if (base.min >= T(0.0)) return Interval<T>{pow(base.min, e), pow(base.max, e)};
            if (base.max < T(0.0)) return Interval<T>{pow(base.max, e), pow(base.min, e)};
            // --- 最终修复: 使用ADL ---
            using std::max;
            using boost::multiprecision::max;
            return Interval<T>{T(0.0), max(pow(base.min, e), pow(base.max, e))};
        }
    }

    T p1 = pow(base.min, e);
    T p2 = pow(base.max, e);

    if (isnan(p1) || isnan(p2)) {
        return get_infinity_interval<T>(precision_bits);
    }
    // --- 最终修复: 使用ADL ---
    using std::min;
    using std::max;
    using boost::multiprecision::min;
    using boost::multiprecision::max;
    return Interval<T>{min(p1, p2), max(p1, p2)};
}

template<typename T>
Interval<T> interval_sqrt(const Interval<T>& i, unsigned int precision_bits = 53) {
    if (i.max < T(0.0)) {
        throw std::domain_error("Square root of a negative interval.");
    }
    // --- 最终修复: 使用ADL ---
    using std::max;
    using boost::multiprecision::max;
    T new_min = max(T(0.0), i.min);
    return Interval<T>{sqrt(new_min), sqrt(i.max)};
}

template<typename T>
Interval<T> interval_exp(const Interval<T>& i) {
    return Interval<T>{exp(i.min), exp(i.max)};
}
// ====================================================================
//          ↓↓↓ 核心修改区域 ↓↓↓
// ====================================================================
template<typename T>
Interval<T> interval_ln(const Interval<T>& i, unsigned int precision_bits = 53) {
    // 如果整个区间都小于等于0，则函数无实数解。
    // 不再抛出异常，而是返回一个代表极大负数的区间。
    if (i.max <= T(0.0)) {
        T neg_inf = -get_infinity_interval<T>(precision_bits).max;
        return Interval<T>{neg_inf, neg_inf};
    }

    T min_val;
    // 如果区间下限小于等于0（但上限大于0），则对数可以趋近于负无穷。
    if (i.min <= T(0.0)) {
        min_val = -get_infinity_interval<T>(precision_bits).max;
    } else {
        // 如果整个区间都大于0，正常计算对数。
        min_val = log(i.min);
    }

    return Interval<T>{min_val, log(i.max)};
}

// --- 三角函数 ---
template<typename T>
Interval<T> interval_sin(const Interval<T>& i) {
    const T pi = boost::math::constants::pi<T>();
    const T two_pi = T(2.0) * pi;
    if (i.max - i.min >= two_pi) {
        return Interval<T>{T(-1.0), T(1.0)};
    }
    T sin_min_val = sin(i.min);
    T sin_max_val = sin(i.max);
    if (sin_min_val > sin_max_val) std::swap(sin_min_val, sin_max_val);

    const T pi_2 = pi / T(2.0);
    T k1 = ceil((i.min - pi_2) / two_pi);
    T peak = pi_2 + k1 * two_pi;
    if (peak >= i.min && peak <= i.max) {
        sin_max_val = T(1.0);
    }

    const T pi_3_2 = T(3.0) * pi / T(2.0);
    T k2 = ceil((i.min - pi_3_2) / two_pi);
    T trough = pi_3_2 + k2 * two_pi;
    if (trough >= i.min && trough <= i.max) {
        sin_min_val = T(-1.0);
    }
    return Interval<T>{sin_min_val, sin_max_val};
}

template<typename T>
Interval<T> interval_cos(const Interval<T>& i) {
    const T pi = boost::math::constants::pi<T>();
    const T two_pi = T(2.0) * pi;
    if (i.max - i.min >= two_pi) {
        return Interval<T>{T(-1.0), T(1.0)};
    }
    T cos_min_val = cos(i.min);
    T cos_max_val = cos(i.max);
    if (cos_min_val > cos_max_val) std::swap(cos_min_val, cos_max_val);

    T k1 = ceil(i.min / two_pi);
    T peak = k1 * two_pi;
    if (peak >= i.min && peak <= i.max) {
        cos_max_val = T(1.0);
    }

    T k2 = ceil((i.min - pi) / two_pi);
    T trough = pi + k2 * two_pi;
    if (trough >= i.min && trough <= i.max) {
        cos_min_val = T(-1.0);
    }
    return Interval<T>{cos_min_val, cos_max_val};
}

template<typename T>
Interval<T> interval_tan(const Interval<T>& i, unsigned int precision_bits = 53) {
    const T pi = boost::math::constants::pi<T>();
    T k = floor(i.min / pi - T(0.5));
    T asymptote = (k + T(0.5)) * pi;
    if (asymptote >= i.min && asymptote <= i.max) {
        return get_infinity_interval<T>(precision_bits);
    }
     asymptote += pi;
    if (asymptote >= i.min && asymptote <= i.max) {
        return get_infinity_interval<T>(precision_bits);
    }
    return Interval<T>{tan(i.min), tan(i.max)};
}

// --- 其他函数 ---
template<typename T>
Interval<T> interval_abs(const Interval<T>& i) {
    if (i.min >= T(0.0)) return i;
    if (i.max < T(0.0)) return Interval<T>{-i.max, -i.min};
    // --- 最终修复: 使用ADL ---
    using std::max;
    using boost::multiprecision::max;
    return Interval<T>{T(0.0), max(-i.min, i.max)};
}

template<typename T>
Interval<T> interval_sign(const Interval<T>& i) {
    if (i.min > T(0.0)) return Interval<T>{T(1.0), T(1.0)};
    if (i.max < T(0.0)) return Interval<T>{T(-1.0), T(-1.0)};
    if (i.min == T(0.0) && i.max == T(0.0)) return Interval<T>{T(0.0), T(0.0)};

    T min_val = i.min < T(0.0) ? T(-1.0) : T(0.0);
    T max_val = i.max > T(0.0) ? T(1.0) : T(0.0);
    return Interval<T>{min_val, max_val};
}


// ====================================================================
//      ↓↓↓ 3. 模板化的标量运算符重载 ↓↓↓
// ====================================================================
template<typename T> inline FORCE_INLINE Interval<T> operator+(const Interval<T>& a, const Interval<T>& b) { return interval_add(a, b); }
template<typename T> inline FORCE_INLINE Interval<T> operator-(const Interval<T>& a, const Interval<T>& b) { return interval_sub(a, b); }
template<typename T> inline FORCE_INLINE Interval<T> operator*(const Interval<T>& a, const Interval<T>& b) { return interval_mul(a, b); }
template<typename T> inline FORCE_INLINE Interval<T> operator/(const Interval<T>& a, const Interval<T>& b) { return interval_div(a, b); }

template<typename T> inline FORCE_INLINE Interval<T>& operator+=(Interval<T>& a, const Interval<T>& b) { a = a + b; return a; }
template<typename T> inline FORCE_INLINE Interval<T>& operator-=(Interval<T>& a, const Interval<T>& b) { a = a - b; return a; }
template<typename T> inline FORCE_INLINE Interval<T>& operator*=(Interval<T>& a, const Interval<T>& b) { a = a * b; return a; }
template<typename T> inline FORCE_INLINE Interval<T>& operator/=(Interval<T>& a, const Interval<T>& b) { a = a / b; return a; }


// ====================================================================
//      ↓↓↓ 4. SIMD 批处理版本 (纯声明) ↓↓↓
// ====================================================================

// SIMD 区间批处理结构体
struct Interval_Batch {
    batch_type min;
    batch_type max;
};

// SIMD 函数声明 (实现位于 intervalEvaluate.cpp)
Interval_Batch interval_add_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_sub_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_mul_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_div_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_pow_batch(const Interval_Batch& base, const Interval_Batch& exp);
Interval_Batch interval_sin_batch(const Interval_Batch& i);
Interval_Batch interval_cos_batch(const Interval_Batch& i);
Interval_Batch interval_tan_batch(const Interval_Batch& i);
Interval_Batch interval_ln_batch(const Interval_Batch& i);
Interval_Batch interval_exp_batch(const Interval_Batch& i);
Interval_Batch interval_abs_batch(const Interval_Batch& i);
Interval_Batch interval_sign_batch(const Interval_Batch& i);
Interval_Batch interval_sqrt_batch(const Interval_Batch& i);

// SIMD 运算符重载
inline FORCE_INLINE Interval_Batch operator+(const Interval_Batch& a, const Interval_Batch& b) { return interval_add_batch(a, b); }
inline FORCE_INLINE Interval_Batch operator-(const Interval_Batch& a, const Interval_Batch& b) { return interval_sub_batch(a, b); }
inline FORCE_INLINE Interval_Batch operator*(const Interval_Batch& a, const Interval_Batch& b) { return interval_mul_batch(a, b); }
inline FORCE_INLINE Interval_Batch operator/(const Interval_Batch& a, const Interval_Batch& b) { return interval_div_batch(a, b); }
inline FORCE_INLINE Interval_Batch& operator+=(Interval_Batch& a, const Interval_Batch& b) { a = a + b; return a; }
inline FORCE_INLINE Interval_Batch& operator-=(Interval_Batch& a, const Interval_Batch& b) { a = a - b; return a; }
inline FORCE_INLINE Interval_Batch& operator*=(Interval_Batch& a, const Interval_Batch& b) { a = a * b; return a; }
inline FORCE_INLINE Interval_Batch& operator/=(Interval_Batch& a, const Interval_Batch& b) { a = a / b; return a; }


#endif //WASMTBBTEST_INTERVAL_H