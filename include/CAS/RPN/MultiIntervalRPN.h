#ifndef MULTI_INTERVAL_RPN_H
#define MULTI_INTERVAL_RPN_H

#include "../../pch.h"
#include "../../interval/MultiInterval.h"
#include "RPN.h"

template<typename T>
MultiInterval<T> evaluate_rpn_multi(
    const AlignedVector<RPNToken>& prog,
    const Interval<T>& x_val,
    const Interval<T>& y_val,
    const Interval<T>& t_val = Interval<T>(T(0)),
    unsigned int precision_bits = 53
) {
    // 栈仍然用 vector，但现在元素很小（16个区间 ≈ 256字节），拷贝很快
    // 也可以优化为 std::array 做的固定栈，进一步消除 heap alloc
    // 但 vector.reserve 之后对于小深度栈来说性能已经足够
    std::vector<MultiInterval<T>> stack;
    stack.reserve(32);

    MultiInterval<T> mx(x_val);
    MultiInterval<T> my(y_val);
    MultiInterval<T> mt(t_val);

    for (const auto& token : prog) {
        switch (token.type) {
            case RPNTokenType::PUSH_CONST:
                stack.emplace_back(T(token.value));
                break;
            case RPNTokenType::PUSH_X: stack.push_back(mx); break;
            case RPNTokenType::PUSH_Y: stack.push_back(my); break;
            case RPNTokenType::PUSH_T: stack.push_back(mt); break;

            case RPNTokenType::ADD: {
                // 这里的拷贝非常快，类似 memcpy
                MultiInterval<T> b = stack.back(); stack.pop_back();
                stack.back() = stack.back() + b;
                break;
            }
            case RPNTokenType::SUB: {
                MultiInterval<T> b = stack.back(); stack.pop_back();
                stack.back() = stack.back() - b;
                break;
            }
            case RPNTokenType::MUL: {
                MultiInterval<T> b = stack.back(); stack.pop_back();
                stack.back() = stack.back() * b;
                break;
            }
            case RPNTokenType::DIV: {
                MultiInterval<T> b = stack.back(); stack.pop_back();
                stack.back() = stack.back() / b;
                break;
            }
            case RPNTokenType::POW: {
                MultiInterval<T> b = stack.back(); stack.pop_back();
                stack.back() = multi_pow(stack.back(), b);
                break;
            }
            case RPNTokenType::SIN: stack.back() = multi_sin(stack.back()); break;
            case RPNTokenType::COS: stack.back() = multi_cos(stack.back()); break;
            case RPNTokenType::TAN: stack.back() = multi_tan(stack.back()); break;
            case RPNTokenType::EXP: stack.back() = multi_exp(stack.back()); break;
            case RPNTokenType::LN:  stack.back() = multi_ln(stack.back()); break;
            case RPNTokenType::ABS: stack.back() = multi_abs(stack.back()); break;

            default: break;
        }
    }

    if (stack.empty()) return MultiInterval<T>();
    return stack.back();
}

#endif