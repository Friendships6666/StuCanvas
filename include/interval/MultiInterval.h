#ifndef MULTI_INTERVAL_H
#define MULTI_INTERVAL_H

#include "../../pch.h"
#include "interval.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>

// =========================================================
//        ↓↓↓ 全局配置与辅助函数 ↓↓↓
// =========================================================

// 工业级阈值：16 个碎片足以处理极复杂的函数 (如 tan(tan(x)))
// 超过此数量会自动合并最小间隙，防止组合爆炸
constexpr int MAX_MULTI_INTERVAL_PARTS = 16;

// 栈上临时缓冲区大小：用于笛卡尔积中间计算
constexpr int TEMP_BUFFER_SIZE = MAX_MULTI_INTERVAL_PARTS * MAX_MULTI_INTERVAL_PARTS;

// 辅助：获取无穷大 (兼容 boost::multiprecision 和标准类型)
template<typename T>
constexpr T get_inf_val() {
    if constexpr (std::numeric_limits<T>::has_infinity) {
        return std::numeric_limits<T>::infinity();
    } else {
        return std::numeric_limits<T>::max();
    }
}

// =========================================================
//        ↓↓↓ MultiInterval 核心结构体 ↓↓↓
// =========================================================

template<typename T>
struct MultiInterval {
    // 核心优化：直接在结构体内部定义固定大小数组
    // 避免 std::vector 的堆内存分配，极大提升缓存命中率
    Interval<T> parts[MAX_MULTI_INTERVAL_PARTS];

    // 当前有效区间数量
    int count = 0;

    // --- 构造函数 ---
    MultiInterval() : count(0) {}

    // 从标量构造
    MultiInterval(T val) {
        // 简单过滤 NaN/Inf
        if (std::isfinite(static_cast<double>(val))) {
            parts[0] = Interval<T>(val);
            count = 1;
        } else {
            count = 0;
        }
    }

    // 从单区间构造
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

    // --- 简单添加 (不检查溢出，由 simplify 保证安全) ---
    void add_unsafe(const Interval<T>& i) {
        if (count < MAX_MULTI_INTERVAL_PARTS) {
            parts[count++] = i;
        }
    }

    // --- 判定是否包含 0 (用于绘图剔除) ---
    bool contains_zero() const {
        T zero(0);
        // 针对最常见情况 (count=1) 的极速优化
        if (count == 1) {
            return parts[0].min <= zero && parts[0].max >= zero;
        }
        for (int i = 0; i < count; ++i) {
            if (parts[i].min <= zero && parts[i].max >= zero) return true;
        }
        return false;
    }

    // --- 核心：从缓冲区加载并简化 (Load, Sort, Merge, Limit) ---
    // buffer: 外部传入的临时数组
    // len: 缓冲区中元素的数量
    void load_and_simplify(Interval<T>* buffer, int len) {
        if (len == 0) { count = 0; return; }

        // 1. 排序：按左端点从小到大
        std::sort(buffer, buffer + len, [](const Interval<T>& a, const Interval<T>& b) {
            return a.min < b.min;
        });

        // 2. 基础合并 (合并重叠或相接的区间)
        // 直接在 buffer 原地进行，节省空间
        int write_idx = 0;
        for (int i = 1; i < len; ++i) {
            Interval<T>& top = buffer[write_idx];
            const Interval<T>& curr = buffer[i];

            using std::max; using boost::multiprecision::max;

            // 如果重叠或首尾相接 (top.max >= curr.min)
            if (top.max >= curr.min) {
                top.max = max(top.max, curr.max);
            } else {
                write_idx++;
                buffer[write_idx] = curr;
            }
        }
        int merged_count = write_idx + 1;

        // 3. 强制压缩 (Gap Filling)
        // 如果合并后数量依然超过容量，必须剔除间隙最小的，防止组合爆炸
        while (merged_count > MAX_MULTI_INTERVAL_PARTS) {
            int best_idx = 0;
            T min_gap = get_inf_val<T>();

            // 寻找间距最小的两个相邻区间
            for (int i = 0; i < merged_count - 1; ++i) {
                T gap = buffer[i+1].min - buffer[i].max;
                if (gap < min_gap) {
                    min_gap = gap;
                    best_idx = i;
                }
            }

            // 合并 best_idx 和 best_idx+1
            // 新的 max 是右边区间的 max，从而填补了中间的 gap
            buffer[best_idx].max = buffer[best_idx+1].max;

            // 移动后续元素 (Array Move)
            for (int i = best_idx + 1; i < merged_count - 1; ++i) {
                buffer[i] = buffer[i+1];
            }
            merged_count--;
        }

        // 4. 复制回对象
        this->count = merged_count;
        for (int i = 0; i < merged_count; ++i) {
            this->parts[i] = buffer[i];
        }
    }
};

