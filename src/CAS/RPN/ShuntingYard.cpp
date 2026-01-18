// --- 文件路径: src/CAS/RPN/ShuntingYard.cpp ---
#include "../include/CAS/RPN/ShuntingYard.h"
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>

namespace CAS::Parser {

namespace {
    // 优先级定义
    enum Precedence { LOWEST = 0, ADD_SUB = 2, MUL_DIV = 3, POW = 4, UNARY_NEG = 5, FUNC = 6 };

    struct Op { std::string name; Precedence prec; RPNTokenType type; bool is_func; };

    // 内部映射：操作符名到 RPN 类型
    RPNTokenType get_operator_type(char c) {
        switch (c) {
            case '+': return RPNTokenType::ADD;
            case '-': return RPNTokenType::SUB;
            case '*': return RPNTokenType::MUL;
            case '/': return RPNTokenType::DIV;
            case '^': return RPNTokenType::POW;
            default:  return RPNTokenType::PUSH_CONST;
        }
    }

    // 映射内置数学函数
    bool try_get_math_builtin(std::string_view name, RPNTokenType& out_type) {
        static const std::unordered_map<std::string_view, RPNTokenType> builtin_map = {
            {"sin", RPNTokenType::SIN}, {"cos", RPNTokenType::COS}, {"tan", RPNTokenType::TAN},
            {"exp", RPNTokenType::EXP}, {"ln", RPNTokenType::LN},   {"abs", RPNTokenType::ABS},
            {"sqrt", RPNTokenType::SQRT}, {"sign", RPNTokenType::SIGN}
        };
        std::string n(name);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        auto it = builtin_map.find(n);
        if (it != builtin_map.end()) {
            out_type = it->second;
            return true;
        }
        return false;
    }

    // 映射自定义黑箱函数
    CustomFunctionType identify_custom_func(std::string_view name) {
        std::string n(name);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n == "length")   return CustomFunctionType::LENGTH;
        if (n == "area")     return CustomFunctionType::AREA;
        if (n == "extractx") return CustomFunctionType::EXTRACT_VALUE_X;
        if (n == "extracty") return CustomFunctionType::EXTRACT_VALUE_Y;
        if (n == "distance") return CustomFunctionType::DISTANCE;
        return CustomFunctionType::NONE;
    }

    // 提取括号内的参数
    std::vector<std::string> parse_blackbox_args(std::string_view expr, size_t& pos) {
        std::vector<std::string> args;
        int paren_depth = 1;
        size_t start = pos;
        while (pos < expr.length() && paren_depth > 0) {
            char c = expr[pos];
            if (c == '(') paren_depth++;
            else if (c == ')') {
                paren_depth--;
                if (paren_depth == 0) break;
            }
            pos++;
        }
        std::string_view all_args = expr.substr(start, pos - start);
        size_t s = 0, e = 0;
        while ((e = all_args.find(',', s)) != std::string_view::npos) {
            std::string arg(all_args.substr(s, e - s));
            size_t first = arg.find_first_not_of(" ");
            if (first != std::string::npos) {
                size_t last = arg.find_last_not_of(" ");
                args.push_back(arg.substr(first, last - first + 1));
            }
            s = e + 1;
        }
        std::string last(all_args.substr(s));
        size_t first = last.find_first_not_of(" ");
        if (first != std::string::npos) {
            size_t last_pos = last.find_last_not_of(" ");
            args.push_back(last.substr(first, last_pos - first + 1));
        }
        pos++; // skip ')'
        return args;
    }

    void validate_custom_func_args(CustomFunctionType type, const std::vector<std::string>& args) {
        if (type == CustomFunctionType::LENGTH) {
            for (const auto& arg : args) {
                if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                    throw std::runtime_error("Semantic Error: 'Length' arguments must be object names, found numeric literal: " + arg);
                }
            }
        } else if (type == CustomFunctionType::EXTRACT_VALUE_X || type == CustomFunctionType::EXTRACT_VALUE_Y) {
            if (args.size() != 1) throw std::runtime_error("Semantic Error: ExtractX/ExtractY requires exactly 1 argument.");
            if (std::isdigit(static_cast<unsigned char>(args[0][0]))) throw std::runtime_error("Semantic Error: Extract arguments must be an object name.");
        }
    }
}

