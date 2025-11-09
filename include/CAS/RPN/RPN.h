#ifndef RPN_H
#define RPN_H

#include "../../../pch.h"
#include "../../functions/functions.h"

enum class RPNTokenType { PUSH_CONST, PUSH_X, PUSH_Y, PUSH_T, ADD, SUB, MUL, DIV, SIN, COS, EXP, POW, SIGN, ABS, SAFE_LN, SAFE_EXP, CHECK_LN, TAN, LN };
struct RPNToken { RPNTokenType type; double value = 0.0; };

constexpr size_t RPN_MAX_STACK_DEPTH = 16;

AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string);

// --- 将函数体移动到这里 ---
FORCE_INLINE double evaluate_rpn(const AlignedVector<RPNToken>& p, std::optional<double> x = std::nullopt, std::optional<double> y = std::nullopt, std::optional<double> t_param = std::nullopt) {
    std::array<double, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST: s[sp++] = t.value; break;
            case RPNTokenType::PUSH_X: s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y: s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T: s[sp++] = t_param.value(); break;
            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;
            case RPNTokenType::DIV:      --sp; s[sp - 1] /= s[sp]; break;
            case RPNTokenType::SIN:      s[sp - 1] = std::sin(s[sp - 1]); break;
            case RPNTokenType::COS:      s[sp - 1] = std::cos(s[sp - 1]); break;
            case RPNTokenType::TAN:      s[sp - 1] = std::tan(s[sp - 1]); break;
            case RPNTokenType::LN:       s[sp - 1] = std::log(s[sp - 1]); break;
            case RPNTokenType::EXP:      s[sp - 1] = std::exp(s[sp - 1]); break;
            case RPNTokenType::POW:      --sp; s[sp - 1] = std::pow(s[sp - 1], s[sp]); break;
            case RPNTokenType::SIGN:     s[sp - 1] = (s[sp - 1] > 0.0) - (s[sp - 1] < 0.0); break;
            case RPNTokenType::ABS:      s[sp - 1] = std::abs(s[sp - 1]); break;
            case RPNTokenType::SAFE_LN:  s[sp - 1] = safe_ln_scalar(s[sp - 1]); break;
            case RPNTokenType::CHECK_LN: s[sp - 1] = check_ln_scalar(s[sp - 1]); break;
            case RPNTokenType::SAFE_EXP: s[sp - 1] = safe_exp_scalar(s[sp - 1]); break;
        }
    }
    return s[0];
}

FORCE_INLINE batch_type evaluate_rpn_batch(const AlignedVector<RPNToken>& p, std::optional<batch_type> x = std::nullopt, std::optional<batch_type> y = std::nullopt, std::optional<batch_type> t_param = std::nullopt) {
    std::array<batch_type, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST: s[sp++] = batch_type(t.value); break;
            case RPNTokenType::PUSH_X: s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y: s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T: s[sp++] = t_param.value(); break;
            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;
            case RPNTokenType::DIV:      --sp; s[sp - 1] /= s[sp]; break;
            case RPNTokenType::SIN:      s[sp - 1] = xs::sin(s[sp - 1]); break;
            case RPNTokenType::COS:      s[sp - 1] = xs::cos(s[sp - 1]); break;
            case RPNTokenType::TAN:      s[sp - 1] = xs::tan(s[sp - 1]); break;
            case RPNTokenType::LN:       s[sp - 1] = xs::log(s[sp - 1]); break;
            case RPNTokenType::EXP:      s[sp - 1] = xs::exp(s[sp - 1]); break;
            case RPNTokenType::POW:      --sp; s[sp - 1] = xs::pow(s[sp - 1], s[sp]); break;
            case RPNTokenType::SIGN:     s[sp - 1] = xs::sign(s[sp - 1]); break;
            case RPNTokenType::ABS:      s[sp - 1] = xs::abs(s[sp - 1]); break;
            case RPNTokenType::SAFE_LN:  s[sp - 1] = safe_ln_batch(s[sp - 1]); break;
            case RPNTokenType::SAFE_EXP: s[sp - 1] = safe_exp_batch(s[sp - 1]); break;
            case RPNTokenType::CHECK_LN: s[sp - 1] = check_ln_batch(s[sp - 1]); break;
        }
    }
    return s[0];
}

#endif //RPN_H