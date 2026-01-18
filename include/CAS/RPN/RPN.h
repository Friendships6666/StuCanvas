// --- 文件路径: include/CAS/RPN/RPN.h ---

#ifndef RPN_H
#define RPN_H

#include "../../../pch.h"
#include "../../functions/functions.h"
#include "../../interval/interval.h"

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
    SIGN, ABS, CUSTOM_FUNCTION
};

struct RPNToken {
    RPNTokenType type;
    double value = 0.0;
};

constexpr size_t RPN_MAX_STACK_DEPTH = 64;


// ====================================================================
//          ↓↓↓ RPN 解析器声明 ↓↓↓
// ====================================================================

AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string);


/**
 * @brief 存储一个 RPN 程序及其所需的运行时计算精度和细分参数。
 */
struct IndustrialRPN {
    AlignedVector<RPNToken> program;
    unsigned int precision_bits;
    // 新增细分控制参数
    double min_pixel_threshold = 0.1; // 最终细分精度
    double start_pixel_threshold = 10.0; // 初始细分精度
    double step_factor = 2.0; // 细分步长
};

/**
 * @brief 解析 "RPN字符串;精度;最小像素;起始像素;步长" 格式的工业级 RPN 输入。
 */
inline IndustrialRPN parse_industrial_rpn(const std::string& rpn_with_precision) {
    std::vector<std::string> parts;
    std::stringstream ss(rpn_with_precision);
    std::string item;
    while (std::getline(ss, item, ';')) {
        parts.push_back(item);
    }

    if (parts.size() < 2) {
        throw std::runtime_error("Invalid industrial RPN format: must have at least 'RPN;Precision'. Input: '" + rpn_with_precision + "'");
    }

    IndustrialRPN result;

    // 1. 解析 RPN 和 精度
    try {
        result.program = parse_rpn(parts[0]);
        result.precision_bits = std::stoul(parts[1]);
    } catch (...) {
        throw std::runtime_error("Invalid RPN or Precision in '" + rpn_with_precision + "'");
    }

    // 2. 解析可选的细分参数
    try {
        if (parts.size() >= 3) result.min_pixel_threshold = std::stod(parts[2]);
        if (parts.size() >= 4) result.start_pixel_threshold = std::stod(parts[3]);
        if (parts.size() >= 5) result.step_factor = std::stod(parts[4]);
    } catch (...) {
         throw std::runtime_error("Invalid subdivision parameters in '" + rpn_with_precision + "'");
    }

    return result;
}


// --- 辅助类型特征，用于判断 T 是否为 Interval 类型 ---
template<typename> struct is_interval : std::false_type {};
template<typename T> struct is_interval<Interval<T>> : std::true_type {};

template<typename T>
FORCE_INLINE T evaluate_rpn(
    const RPNToken* p,    // 改为原始指针
    size_t len,           // 增加长度参数

    std::optional<T> x = std::nullopt,
    std::optional<T> y = std::nullopt,
    std::optional<T> t_param = std::nullopt,
    unsigned int precision_bits = 53)
{
    std::array<T, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (size_t i = 0; i < len; ++i) {
        const auto& t = p[i];
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
                else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, hp_float>) {
                    s[sp - 1] = (s[sp - 1] > T(0)) - (s[sp - 1] < T(0));
                }
                break;

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
            case RPNTokenType::CUSTOM_FUNCTION:
                throw std::runtime_error("Custom function not implemented");


        }
    }
    return s[0];
}
/**
 * @brief evaluate_rpn 的容器重载版本
 * 作用：作为包装器，将 AlignedVector 转换为指针和长度，转发给核心执行函数。
 * 优点：保持了对旧代码的兼容性，同时避免了任何内存拷贝。
 */
template<typename T>
FORCE_INLINE T evaluate_rpn(
    const AlignedVector<RPNToken>& v,           // 输入容器
    std::optional<T> x = std::nullopt,         // 可选变量 X
    std::optional<T> y = std::nullopt,         // 可选变量 Y
    std::optional<T> t_param = std::nullopt,   // 可选变量 T
    unsigned int precision_bits = 53           // 计算精度
) {


    // 转发调用指针版本的核心逻辑
    // v.data() 获取底层连续内存地址，v.size() 获取指令长度
    return evaluate_rpn<T>(
        v.data(),
        v.size(),
        x,
        y,
        t_param,
        precision_bits
    );
}


#endif //RPN_H