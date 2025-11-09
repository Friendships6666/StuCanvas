// --- 文件路径: include/CAS/RPN/RPN.h ---

#ifndef RPN_H
#define RPN_H

#include "../../../pch.h"
#include "../../functions/functions.h"
#include "../../interval/interval.h" // 引入 Interval 定义
#include <type_traits> // 引入 for std::is_same_v

enum class RPNTokenType { PUSH_CONST, PUSH_X, PUSH_Y, PUSH_T, ADD, SUB, MUL, DIV, SIN, COS, EXP, POW, SIGN, ABS, SAFE_LN, SAFE_EXP, CHECK_LN, TAN, LN };
struct RPNToken { RPNTokenType type; double value = 0.0; };

constexpr size_t RPN_MAX_STACK_DEPTH = 16;

// RPN 解析器的函数声明
AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string);

// --- 泛型化的 RPN 求值器 ---
template<typename T>
FORCE_INLINE T evaluate_rpn(
    const AlignedVector<RPNToken>& p,
    std::optional<T> x = std::nullopt,
    std::optional<T> y = std::nullopt,
    std::optional<T> t_param = std::nullopt)
{
    std::array<T, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST: s[sp++] = T(t.value); break;
            case RPNTokenType::PUSH_X:     s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y:     s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T:     s[sp++] = t_param.value(); break;

            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;
            case RPNTokenType::DIV:      --sp; s[sp - 1] /= s[sp]; break;

            case RPNTokenType::SIN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::sin(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::sin(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_sin(s[sp - 1]);
                break;
            case RPNTokenType::COS:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::cos(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::cos(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_cos(s[sp - 1]);
                break;
            case RPNTokenType::TAN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::tan(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::tan(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_tan(s[sp - 1]);
                break;
            case RPNTokenType::LN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::log(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::log(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_ln(s[sp - 1]);
                break;
            case RPNTokenType::EXP:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::exp(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::exp(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_exp(s[sp - 1]);
                break;
            case RPNTokenType::POW:
                --sp;
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::pow(s[sp - 1], s[sp]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::pow(s[sp - 1], s[sp]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_pow(s[sp - 1], s[sp]);
                break;
            case RPNTokenType::SIGN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = (s[sp - 1] > 0.0) - (s[sp - 1] < 0.0);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::sign(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_sign(s[sp - 1]);
                break;
            case RPNTokenType::ABS:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = std::abs(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::abs(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_abs(s[sp - 1]);
                break;

            // ====================================================================
            //  FIXED: 将对已删除函数的调用重定向到标准函数
            // ====================================================================
            case RPNTokenType::SAFE_LN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = safe_ln_scalar(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = safe_ln_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_ln(s[sp - 1]); // <-- 修正
                break;
            case RPNTokenType::CHECK_LN:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = check_ln_scalar(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = check_ln_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_ln(s[sp - 1]); // <-- 修正
                break;
            case RPNTokenType::SAFE_EXP:
                if constexpr (std::is_same_v<T, double>) s[sp - 1] = safe_exp_scalar(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = safe_exp_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval>) s[sp - 1] = interval_exp(s[sp - 1]); // <-- 修正
                break;
            // ====================================================================

        }
    }
    return s[0];
}

#endif //RPN_H