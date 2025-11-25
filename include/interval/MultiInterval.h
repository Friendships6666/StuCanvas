#ifndef MULTI_INTERVAL_H
#define MULTI_INTERVAL_H

#include "../../pch.h"
#include "interval.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <vector>

// =========================================================
//        ↓↓↓ 全局配置与辅助函数 ↓↓↓
// =========================================================

constexpr int MAX_MULTI_INTERVAL_PARTS = 16;

template<typename T>
constexpr T get_inf_val() {
    if constexpr (std::numeric_limits<T>::has_infinity) {
        return std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::max();
    }
}

// =========================================================
//  【核心优化】TLS 内存池获取函数
//   作用：获取一个预分配的、线程独享的 vector。
//   优势：既不在栈上（避免溢出），也不反复 malloc（避免性能慢）。
// =========================================================
template<typename T>
std::vector<Interval<T>>& get_tls_interval_buffer() {
    // static thread_local 保证每个线程只有一份，且生命周期贯穿整个程序
    static thread_local std::vector<Interval<T>> buffer;

    // 首次使用时预留足够空间 (16 * 16 * 2 = 512，给 1024 很安全)
    if (buffer.capacity() < 1024) {
        buffer.reserve(1024);
    }

    // 重置大小为 0，但保留 capacity 物理内存，零开销
    buffer.clear();
    return buffer;
}

// =========================================================
//        ↓↓↓ MultiInterval 核心结构体 ↓↓↓
// =========================================================

template<typename T>
struct MultiInterval {
    // 保持固定数组，作为数据容器非常高效
    Interval<T> parts[MAX_MULTI_INTERVAL_PARTS];
    int count = 0;

    MultiInterval() : count(0) {}

    MultiInterval(T val) {
        if (std::isfinite(static_cast<double>(val))) {
            parts[0] = Interval<T>(val);
            count = 1;
        } else {
            count = 0;
        }
    }

    MultiInterval(const Interval<T>& i) {
        bool valid = true;
        if constexpr (std::is_same_v<T, double>) {
            if (std::isnan(i.min) || std::isnan(i.max)) valid = false;
        }
        if (valid) {
            parts[0] = i;
            count = 1;
        } else {
            count = 0;
        }
    }

    void add_unsafe(const Interval<T>& i) {
        if (count < MAX_MULTI_INTERVAL_PARTS) {
            parts[count++] = i;
        }
    }

    bool contains_zero() const {
        T zero{0};
        if (count == 1) {
            return parts[0].min <= zero && parts[0].max >= zero;
        }
        for (int i = 0; i < count; ++i) {
            if (parts[i].min <= zero && parts[i].max >= zero) return true;
        }
        return false;
    }

    // 核心简化逻辑，使用指针操作，高性能
    void load_and_simplify(Interval<T>* buffer, int len) {
        if (len == 0) { count = 0; return; }

        std::sort(buffer, buffer + len, [](const Interval<T>& a, const Interval<T>& b) {
            return a.min < b.min;
        });

        int write_idx = 0;
        for (int i = 1; i < len; ++i) {
            Interval<T>& top = buffer[write_idx];
            const Interval<T>& curr = buffer[i];

            using std::max; using boost::multiprecision::max;

            if (top.max >= curr.min) {
                top.max = max(top.max, curr.max);
            } else {
                write_idx++;
                buffer[write_idx] = curr;
            }
        }
        int merged_count = write_idx + 1;

        while (merged_count > MAX_MULTI_INTERVAL_PARTS) {
            int best_idx = 0;
            T min_gap = get_inf_val<T>();

            for (int i = 0; i < merged_count - 1; ++i) {
                T gap = buffer[i+1].min - buffer[i].max;
                if (gap < min_gap) {
                    min_gap = gap;
                    best_idx = i;
                }
            }

            buffer[best_idx].max = buffer[best_idx+1].max;
            for (int i = best_idx + 1; i < merged_count - 1; ++i) {
                buffer[i] = buffer[i+1];
            }
            merged_count--;
        }

        this->count = merged_count;
        for (int i = 0; i < merged_count; ++i) {
            this->parts[i] = buffer[i];
        }
    }
};

// =========================================================
//              算术运算符重载 (TLS 优化版)
// =========================================================

template<typename T, typename OpFunc>
inline MultiInterval<T> apply_cartesian(const MultiInterval<T>& A, const MultiInterval<T>& B, OpFunc op) {
    MultiInterval<T> res;

    // 快速路径：90% 的情况，直接计算，不涉及 buffer
    if (A.count == 1 && B.count == 1) {
        res.parts[0] = op(A.parts[0], B.parts[0]);
        res.count = 1;
        return res;
    }

    // 慢速路径：使用 TLS Buffer
    std::vector<Interval<T>>& temp_buffer = get_tls_interval_buffer<T>();

    for (int i = 0; i < A.count; ++i) {
        for (int j = 0; j < B.count; ++j) {
            temp_buffer.push_back(op(A.parts[i], B.parts[j]));
        }
    }

    res.load_and_simplify(temp_buffer.data(), (int)temp_buffer.size());
    return res;
}

template<typename T> MultiInterval<T> operator+(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_add<T>); }
template<typename T> MultiInterval<T> operator-(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_sub<T>); }
template<typename T> MultiInterval<T> operator*(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_mul<T>); }

