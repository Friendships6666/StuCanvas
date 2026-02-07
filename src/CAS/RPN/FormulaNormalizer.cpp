#include "../include/CAS/RPN/FormulaNormalizer.h"
#include <vector>
#include <cctype>
#include <string>

namespace CAS::Parser {

    // =========================================================
    // 1. 词法切分 (Tokenize) - 包含小数规范化
    // =========================================================
    std::vector<FormulaNormalizer::NormToken> FormulaNormalizer::Tokenize(std::string_view input, const GeometryGraph& graph) {
        std::vector<NormToken> tokens;
        size_t n = input.length();

        for (size_t i = 0; i < n; ) {
            char c = input[i];
            if (std::isspace(c)) { i++; continue; }

            // A. 数字识别与规范化 (.5 -> 0.5, 5. -> 5.0)
            if (std::isdigit(c) || c == '.') {
                size_t start = i;
                bool has_dot = (c == '.');
                i++;
                while (i < n && (std::isdigit(input[i]) || input[i] == '.')) {
                    if (input[i] == '.') has_dot = true;
                    i++;
                }

                std::string num_str(input.substr(start, i - start));

                // --- 小数规范化逻辑 ---
                if (num_str == ".") {
                    // 孤立的小数点交给 SyntaxChecker 报错，这里保留原始
                } else {
                    if (num_str.front() == '.') {
                        num_str = "0" + num_str; // .5 -> 0.5
                    }
                    if (num_str.back() == '.') {
                        num_str += "0";          // 5. -> 5.0
                    }
                }

                tokens.push_back({NormTokenType::NUM, num_str, false});
                continue;
            }

            // B. 标识符
            if (std::isalpha(c) || c == '_' || (uint8_t)c > 127) {
                size_t start = i;
                while (i < n && (std::isalnum(input[i]) || input[i] == '_' || (uint8_t)input[i] > 127)) i++;
                tokens.push_back({NormTokenType::ID, std::string(input.substr(start, i - start)), false});
                continue;
            }

            // C. 运算符
            NormTokenType type = NormTokenType::OP;
            if (c == '(') type = NormTokenType::LP;
            else if (c == ')') type = NormTokenType::RP;
            else if (c == ',') type = NormTokenType::COMMA;
            else if (c == '=') type = NormTokenType::EQ;

            bool is_unary = false;
            if (c == '+' || c == '-') {
                if (tokens.empty()) is_unary = true;
                else {
                    NormTokenType last = tokens.back().type;
                    if (last == NormTokenType::OP || last == NormTokenType::LP ||
                        last == NormTokenType::COMMA || last == NormTokenType::EQ) {
                        is_unary = true;
                    }
                }
            }

            tokens.push_back({type, std::string(1, c), is_unary});
            i++;
        }
        return tokens;
    }

    // =========================================================
    // 2. 符号折叠 (FoldSigns) - 逻辑保持不变
    // =========================================================
    std::vector<FormulaNormalizer::NormToken> FormulaNormalizer::FoldSigns(const std::vector<NormToken>& input_tokens) {
        std::vector<NormToken> result;

        for (size_t i = 0; i < input_tokens.size(); ++i) {
            const auto& t = input_tokens[i];

            if (t.type != NormTokenType::OP || (t.content != "+" && t.content != "-")) {
                result.push_back(t);
                continue;
            }

            bool context_is_unary = t.is_unary;
            int minus_count = 0;
            size_t j = i;

            while (j < input_tokens.size() && input_tokens[j].type == NormTokenType::OP &&
                  (input_tokens[j].content == "+" || input_tokens[j].content == "-")) {
                if (input_tokens[j].content == "-") minus_count++;
                j++;
            }

            bool is_negative = (minus_count % 2 != 0);
            i = j - 1;

            if (context_is_unary) {
                if (is_negative) result.push_back({NormTokenType::OP, "-", true});
            } else {
                result.push_back({NormTokenType::OP, is_negative ? "-" : "+", false});
            }
        }
        return result;
    }

    // =========================================================
    // 3. Normalize 入口
    // =========================================================
    std::string FormulaNormalizer::Normalize(std::string_view input, const GeometryGraph& graph) {
        auto raw_tokens = Tokenize(input, graph);
        auto folded_tokens = FoldSigns(raw_tokens);

        std::string res;
        res.reserve(input.length() + 2); // 预留多一点空间给补位的0
        for (const auto& t : folded_tokens) res += t.content;
        return res;
    }

} // namespace CAS::Parser