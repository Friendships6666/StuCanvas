// --- 文件路径: include/CAS/RPN/RPN.h ---

#ifndef RPN_H
#define RPN_H

#include "../../../pch.h"
#include "../../functions/functions.h"
#include "../../interval/interval.h" // 引入 Interval<T> 模板

// ====================================================================
//          ↓↓↓ RPN Token 定义 ↓↓↓
// ====================================================================

enum class RPNTokenType {
    // Variables and Constants
    PUSH_CONST, PUSH_X, PUSH_Y, PUSH_T,
    // Basic Arithmetic
    ADD, SUB, MUL, DIV,
    // Powers and Roots
    POW, SQRT,
    // Exponential and Logarithmic
    EXP, LN, SAFE_LN, SAFE_EXP, CHECK_LN,
    // Trigonometric
    SIN, COS, TAN,
    // Other
    SIGN, ABS
};

struct RPNToken {
    RPNTokenType type;
    double value = 0.0;
};

constexpr size_t RPN_MAX_STACK_DEPTH = 64;


// ====================================================================
//          ↓↓↓ RPN 解析器声明 ↓↓↓
// ====================================================================

// 旧版解析器，用于解析纯 RPN 字符串
AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string);


/**
 * @brief 存储一个 RPN 程序及其所需的运行时计算精度。
 */
struct IndustrialRPN {
    AlignedVector<RPNToken> program;
    unsigned int precision_bits;
};

/**
 * @brief 解析 "RPN字符串;精度" 格式的工业级 RPN 输入。
 * @param rpn_with_precision 输入字符串，例如 "x 2 pow;256"。
 * @return 一个包含 RPN 程序和精度的 IndustrialRPN 结构体。
 * @throws std::runtime_error 如果格式无效或精度不是有效数字。
 */
inline IndustrialRPN parse_industrial_rpn(const std::string& rpn_with_precision) {
    size_t semicolon_pos = rpn_with_precision.rfind(';');
    if (semicolon_pos == std::string::npos) {
        throw std::runtime_error("Invalid industrial RPN format: semicolon not found in '" + rpn_with_precision + "'");
    }

    std::string rpn_part = rpn_with_precision.substr(0, semicolon_pos);
    std::string precision_part = rpn_with_precision.substr(semicolon_pos + 1);

    if (rpn_part.empty() || precision_part.empty()) {
        throw std::runtime_error("Invalid industrial RPN format: RPN or precision part is empty in '" + rpn_with_precision + "'");
    }

    try {
        unsigned int precision = std::stoul(precision_part);
        AlignedVector<RPNToken> program = parse_rpn(rpn_part);
        return {std::move(program), precision};
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("Invalid precision value in '" + rpn_with_precision + "': not a valid number.");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Precision value in '" + rpn_with_precision + "' is out of range.");
    }
}


// --- 辅助类型特征，用于判断 T 是否为 Interval 类型 ---
template<typename> struct is_interval : std::false_type {};
template<typename T> struct is_interval<Interval<T>> : std::true_type {};

