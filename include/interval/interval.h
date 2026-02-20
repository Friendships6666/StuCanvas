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
 * @brief 根据给定的精度，获取一个表示“整个数轴”的区间 [-Inf, +Inf]。
 * @tparam T 数值类型 (double, hp_float等)。
 * @param precision_bits 对于高精度类型，需要指定的二进制位数。
 * @return 一个覆盖最大范围的 Interval (从负无穷到正无穷)。
 */
template<typename T>
Interval<T> get_infinity_interval(unsigned int precision_bits = 53) {
    T inf_val;
    if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
        // 获取标准浮点类型的无穷大
        inf_val = std::numeric_limits<T>::infinity();
    } else if constexpr (std::is_same_v<T, hp_float>) {
        // 获取高精度类型的无穷大
        hp_float calculated_inf;
        calculated_inf.precision(precision_bits); // 设置精度
        calculated_inf = std::numeric_limits<hp_float>::infinity();
        inf_val = calculated_inf;
    }
    // 返回 [-Inf, +Inf]
    return Interval<T>{-inf_val, inf_val};
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


template<typename T>
Interval<T> interval_pow(const Interval<T>& base, const Interval<T>& exp, unsigned int precision_bits = 53) {
    // =========================================================
    //  不使用 using 声明，完全通过 if constexpr 分流
    //  避免任何潜在的命名空间冲突
    // =========================================================

    // ----------------------------------------------------------------
    // 路径 A: 指数是确定的整数 (处理 x^2, x^3 等)
    // ----------------------------------------------------------------
    if (exp.min == exp.max) {
        const T e = exp.min;

        // 计算 floor(e)
        T floor_e;
        if constexpr (std::is_same_v<T, hp_float>) floor_e = boost::multiprecision::floor(e);
        else floor_e = std::floor(e);

        if (floor_e == e) {
            long long n;
            if constexpr (std::is_same_v<T, hp_float>) n = e.template convert_to<long long>();
            else n = static_cast<long long>(e);

            // 偶数次幂: [-2, 3]^2 -> [0, 9]
            if (n % 2 == 0) {
                if (base.min < T(0.0) && base.max > T(0.0)) {
                    T abs_min, abs_max;
                    if constexpr (std::is_same_v<T, hp_float>) {
                        abs_min = boost::multiprecision::abs(base.min);
                        abs_max = boost::multiprecision::abs(base.max);
                    } else {
                        abs_min = std::abs(base.min);
                        abs_max = std::abs(base.max);
                    }

                    T val1, val2;
                    if constexpr (std::is_same_v<T, hp_float>) {
                        val1 = boost::multiprecision::pow(abs_min, n);
                        val2 = boost::multiprecision::pow(abs_max, n);
                    } else {
                        val1 = std::pow(abs_min, n);
                        val2 = std::pow(abs_max, n);
                    }

                    // std::max 支持 boost 类型（只要定义了 < 运算符）
                    return Interval<T>{T(0.0), std::max(val1, val2)};
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // 路径 B: 底数严格大于 0 (处理 3^x, x^x, x^y 等)
    // 利用公式: base^exp = e^(exp * ln(base))
    // ----------------------------------------------------------------
    if (base.min > T(0.0)) {
        Interval<T> ln_base = interval_ln(base, precision_bits);
        Interval<T> product = interval_mul(exp, ln_base);
        return interval_exp(product);
    }

    // 边界修正：底数是 [0, y] 且指数 > 0
    if (base.min == T(0.0) && base.max >= T(0.0) && exp.min > T(0.0)) {
         Interval<T> safe_base = base;
         safe_base.min = std::numeric_limits<T>::epsilon();
         Interval<T> ln_res = interval_ln(safe_base, precision_bits);
         Interval<T> res = interval_exp(interval_mul(exp, ln_res));
         res.min = T(0.0);
         return res;
    }

    // ----------------------------------------------------------------
    // 路径 C: 传统标量计算 (兜底)
    // ----------------------------------------------------------------
    if (exp.min == exp.max) {
        const T e = exp.min;
        T p1, p2;

        if constexpr (std::is_same_v<T, hp_float>) {
            p1 = boost::multiprecision::pow(base.min, e);
            p2 = boost::multiprecision::pow(base.max, e);
        } else {
            p1 = std::pow(base.min, e);
            p2 = std::pow(base.max, e);
        }

        bool is_p1_nan, is_p2_nan;
        if constexpr (std::is_same_v<T, hp_float>) {
            // boost::math::isnan 位于 boost/math/special_functions/fpclassify.hpp
            // 如果未包含，可以直接用 != 自身来判断 NaN，或者依赖 std::isnan 对模板的支持
            using boost::math::isnan;
            is_p1_nan = isnan(p1);
            is_p2_nan = isnan(p2);
        } else {
            is_p1_nan = std::isnan(p1);
            is_p2_nan = std::isnan(p2);
        }

        if (is_p1_nan || is_p2_nan) {
            return get_infinity_interval<T>(precision_bits);
        }

        // std::min/max 对 boost 类型也是安全的
        return Interval<T>{std::min(p1, p2), std::max(p1, p2)};
    }

    return get_infinity_interval<T>(precision_bits);
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
    // 情况 1: 整个区间都在定义域外 (x <= 0)
    // 例如 [-5, -2] 或 [-5, 0]
    // 此时函数无定义，必须返回 NaN。
    // 任何包含 NaN 的后续运算（如加减乘除）结果也会变成 NaN，
    // 最终在绘图判断时 (min <= 0 && max >= 0) 会因为与 NaN 比较返回 false 而被自动剔除。
    if (i.max <= T(0.0)) {
        T nan_val = std::numeric_limits<T>::quiet_NaN();
        return Interval<T>{nan_val, nan_val};
    }

    T min_val;
    // 情况 2: 区间跨越了 0 (例如 [-0.5, 2])
    // 实际上有效的输入部分是 (0, 2]，当 x 趋近于 0+ 时，ln(x) 趋近于 -infinity。
    // 所以区间的下界应该是负无穷大。
    if (i.min <= T(0.0)) {
        min_val = -std::numeric_limits<T>::infinity();
    } else {
        // 情况 3: 正常区间 (x > 0)，例如 [1, 2]
        // 正常计算对数
        min_val = log(i.min);
    }

    // 上界始终是 log(max)，因为我们已经排除了 max <= 0 的情况
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