// =========================================================
//              算术运算符重载 (带快速路径优化)
// =========================================================

// 通用笛卡尔积模板
template<typename T, typename OpFunc>
inline MultiInterval<T> apply_cartesian(const MultiInterval<T>& A, const MultiInterval<T>& B, OpFunc op) {
    MultiInterval<T> res;

    // 【极速优化】如果两边都只有一个区间，直接算，跳过循环和排序！
    // 这是 90% 的情况，性能提升的关键。
    if (A.count == 1 && B.count == 1) {
        res.parts[0] = op(A.parts[0], B.parts[0]);
        res.count = 1;
        return res;
    }

    // 慢速路径：笛卡尔积
    // 使用栈上临时缓冲区，避免 heap alloc
    Interval<T> temp_buffer[TEMP_BUFFER_SIZE];
    int temp_count = 0;

    for (int i = 0; i < A.count; ++i) {
        for (int j = 0; j < B.count; ++j) {
            temp_buffer[temp_count++] = op(A.parts[i], B.parts[j]);
        }
    }

    // 统一简化并存入结果
    res.load_and_simplify(temp_buffer, temp_count);
    return res;
}

// 加减乘
template<typename T> MultiInterval<T> operator+(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_add<T>); }
template<typename T> MultiInterval<T> operator-(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_sub<T>); }
template<typename T> MultiInterval<T> operator*(const MultiInterval<T>& A, const MultiInterval<T>& B) { return apply_cartesian(A, B, interval_mul<T>); }

// --- 除法 (带分裂逻辑 + 0/0 保护) ---
template<typename T>
MultiInterval<T> operator/(const MultiInterval<T>& Num, const MultiInterval<T>& Den) {
    T zero(0);

    // 【极速优化】如果分母只有一个区间，且不跨越0，直接算
    if (Num.count == 1 && Den.count == 1) {
        if (Den.parts[0].min > zero || Den.parts[0].max < zero) {
            MultiInterval<T> res;
            res.parts[0] = interval_div(Num.parts[0], Den.parts[0]);
            res.count = 1;
            return res;
        }
    }

    // 慢速路径 (含分裂逻辑)
    MultiInterval<T> res;
    T inf_val = get_inf_val<T>();

    // 缓冲区稍微大一点，因为除法可能分裂
    Interval<T> temp_buffer[512];
    int temp_count = 0;

    for (int i = 0; i < Num.count; ++i) {
        for (int j = 0; j < Den.count; ++j) {
            const auto& n = Num.parts[i];
            const auto& d = Den.parts[j];

            // 分子含0检测 (用于 0/0 保护)
            bool n_contains_zero = (n.min <= zero && n.max >= zero);

            if (d.min < zero && d.max > zero) {
                // 分裂：跨越 0
                if (d.min != zero) {
                    if (n_contains_zero) temp_buffer[temp_count++] = Interval<T>(-inf_val, inf_val); // 0/0 保护
                    else temp_buffer[temp_count++] = interval_mul(n, Interval<T>{-inf_val, T(1.0)/d.min});
                }
                if (d.max != zero) {
                    if (n_contains_zero) temp_buffer[temp_count++] = Interval<T>(-inf_val, inf_val); // 0/0 保护
                    else temp_buffer[temp_count++] = interval_mul(n, Interval<T>{T(1.0)/d.max, inf_val});
                }
            } else if (d.min == zero && d.max == zero) {
                continue; // 纯 0，无定义
            } else if (d.max == zero) {
                // 左侧接触 0
                if (n_contains_zero) temp_buffer[temp_count++] = Interval<T>(-inf_val, inf_val);
                else temp_buffer[temp_count++] = interval_mul(n, Interval<T>{-inf_val, T(1.0)/d.min});
            } else if (d.min == zero) {
                // 右侧接触 0
                if (n_contains_zero) temp_buffer[temp_count++] = Interval<T>(-inf_val, inf_val);
                else temp_buffer[temp_count++] = interval_mul(n, Interval<T>{T(1.0)/d.max, inf_val});
            } else {
                // 正常除法
                temp_buffer[temp_count++] = interval_div(n, d);
            }
        }
    }
    res.load_and_simplify(temp_buffer, temp_count);
    return res;
}

