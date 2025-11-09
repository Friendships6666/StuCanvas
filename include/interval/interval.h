// --- 文件路径: include/interval/interval.h ---

#ifndef WASMTBBTEST_INTERVAL_H
#define WASMTBBTEST_INTERVAL_H

#include "../../pch.h"

// 1. 定义区间结构体
struct Interval {
    double min = 0.0;
    double max = 0.0;

    // 从单个数值创建区间 (例如常量)
    Interval(double val) : min(val), max(val) {}
    // 从最小值和最大值创建区间
    Interval(double v_min, double v_max) : min(v_min), max(v_max) {}
};


// 2. 声明所有区间算术函数 (当前不实现)
//    这些函数是未来让 evaluate_rpn 支持 Interval 类型所必需的。

// 二元运算
Interval interval_add(const Interval& a, const Interval& b);
Interval interval_sub(const Interval& a, const Interval& b);
Interval interval_mul(const Interval& a, const Interval& b);
Interval interval_div(const Interval& a, const Interval& b);
Interval interval_pow(const Interval& base, const Interval& exp);

// 一元运算
Interval interval_sin(const Interval& i);
Interval interval_cos(const Interval& i);
Interval interval_tan(const Interval& i);
Interval interval_ln(const Interval& i);
Interval interval_exp(const Interval& i);
Interval interval_sign(const Interval& i);
Interval interval_abs(const Interval& i);

// 特殊安全函数
Interval interval_safe_ln(const Interval& i);
Interval interval_safe_exp(const Interval& i);
Interval interval_check_ln(const Interval& i);


#endif //WASMTBBTEST_INTERVAL_H