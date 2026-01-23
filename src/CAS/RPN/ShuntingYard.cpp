// --- 文件路径: src/CAS/RPN/ShuntingYard.cpp ---
#include "../include/CAS/RPN/ShuntingYard.h"
#include <stack>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>
#include <cctype>

namespace CAS::Parser {

namespace {
    // 优先级定义
    enum Precedence { LOWEST = 0, ADD_SUB = 2, MUL_DIV = 3, POW = 4, UNARY_NEG = 5, FUNC = 6 };

    struct Op {
        std::string name;
        Precedence prec;
        RPNTokenType type;
        bool is_func;
    };

    // 将字符映射到 RPN 运算符类型
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

    // 检查是否为内置数学函数 (大小写不敏感)
    bool try_get_math_builtin(std::string_view name, RPNTokenType& out_type) {
        static const std::unordered_map<std::string, RPNTokenType> builtin_map = {
            {"sin", RPNTokenType::SIN}, {"cos", RPNTokenType::COS}, {"tan", RPNTokenType::TAN},
            {"exp", RPNTokenType::EXP}, {"ln", RPNTokenType::LN},   {"abs", RPNTokenType::ABS},
            {"sqrt", RPNTokenType::SQRT}, {"sign", RPNTokenType::SIGN}
        };
        // 转为小写进行匹配
        std::string lowercase_name;
        lowercase_name.reserve(name.size());
        for (char c : name) lowercase_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        auto it = builtin_map.find(lowercase_name);
        if (it != builtin_map.end()) {
            out_type = it->second;
            return true;
        }
        return false;
    }

    // 检查是否为自定义黑箱几何函数 (大小写不敏感)
    CustomFunctionType identify_custom_func(std::string_view name) {
        std::string n;
        n.reserve(name.size());
        for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (n == "length")   return CustomFunctionType::LENGTH;
        if (n == "area")     return CustomFunctionType::AREA;
        if (n == "extractx") return CustomFunctionType::EXTRACT_VALUE_X;
        if (n == "extracty") return CustomFunctionType::EXTRACT_VALUE_Y;


        return CustomFunctionType::NONE;
    }

    // 安全提取括号内的参数
    bool parse_blackbox_args_safe(std::string_view expr, size_t& pos, std::vector<std::string>& out_args, std::string& err_out) {
        int paren_depth = 1;
        size_t start = pos;
        bool found_end = false;

        while (pos < expr.length()) {
            char c = expr[pos];
            if (c == '(') paren_depth++;
            else if (c == ')') {
                if (--paren_depth == 0) {
                    found_end = true;
                    break;
                }
            }
            pos++;
        }

        if (!found_end) {
            err_out = "Unclosed parenthesis in function";
            return false;
        }

        std::string_view all_args = expr.substr(start, pos - start);
        size_t s = 0, e = 0;
        while ((e = all_args.find(',', s)) != std::string_view::npos) {
            std::string arg(all_args.substr(s, e - s));
            size_t first = arg.find_first_not_of(" ");
            if (first != std::string::npos) {
                size_t last = arg.find_last_not_of(" ");
                out_args.push_back(arg.substr(first, last - first + 1));
            }
            s = e + 1;
        }

        std::string last(all_args.substr(s));
        size_t first = last.find_first_not_of(" ");
        if (first != std::string::npos) {
            size_t last_pos = last.find_last_not_of(" ");
            out_args.push_back(last.substr(first, last_pos - first + 1));
        }

        pos++; // 跳过 ')'
        return true;
    }

    // 验证自定义函数的参数合法性
    bool validate_custom_func_args_safe(CustomFunctionType type, const std::vector<std::string>& args, std::string& err_out) {
        if (type == CustomFunctionType::LENGTH) {
            if (args.size() != 2) {
                err_out = "Length(a,b) requires 2 args";
                return false;
            }
        } else if (type == CustomFunctionType::EXTRACT_VALUE_X || type == CustomFunctionType::EXTRACT_VALUE_Y) {
            if (args.size() != 1) {
                err_out = "Extract(a) requires 1 arg";
                return false;
            }
        }

        for (const auto& arg : args) {
            if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                err_out = "Expected object name, found number: " + arg;
                return false;
            }
        }
        return true;
    }
}