template<typename T>
FORCE_INLINE T evaluate_rpn(
    const AlignedVector<RPNToken>& p,
    std::optional<T> x = std::nullopt,
    std::optional<T> y = std::nullopt,
    std::optional<T> t_param = std::nullopt,
    unsigned int precision_bits = 53)
{
    std::array<T, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST:
                if constexpr (std::is_same_v<T, Interval_Batch>) {
                    s[sp++] = {batch_type(t.value), batch_type(t.value)};
                } else {
                    s[sp++] = T(t.value);
                }
                break;
            case RPNTokenType::PUSH_X:     s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y:     s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T:     s[sp++] = t_param.value(); break;

            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;

            case RPNTokenType::DIV:
                --sp;
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_div(s[sp - 1], s[sp], precision_bits);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_div_batch(s[sp - 1], s[sp]);
                else s[sp - 1] /= s[sp];
                break;

            case RPNTokenType::POW:
                --sp;
                 if constexpr (is_interval<T>::value) s[sp - 1] = interval_pow(s[sp - 1], s[sp], precision_bits);
                 else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_pow_batch(s[sp - 1], s[sp]);
                 else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::pow(s[sp - 1], s[sp]);
                 else s[sp - 1] = pow(s[sp - 1], s[sp]);
                break;

            case RPNTokenType::SQRT:
                 if constexpr (is_interval<T>::value) s[sp - 1] = interval_sqrt(s[sp - 1], precision_bits);
                 else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_sqrt_batch(s[sp - 1]);
                 else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::sqrt(s[sp - 1]);
                 else s[sp - 1] = sqrt(s[sp - 1]);
                break;

            case RPNTokenType::SIN:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_sin(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_sin_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::sin(s[sp - 1]);
                else s[sp - 1] = sin(s[sp - 1]);
                break;

            case RPNTokenType::COS:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_cos(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_cos_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::cos(s[sp - 1]);
                else s[sp - 1] = cos(s[sp - 1]);
                break;

            case RPNTokenType::TAN:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_tan(s[sp - 1], precision_bits);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_tan_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::tan(s[sp - 1]);
                else s[sp - 1] = tan(s[sp - 1]);
                break;

            case RPNTokenType::LN:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_ln(s[sp - 1], precision_bits);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_ln_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::log(s[sp - 1]);
                else s[sp - 1] = log(s[sp - 1]);
                break;

            case RPNTokenType::EXP:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_exp(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_exp_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::exp(s[sp - 1]);
                else s[sp - 1] = exp(s[sp - 1]);
                break;

            case RPNTokenType::ABS:
                if constexpr (is_interval<T>::value) s[sp - 1] = interval_abs(s[sp - 1]);
                else if constexpr (std::is_same_v<T, Interval_Batch>) s[sp - 1] = interval_abs_batch(s[sp - 1]);
                else if constexpr (std::is_same_v<T, batch_type>) s[sp - 1] = xs::abs(s[sp - 1]);
                else s[sp - 1] = abs(s[sp - 1]);
                break;

            // ====================================================================
            //          ↓↓↓ 这是关键的修正区域 ↓↓↓
            // ====================================================================
            case RPNTokenType::SIGN:
                if constexpr (is_interval<T>::value) {
                    s[sp - 1] = interval_sign(s[sp - 1]);
                }
                else if constexpr (std::is_same_v<T, Interval_Batch>) {
                    s[sp - 1] = interval_sign_batch(s[sp - 1]);
                }
                else if constexpr (std::is_same_v<T, batch_type>) {
                    s[sp - 1] = xs::sign(s[sp - 1]);
                }
                // 明确处理 double 和 hp_float 的情况
                else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, hp_float>) {
                    s[sp - 1] = (s[sp - 1] > T(0)) - (s[sp - 1] < T(0));
                }
                break;
            // ====================================================================
            //                          ↑↑↑ 修改结束 ↑↑↑
            // ====================================================================

            case RPNTokenType::SAFE_LN:
                if constexpr (is_interval<T>::value) { s[sp - 1] = interval_ln(s[sp - 1], precision_bits); }
                else if constexpr (std::is_same_v<T, double>) { s[sp - 1] = safe_ln_scalar(s[sp - 1]); }
                else if constexpr (std::is_same_v<T, batch_type>) { s[sp - 1] = safe_ln_batch(s[sp - 1]); }
                break;
            case RPNTokenType::CHECK_LN:
                if constexpr (is_interval<T>::value) { s[sp - 1] = interval_ln(s[sp - 1], precision_bits); }
                else if constexpr (std::is_same_v<T, double>) { s[sp - 1] = check_ln_scalar(s[sp - 1]); }
                else if constexpr (std::is_same_v<T, batch_type>) { s[sp - 1] = check_ln_batch(s[sp - 1]); }
                break;
            case RPNTokenType::SAFE_EXP:
                if constexpr (is_interval<T>::value) { s[sp - 1] = interval_exp(s[sp - 1]); }
                else if constexpr (std::is_same_v<T, double>) { s[sp - 1] = safe_exp_scalar(s[sp - 1]); }
                else if constexpr (std::is_same_v<T, batch_type>) { s[sp - 1] = safe_exp_batch(s[sp - 1]); }
                break;
        }
    }
    return s[0];
}


#endif //RPN_H