// --- 除法 (带分裂逻辑) ---
template<typename T>
MultiInterval<T> operator/(const MultiInterval<T>& Num, const MultiInterval<T>& Den) {
    T zero(0);

    // 快速路径
    if (Num.count == 1 && Den.count == 1) {
        if (Den.parts[0].min > zero || Den.parts[0].max < zero) {
            MultiInterval<T> res;
            res.parts[0] = interval_div(Num.parts[0], Den.parts[0]);
            res.count = 1;
            return res;
        }
    }

    MultiInterval<T> res;
    T inf_val = get_inf_val<T>();

    // 使用 TLS Buffer
    std::vector<Interval<T>>& temp_buffer = get_tls_interval_buffer<T>();

    for (int i = 0; i < Num.count; ++i) {
        for (int j = 0; j < Den.count; ++j) {
            const auto& n = Num.parts[i];
            const auto& d = Den.parts[j];
            bool n_contains_zero = (n.min <= zero && n.max >= zero);

            if (d.min < zero && d.max > zero) {
                if (d.min != zero) {
                    if (n_contains_zero) temp_buffer.push_back(Interval<T>(-inf_val, inf_val));
                    else temp_buffer.push_back(interval_mul(n, Interval<T>{-inf_val, T(1.0)/d.min}));
                }
                if (d.max != zero) {
                    if (n_contains_zero) temp_buffer.push_back(Interval<T>(-inf_val, inf_val));
                    else temp_buffer.push_back(interval_mul(n, Interval<T>{T(1.0)/d.max, inf_val}));
                }
            } else if (d.min == zero && d.max == zero) {
                continue;
            } else if (d.max == zero) {
                if (n_contains_zero) temp_buffer.push_back(Interval<T>(-inf_val, inf_val));
                else temp_buffer.push_back(interval_mul(n, Interval<T>{-inf_val, T(1.0)/d.min}));
            } else if (d.min == zero) {
                if (n_contains_zero) temp_buffer.push_back(Interval<T>(-inf_val, inf_val));
                else temp_buffer.push_back(interval_mul(n, Interval<T>{T(1.0)/d.max, inf_val}));
            } else {
                temp_buffer.push_back(interval_div(n, d));
            }
        }
    }
    res.load_and_simplify(temp_buffer.data(), (int)temp_buffer.size());
    return res;
}

// =========================================================
//              数学函数支持 (一元)
// =========================================================

// 一元运算无需 buffer，直接 map
template<typename T, typename Func>
MultiInterval<T> map_unary(const MultiInterval<T>& A, Func f) {
    MultiInterval<T> res;
    res.count = A.count;
    for (int i = 0; i < A.count; ++i) {
        res.parts[i] = f(A.parts[i]);
    }
    return res;
}

template<typename T> MultiInterval<T> multi_sin(const MultiInterval<T>& A) { return map_unary(A, interval_sin<T>); }
template<typename T> MultiInterval<T> multi_cos(const MultiInterval<T>& A) { return map_unary(A, interval_cos<T>); }
template<typename T> MultiInterval<T> multi_exp(const MultiInterval<T>& A) { return map_unary(A, interval_exp<T>); }
template<typename T> MultiInterval<T> multi_abs(const MultiInterval<T>& A) { return map_unary(A, interval_abs<T>); }

template<typename T>
MultiInterval<T> multi_ln(const MultiInterval<T>& A) {
    MultiInterval<T> res;
    T zero(0);
    int idx = 0;
    for (int i = 0; i < A.count; ++i) {
        if (A.parts[i].max <= zero) continue;
        res.parts[idx++] = interval_ln(A.parts[i]);
    }
    res.count = idx;
    return res;
}

template<typename T>
MultiInterval<T> multi_tan(const MultiInterval<T>& A) {
    return multi_sin(A) / multi_cos(A);
}

// Gamma 函数 (使用 TLS)
template<typename T>
MultiInterval<T> multi_gamma(const MultiInterval<T>& A) {
    MultiInterval<T> res;
    T inf_val = get_inf_val<T>();

    std::vector<Interval<T>>& temp_buffer = get_tls_interval_buffer<T>();

    for (int i = 0; i < A.count; ++i) {
        const auto& part = A.parts[i];
        T k_start = std::ceil(part.min);
        T k_end = std::floor(part.max);

        if (k_start <= k_end && k_start <= T(0)) {
            temp_buffer.push_back(Interval<T>(-inf_val, inf_val));
        } else {
            temp_buffer.push_back(interval_gamma(part));
        }
    }
    res.load_and_simplify(temp_buffer.data(), (int)temp_buffer.size());
    return res;
}

// --- 文件路径: include/interval/MultiInterval.h ---

// 放到文件末尾的 multi_pow 实现

template<typename T>
MultiInterval<T> multi_pow(const MultiInterval<T>& Base, const MultiInterval<T>& Exp) {
    MultiInterval<T> res;

    // =========================================================
    // 1. 快速路径优化 (Fast Path)
    // 90% 的情况是单区间对单区间 (例如 y - x^2)，直接计算可避免开销
    // =========================================================
    if (Base.count == 1 && Exp.count == 1) {
        // 直接调用 include/interval/interval.h 中已修复的 interval_pow
        // 由于 interval_pow 现在内部处理了命名空间，这里直接调用是安全的
        res.parts[0] = interval_pow(Base.parts[0], Exp.parts[0]);
        res.count = 1;
        return res;
    }

    // =========================================================
    // 2. 慢速路径：笛卡尔积 (Cartesian Product)
    // 处理多区间的情况 (例如 tan(x) 产生的多段区间作为底数)
    // =========================================================
    std::vector<Interval<T>>& temp_buffer = get_tls_interval_buffer<T>();

    for (int i = 0; i < Base.count; ++i) {
        for (int j = 0; j < Exp.count; ++j) {
            temp_buffer.push_back(interval_pow(Base.parts[i], Exp.parts[j]));
        }
    }

    // 3. 加载并简化结果 (合并重叠区间)
    res.load_and_simplify(temp_buffer.data(), (int)temp_buffer.size());
    return res;
}

#endif // MULTI_INTERVAL_H