CompileResult compile_infix_to_rpn(std::string_view expression) {
    CompileResult result;
    std::stack<Op> op_stack;

    // 状态机：是否期待一个操作数。
    // 在表达式开头、左括号后、运算符后，我们都期待一个操作数。
    bool expect_operand = true;

    auto emit = [&](RPNTokenType type, double val = 0.0) {
        result.bytecode.push_back({type, val});
        return result.bytecode.size() - 1;
    };

    size_t i = 0;
    const size_t n = expression.length();

    while (i < n) {
        char c = expression[i];
        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

        // 1. 处理数字 (支持负号前缀)
        // 如果当前期待操作数，且遇到数字或以数字开头的负号，则让 strtod 整体解析
        bool is_negative_num = (c == '-' && expect_operand && i + 1 < n && std::isdigit(static_cast<unsigned char>(expression[i+1])));

        if (std::isdigit(static_cast<unsigned char>(c)) || is_negative_num) {
            char* end;
            double val = std::strtod(&expression[i], &end);
            emit(RPNTokenType::PUSH_CONST, val);
            i += (end - &expression[i]);
            expect_operand = false; // 拿到数字了，下一个应该是运算符
        }
        // 2. 处理单词
        else if (std::isalpha(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) > 127 || c == '_') {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(expression[i])) ||
                   static_cast<unsigned char>(expression[i]) > 127 || expression[i] == '_')) {
                i++;
            }
            std::string_view word = expression.substr(start, i - start);

            size_t peek = i;
            while (peek < n && std::isspace(static_cast<unsigned char>(expression[peek]))) peek++;

            if (peek < n && expression[peek] == '(') {
                CustomFunctionType cf = identify_custom_func(word);
                if (cf != CustomFunctionType::NONE) {
                    size_t r_idx = emit(RPNTokenType::CUSTOM_FUNCTION, 0.0);
                    i = peek + 1;
                    auto args = parse_blackbox_args(expression, i);
                    validate_custom_func_args(cf, args);
                    result.binding_slots.push_back({r_idx, RPNBindingSlot::SlotType::FUNCTION, "", cf, args});
                    expect_operand = false;
                } else {
                    RPNTokenType bt;
                    if (try_get_math_builtin(word, bt)) {
                        op_stack.push({std::string(word), FUNC, bt, true});
                        i = peek + 1;
                        op_stack.push({"(", LOWEST, RPNTokenType::PUSH_CONST, false});
                        expect_operand = true;
                    } else {
                        throw std::runtime_error("Unknown function: " + std::string(word));
                    }
                }
            } else {
                // 变量引用
                size_t r_idx = emit(RPNTokenType::PUSH_CONST, 0.0);
                result.binding_slots.push_back({r_idx, RPNBindingSlot::SlotType::VARIABLE, std::string(word), CustomFunctionType::NONE, {}});
                expect_operand = false;
            }
        }
        // 3. 处理运算符
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            // 此时 expect_operand 必定为 false (因为 is_negative_num 拦截了一元负号)
            // 如果在此处遇到运算符且 expect_operand 为真，说明语法错误 (例如 "1 + * 2")
            if (expect_operand) throw std::runtime_error("Unexpected operator at position " + std::to_string(i));

            Precedence p = (c == '+' || c == '-') ? ADD_SUB : (c == '^' ? POW : MUL_DIV);
            while (!op_stack.empty() && op_stack.top().name != "(" && op_stack.top().prec >= p) {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            op_stack.push({std::string(1, c), p, get_operator_type(c), false});
            i++;
            expect_operand = true; // 运算符后期待操作数
        }
        else if (c == '(') {
            op_stack.push({"(", LOWEST, RPNTokenType::PUSH_CONST, false});
            i++;
            expect_operand = true;
        }
        else if (c == ')') {
            while (!op_stack.empty() && op_stack.top().name != "(") {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            if (op_stack.empty()) throw std::runtime_error("Mismatched parentheses");
            op_stack.pop(); // pop "("

            if (!op_stack.empty() && op_stack.top().is_func) {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            i++;
            expect_operand = false;
        }
        else if (c == ',') { // 处理函数参数分隔符
            while (!op_stack.empty() && op_stack.top().name != "(") {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            i++;
            expect_operand = true;
        }
        else { i++; }
    }

    while (!op_stack.empty()) {
        if (op_stack.top().name == "(") throw std::runtime_error("Mismatched parentheses");
        emit(op_stack.top().type);
        op_stack.pop();
    }

    return result;
}

} // namespace CAS::Parser