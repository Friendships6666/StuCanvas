#include "../include/CAS/RPN/SyntaxChecker.h"
#include "../include/graph/GeoGraph.h"
#include <vector>
#include <cctype>
#include <string>

namespace CAS::Parser {

namespace {
    enum class LocalArgType : uint8_t {
        ID_ONLY = 0, NUM_ONLY = 1, VAR_ONLY = 2, MIXED = 3, FULLY_MIXED = 4
    };

    enum class TokenType { ID, VAR, NUM, OP, EQ, LP, RP, COMMA };
    struct Token { TokenType type; std::string_view content; int pos; };

    struct LexResult {
        std::vector<Token> tokens;
        SyntaxCheckResult error;
    };

    LexResult tokenize(std::string_view expr) {
        std::vector<Token> tokens;
        int n = (int)expr.length();
        for (int i = 0; i < n; ) {
            char c = expr[i];
            if (std::isspace(c)) { i++; continue; }

            // ============================================================
            // 改动 A: 数字识别逻辑支持正号 (+) 开头
            // ============================================================
            // 判断当前字符是否为符号位 (+ 或 -)
            bool is_sign_char = (c == '-' || c == '+');

            // 判断上下文：是否处于可以作为数字符号的位置
            bool is_sign_context = (tokens.empty() || tokens.back().type == TokenType::OP ||
                                    tokens.back().type == TokenType::LP || tokens.back().type == TokenType::COMMA ||
                                    tokens.back().type == TokenType::EQ);

            bool is_signed_num = is_sign_char && is_sign_context;

            if (std::isdigit(c) || c == '.' || is_signed_num) {
                int start = i;
                if (is_signed_num) i++; // 跳过符号位

                // 处理孤立符号：如果 + 或 - 后面不是数字或小数点，它就是普通操作符
                if (is_signed_num && (i >= n || (!std::isdigit(expr[i]) && expr[i] != '.'))) {
                    tokens.push_back({TokenType::OP, expr.substr(start, 1), start});
                    continue;
                }

                bool dot_seen = false; int digits = 0;
                while (i < n && (std::isdigit(expr[i]) || expr[i] == '.')) {
                    if (expr[i] == '.') {
                        if (dot_seen) return {{}, {false, false, SyntaxErrorCode::ERR_NUMBER_FORMAT, "Multiple dots", i}};
                        dot_seen = true;
                    } else digits++;
                    i++;
                }

                if (dot_seen && digits == 0) return {{}, {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Isolated dot", start}};
                if (i < n && (std::isalpha(expr[i]) || expr[i] == '_'))
                    return {{}, {false, false, SyntaxErrorCode::ERR_NUMBER_FORMAT, "Invalid char after num", i}};

                tokens.push_back({TokenType::NUM, expr.substr(start, i - start), start});
                continue;
            }

            if (std::isalpha(c) || c == '_' || (uint8_t)c > 127) {
                int start = i;
                while (i < n && (std::isalnum(expr[i]) || expr[i] == '_' || (uint8_t)expr[i] > 127)) i++;
                std::string_view content = expr.substr(start, i - start);
                if (content.length() == 1 && (content == "x" || content == "y" || content == "t"))
                    tokens.push_back({TokenType::VAR, content, start});
                else
                    tokens.push_back({TokenType::ID, content, start});
                continue;
            }

            if (std::string_view("+-*/^").find(c) != std::string_view::npos) { tokens.push_back({TokenType::OP, expr.substr(i, 1), i}); i++; }
            else if (c == '=') { tokens.push_back({TokenType::EQ, expr.substr(i, 1), i}); i++; }
            else if (c == '(') { tokens.push_back({TokenType::LP, expr.substr(i, 1), i}); i++; }
            else if (c == ')') { tokens.push_back({TokenType::RP, expr.substr(i, 1), i}); i++; }
            else if (c == ',') { tokens.push_back({TokenType::COMMA, expr.substr(i, 1), i}); i++; }
            else return {{}, {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Unknown char", i}};
        }
        return {tokens, {true}};
    }

    struct ArgRange { std::vector<Token> tokens; };
    std::vector<ArgRange> get_args(const std::vector<Token>& tokens, int id_idx, int& out_rp_idx) {
        std::vector<ArgRange> args;
        int lp_idx = id_idx + 1;
        if (lp_idx >= (int)tokens.size() || tokens[lp_idx].type != TokenType::LP) { out_rp_idx = -1; return args; }
        int d = 0; int last_start = lp_idx + 1;
        for (int i = lp_idx; i < (int)tokens.size(); ++i) {
            if (tokens[i].type == TokenType::LP) d++;
            else if (tokens[i].type == TokenType::RP) {
                if (--d == 0) {
                    ArgRange range; for(int k=last_start; k < i; ++k) range.tokens.push_back(tokens[k]);
                    args.push_back(range); out_rp_idx = i; return args;
                }
            }
            else if (tokens[i].type == TokenType::COMMA && d == 1) {
                ArgRange range; for(int k=last_start; k < i; ++k) range.tokens.push_back(tokens[k]);
                args.push_back(range); last_start = i + 1;
            }
        }
        out_rp_idx = -1; return args;
    }

    bool is_name_illegal(std::string_view name, const GeometryGraph& graph, bool is_call) {
        for (auto const& [fn, m] : GeometryGraph::BUILT_IN_FUNCTIONS) {
            if (name.starts_with(fn)) { if (is_call && name == fn) return false; return true; }
        }
        for (auto const& [fn, m] : graph.dynamic_functions) {
            if (name.starts_with(fn)) { if (is_call && name == fn) return false; return true; }
        }
        return false;
    }
}

SyntaxCheckResult check_syntax(std::string_view expression, const GeometryGraph& graph) {
    auto lex = tokenize(expression);
    if (!lex.error.success) return lex.error;
    const auto& tokens = lex.tokens;
    if (tokens.empty()) return {false, false, SyntaxErrorCode::ERR_EMPTY_EXPRESSION, "Empty expression"};

    int eq_idx = -1; int eq_cnt = 0;
    for (int i = 0; i < (int)tokens.size(); ++i) if (tokens[i].type == TokenType::EQ) { eq_idx = i; eq_cnt++; }
    if (eq_cnt > 1) return {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Multiple assignments", tokens[eq_idx].pos};

    int depth = 0;
    std::vector<bool> ctx_stack;
    const FuncMeta* m_ptr = nullptr;
    int m_pos = -1;

    for (int i = 0; i < (int)tokens.size(); ++i) {
        const auto& t = tokens[i];

        if (i > 0) {
            const auto& prev = tokens[i-1];
            bool prev_is_operand = (prev.type == TokenType::NUM || prev.type == TokenType::ID || prev.type == TokenType::VAR || prev.type == TokenType::RP);
            bool curr_is_operand_start = (t.type == TokenType::NUM || t.type == TokenType::ID || t.type == TokenType::VAR || t.type == TokenType::LP);

            if (prev_is_operand && curr_is_operand_start) {
                if (!(prev.type == TokenType::ID && t.type == TokenType::LP)) {
                    return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing operator between operands", t.pos};
                }
            }
        }

        if (t.type == TokenType::LP) { depth++; ctx_stack.push_back(i > 0 && tokens[i-1].type == TokenType::ID); }
        else if (t.type == TokenType::RP) { if (--depth < 0) return {false, false, SyntaxErrorCode::ERR_UNBALANCED_PAREN, "Unexpected RP", t.pos}; ctx_stack.pop_back(); }
        else if (t.type == TokenType::COMMA) { if (depth == 0 || !ctx_stack.back()) return {false, false, SyntaxErrorCode::ERR_UNEXPECTED_COMMA, "Bad comma", t.pos}; }

        else if (t.type == TokenType::OP) {
            // ============================================================
            // 改动 B: 扩展一元判定，支持 + 作为一元正号
            // ============================================================
            bool is_unary_char = (t.content == "-" || t.content == "+");
            bool is_unary_pos = (i == 0 || tokens[i-1].type == TokenType::LP ||
                                 tokens[i-1].type == TokenType::COMMA ||
                                 tokens[i-1].type == TokenType::EQ ||
                                 tokens[i-1].type == TokenType::OP); // 允许接在 OP 后面 (如 x + + y)

            if (!is_unary_char || !is_unary_pos) {
                // 如果不是一元用法，那就是二元，必须有左操作数
                bool l_ok = (i > 0 && (tokens[i-1].type == TokenType::ID || tokens[i-1].type == TokenType::NUM || tokens[i-1].type == TokenType::VAR || tokens[i-1].type == TokenType::RP));
                if (!l_ok) return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing left op", t.pos};
            }

            // 右操作数检查：允许 ID, NUM, VAR, LP, 或者 **一元运算符**
            // 注意：这里我们允许右边是任意 OP，因为下一轮循环会检查右边那个 OP 是否符合一元位置规则
            // 例如 x + + y，在检查第一个 + 时，右边是 + (OP)，通过。
            // 下一轮检查第二个 +，发现它在 OP 之后，符合 is_unary_pos，于是作为一元正号，检查其右边是 y (VAR)，通过。
            bool r_ok = (i < (int)tokens.size() - 1 && (
                tokens[i+1].type == TokenType::ID ||
                tokens[i+1].type == TokenType::NUM ||
                tokens[i+1].type == TokenType::VAR ||
                tokens[i+1].type == TokenType::LP ||
                tokens[i+1].type == TokenType::OP // 允许操作符链 (如 x ^ -2 或 x + + y)
            ));
            if (!r_ok) return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing right op", t.pos};
        }

        if (t.type == TokenType::ID) {
            bool is_c = (i + 1 < (int)tokens.size() && tokens[i+1].type == TokenType::LP);
            if (is_name_illegal(t.content, graph, is_c)) return {false, false, SyntaxErrorCode::ERR_NAME_ILLEGAL, "Illegal prefix", t.pos};
            if (is_c) {
                const FuncMeta* meta = graph.FindFunction(t.content);
                int rp = -1; auto args = get_args(tokens, i, rp);
                for (const auto& a : args) if (a.tokens.empty()) return {false, false, SyntaxErrorCode::ERR_INVALID_FUNC_SYNTAX, "Empty arg", t.pos};
                if (meta) {
                    if (meta->is_macro) {
                        if (i != 0 || rp != (int)tokens.size()-1) return {false, false, SyntaxErrorCode::ERR_MACRO_VIOLATION, "Macro must be standalone", t.pos};
                        m_ptr = meta; m_pos = t.pos;
                    }
                    size_t c = args.size();
                    bool c_ok = (t.content == "Length") ? (c == 1 || c == 2) : (t.content == "Area") ? (c >= 2) : (meta->variadic ? c >= meta->pos_rules.size() : c == meta->pos_rules.size());
                    if (!c_ok) return {false, false, SyntaxErrorCode::ERR_WRONG_ARG_COUNT, "Arg count err", t.pos};
                    for (size_t k = 0; k < c; ++k) {
                        LocalArgType rule = (k < meta->pos_rules.size()) ? (LocalArgType)meta->pos_rules[k] : (meta->variadic ? (LocalArgType)meta->pos_rules.back() : LocalArgType::FULLY_MIXED);
                        if (args[k].tokens.size() > 1 && (rule == LocalArgType::ID_ONLY || rule == LocalArgType::NUM_ONLY || rule == LocalArgType::VAR_ONLY))
                            return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Complex forbidden", args[k].tokens[0].pos};
                        TokenType ft = args[k].tokens[0].type;
                        if (rule == LocalArgType::ID_ONLY && ft != TokenType::ID) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need ID", args[k].tokens[0].pos};
                        if (rule == LocalArgType::NUM_ONLY && ft != TokenType::NUM) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need Num", args[k].tokens[0].pos};
                        if (rule == LocalArgType::VAR_ONLY && ft != TokenType::VAR) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need x,y,t", args[k].tokens[0].pos};
                        if (rule == LocalArgType::MIXED && ft == TokenType::VAR) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "No var allowed", args[k].tokens[0].pos};
                    }
                } else if (eq_idx == -1 || i != 0 || rp != eq_idx - 1) return {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Unknown func outside LHS", t.pos};
            }
        }
    }

    if (depth != 0) return {false, false, SyntaxErrorCode::ERR_UNBALANCED_PAREN, "Unclosed parentheses", (int)expression.length()};

    if (eq_idx != -1) {
        bool lhs_v = false;
        if (eq_idx == 1 && (tokens[0].type == TokenType::ID || tokens[0].type == TokenType::VAR)) lhs_v = true;
        else if (eq_idx > 2 && tokens[0].type == TokenType::ID && tokens[1].type == TokenType::LP) {
            int drp = -1; get_args(tokens, 0, drp);
            if (drp == eq_idx - 1) lhs_v = true;
        }
        if (!lhs_v) return {false, false, SyntaxErrorCode::ERR_EMPTY_EQUAL_SIDE, "Invalid LHS structure", tokens[eq_idx].pos};
    }

    if (m_ptr && eq_idx != -1) return {false, false, SyntaxErrorCode::ERR_MACRO_VIOLATION, "Macro in assignment", m_pos};
    return {true, m_ptr != nullptr};
}
}