// --- 文件路径: src/CAS/RPN/RPN.cpp ---

#include "../../../pch.h"
#include "../../../include/CAS/RPN/RPN.h"

AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string) {
    AlignedVector<RPNToken> tokens;
    std::stringstream ss(rpn_string);
    std::string token_str;
    while (ss >> token_str) {
        if (token_str == "x") tokens.push_back({RPNTokenType::PUSH_X});
        else if (token_str == "y") tokens.push_back({RPNTokenType::PUSH_Y});
        else if (token_str == "+") tokens.push_back({RPNTokenType::ADD});
        else if (token_str == "-") tokens.push_back({RPNTokenType::SUB});
        else if (token_str == "*") tokens.push_back({RPNTokenType::MUL});
        else if (token_str == "/") tokens.push_back({RPNTokenType::DIV});
        else if (token_str == "sin") tokens.push_back({RPNTokenType::SIN});
        else if (token_str == "cos") tokens.push_back({RPNTokenType::COS});
        else if (token_str == "exp") tokens.push_back({RPNTokenType::EXP});
        else if (token_str == "tan") tokens.push_back({RPNTokenType::TAN});
        else if (token_str == "pow") tokens.push_back({RPNTokenType::POW});
        else if (token_str == "sign") tokens.push_back({RPNTokenType::SIGN});
        else if (token_str == "abs") tokens.push_back({RPNTokenType::ABS});
        else if (token_str == "_t_") tokens.push_back({RPNTokenType::PUSH_T});
        else if (token_str == "safeln") tokens.push_back({RPNTokenType::SAFE_LN});
        else if (token_str == "ln") tokens.push_back({RPNTokenType::LN});
        else if (token_str == "safeexp") tokens.push_back({RPNTokenType::SAFE_EXP});
        else if (token_str == "check_ln") tokens.push_back({RPNTokenType::CHECK_LN});
        else {
            try { tokens.push_back({RPNTokenType::PUSH_CONST, std::stod(token_str)}); }
            catch (const std::invalid_argument&) { throw std::runtime_error("无效的RPN指令: " + token_str); }
        }
    }
    return tokens;
}