CompileResult compile_infix_to_rpn(std::string_view expression) {
    CompileResult result;
    result.success = true;
    std::stack<Op> op_stack;

    bool expect_operand = true;

    auto emit = [&](RPNTokenType type, double val = 0.0) {
        result.bytecode.push_back({type, val});
    };

    size_t i = 0;
    const size_t n = expression.length();

    while (i < n) {
        char c = expression[i];
        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

        // 1. 处理数字
        bool is_negative_num = (c == '-' && expect_operand && i + 1 < n && std::isdigit(static_cast<unsigned char>(expression[i+1])));

        if (std::isdigit(static_cast<unsigned char>(c)) || is_negative_num) {
            char* end_ptr;
            double val = std::strtod(&expression[i], &end_ptr);
            emit(RPNTokenType::PUSH_CONST, val);
            i += (end_ptr - &expression[i]);
            expect_operand = false;
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
                // 函数识别 (大小写不敏感)
                CustomFunctionType cf = identify_custom_func(word);
                if (cf != CustomFunctionType::NONE) {
                    emit(RPNTokenType::CUSTOM_FUNCTION, 0.0);

                    i = peek + 1;
                    std::vector<std::string> args;
                    if (!parse_blackbox_args_safe(expression, i, args, result.error_arg)) {
                        result.success = false; return result;
                    }
                    if (!validate_custom_func_args_safe(cf, args, result.error_arg)) {
                        result.success = false; return result;
                    }

                    result.binding_slots.push_back({result.bytecode.size() - 1, RPNBindingSlot::SlotType::FUNCTION, "", cf, args});
                    expect_operand = false;
                } else {
                    RPNTokenType bt;
                    if (try_get_math_builtin(word, bt)) {
                        op_stack.push({std::string(word), FUNC, bt, true});
                        i = peek + 1;
                        op_stack.push({"(", LOWEST, RPNTokenType::PUSH_CONST, false});
                        expect_operand = true;
                    } else {
                        result.success = false;
                        result.error_arg = "Unknown function: " + std::string(word);
                        return result;
                    }
                }
            } else {
                // 变量名引用 (保持原始大小写，敏感)
                emit(RPNTokenType::PUSH_CONST, 0.0);
                result.binding_slots.push_back({result.bytecode.size() - 1, RPNBindingSlot::SlotType::VARIABLE, std::string(word), CustomFunctionType::NONE, {}});
                expect_operand = false;
            }
        }
        // 3. 处理运算符
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            if (expect_operand) {
                result.success = false;
                result.error_arg = std::string(1, c);
                return result;
            }
            Precedence p = (c == '+' || c == '-') ? ADD_SUB : (c == '^' ? POW : MUL_DIV);
            while (!op_stack.empty() && op_stack.top().name != "(" && op_stack.top().prec >= p) {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            op_stack.push({std::string(1, c), p, get_operator_type(c), false});
            i++;
            expect_operand = true;
        }
        else if (c == '(') {
            op_stack.push({"(", LOWEST, RPNTokenType::PUSH_CONST, false});
            i++;
            expect_operand = true;
        }
        else if (c == ')') {
            bool found_open = false;
            while (!op_stack.empty()) {
                if (op_stack.top().name == "(") {
                    found_open = true;
                    break;
                }
                emit(op_stack.top().type);
                op_stack.pop();
            }
            if (!found_open) {
                result.success = false;
                result.error_arg = ")";
                return result;
            }
            op_stack.pop();

            if (!op_stack.empty() && op_stack.top().is_func) {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            i++;
            expect_operand = false;
        }
        else if (c == ',') {
            while (!op_stack.empty() && op_stack.top().name != "(") {
                emit(op_stack.top().type);
                op_stack.pop();
            }
            if (op_stack.empty()) {
                result.success = false;
                result.error_arg = ",";
                return result;
            }
            i++;
            expect_operand = true;
        }
        else {
            i++;
        }
    }

    while (!op_stack.empty()) {
        if (op_stack.top().name == "(") {
            result.success = false;
            result.error_arg = "(";
            return result;
        }
        emit(op_stack.top().type);
        op_stack.pop();
    }

    return result;
}

} // namespace CAS::Parser