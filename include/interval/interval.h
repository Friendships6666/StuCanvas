// --- 文件路径: include/interval/interval.h ---

#ifndef WASMTBBTEST_INTERVAL_H
#define WASMTBBTEST_INTERVAL_H
// 确保 FORCE_INLINE 宏已定义
#ifndef FORCE_INLINE
    #if defined(_MSC_VER)
        #define FORCE_INLINE __forceinline
    #else
        #define FORCE_INLINE inline __attribute__((always_inline))
    #endif
#endif
#include "../../pch.h"

// 1. 定义区间结构体
struct Interval {
    double min = 0.0;
    double max = 0.0;

    // 默认构造函数
    Interval() = default;
    // 从单个数值创建区间 (例如常量)
    Interval(double val) : min(val), max(val) {}
    // 从最小值和最大值创建区间
    Interval(double v_min, double v_max) : min(v_min), max(v_max) {}
};

// 2. 为 SIMD 定义区间批处理结构体 (SoA layout)
struct Interval_Batch {
    batch_type min;
    batch_type max;
};


// 3. 声明所有区间算术函数
//    这些函数将在 src/interval/IntervalEvaluate.cpp 中实现

// --- 标量版本 ---
Interval interval_add(const Interval& a, const Interval& b);
Interval interval_sub(const Interval& a, const Interval& b);
Interval interval_mul(const Interval& a, const Interval& b);
Interval interval_div(const Interval& a, const Interval& b);
Interval interval_pow(const Interval& base, const Interval& exp);
Interval interval_sin(const Interval& i);
Interval interval_cos(const Interval& i);
Interval interval_tan(const Interval& i);
Interval interval_ln(const Interval& i); // <--- 已更新
Interval interval_exp(const Interval& i);
Interval interval_sign(const Interval& i);
Interval interval_abs(const Interval& i);
// interval_safe_ln, interval_safe_exp, interval_check_ln 已被移除

// --- SIMD 批处理版本 ---
Interval_Batch interval_add_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_sub_batch(const Interval_Batch& a, const Interval_Batch& b);
Interval_Batch interval_mul_batch(const Interval_Batch& a, const Interval_Batch& b);
// ... (其他 SIMD 版本的声明可以根据需要添加)
Interval_Batch interval_div_batch(const Interval_Batch& a, const Interval_Batch& b); // 新增

Interval_Batch interval_sin_batch(const Interval_Batch& i); // 新增 (如果你还没添加)
Interval_Batch interval_cos_batch(const Interval_Batch& i); // 新增 (如果你还没添加)
Interval_Batch interval_tan_batch(const Interval_Batch& i); // 新增
Interval_Batch interval_ln_batch(const Interval_Batch& i);  // 新增 (如果你还没添加)
Interval_Batch interval_exp_batch(const Interval_Batch& i);  // 新增
Interval_Batch interval_sign_batch(const Interval_Batch& i); // 新增
Interval_Batch interval_abs_batch(const Interval_Batch& i);  // 新增
Interval_Batch interval_pow_batch(const Interval_Batch& base, const Interval_Batch& exp);
inline FORCE_INLINE Interval operator+(const Interval& a, const Interval& b) { return interval_add(a, b); }
inline FORCE_INLINE Interval operator-(const Interval& a, const Interval& b) { return interval_sub(a, b); }
inline FORCE_INLINE Interval operator*(const Interval& a, const Interval& b) { return interval_mul(a, b); }
inline FORCE_INLINE Interval operator/(const Interval& a, const Interval& b) { return interval_div(a, b); }

inline FORCE_INLINE Interval& operator+=(Interval& a, const Interval& b) { a = a + b; return a; }
inline FORCE_INLINE Interval& operator-=(Interval& a, const Interval& b) { a = a - b; return a; }
inline FORCE_INLINE Interval& operator*=(Interval& a, const Interval& b) { a = a * b; return a; }
inline FORCE_INLINE Interval& operator/=(Interval& a, const Interval& b) { a = a / b; return a; }


#endif //WASMTBBTEST_INTERVAL_H