// =========================================================
//              数学函数支持 (一元)
// =========================================================

template<typename T, typename Func>
MultiInterval<T> map_unary(const MultiInterval<T>& A, Func f) {
    MultiInterval<T> res;
    // 一元运算通常不改变区间数量，直接循环赋值，不需要 simplify
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

// 对数：需处理定义域
template<typename T>
MultiInterval<T> multi_ln(const MultiInterval<T>& A) {
    MultiInterval<T> res;
    T zero(0);
    int idx = 0;
    for (int i = 0; i < A.count; ++i) {
        // 预先过滤掉完全在定义域外的区间
        if (A.parts[i].max <= zero) continue;
        // interval_ln 内部会处理跨越 0 的情况 (返回 -inf)
        res.parts[idx++] = interval_ln(A.parts[i]);
    }
    res.count = idx;
    return res;
}

// 正切：使用 sin/cos 实现以获得自动分裂特性
template<typename T>
MultiInterval<T> multi_tan(const MultiInterval<T>& A) {
    return multi_sin(A) / multi_cos(A);
}

// Gamma 函数：处理负整数分裂
template<typename T>
MultiInterval<T> multi_gamma(const MultiInterval<T>& A) {
    MultiInterval<T> res;
    T inf_val = get_inf_val<T>();

    // 这里的 buffer 需要大一点，因为每个区间可能分裂
    Interval<T> temp_buffer[MAX_MULTI_INTERVAL_PARTS * 2];
    int temp_count = 0;

    for (int i = 0; i < A.count; ++i) {
        const auto& part = A.parts[i];
        T k_start = std::ceil(part.min);
        T k_end = std::floor(part.max);

        // 如果区间内包含非正整数 (0, -1, -2...)
        if (k_start <= k_end && k_start <= T(0)) {
            // 跨越渐近线，分裂为全实数轴 (保守策略)
            temp_buffer[temp_count++] = Interval<T>(-inf_val, inf_val);
        } else {
            // 正常计算
            temp_buffer[temp_count++] = interval_gamma(part);
        }
    }
    res.load_and_simplify(temp_buffer, temp_count);
    return res;
}

// 幂函数
template<typename T>
MultiInterval<T> multi_pow(const MultiInterval<T>& Base, const MultiInterval<T>& Exp) {
    MultiInterval<T> res;
    Interval<T> temp_buffer[TEMP_BUFFER_SIZE];
    int temp_count = 0;

    for(int i = 0; i < Base.count; ++i) {
        for(int j = 0; j < Exp.count; ++j) {
            temp_buffer[temp_count++] = interval_pow(Base.parts[i], Exp.parts[j]);
        }
    }
    res.load_and_simplify(temp_buffer, temp_count);
    return res;
}

#endif // MULTI_INTERVAL_H