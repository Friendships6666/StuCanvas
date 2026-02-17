#include "../include/CAS/RPN/SyntaxChecker.h"
// 必须确保此头文件包含 GeometryGraph 类的完整定义，而不仅仅是前向声明
#include "../include/graph/GeoGraph.h"

#include <vector>
#include <cctype>
#include <string>
#include <stack>
#include <iostream>

namespace CAS::Parser {

namespace {
    // =========================================================
    // 1. 类型与枚举定义 (必须放在最前面)
    // =========================================================

    // 参数规则枚举 (修复：确保此枚举在所有函数之前可见)
    enum class LocalArgType : uint8_t {
        ID_ONLY     = 0,
        NUM_ONLY    = 1,
        VAR_ONLY    = 2,
        MIXED       = 3,
        FULLY_MIXED = 4
    };

    enum class TokenType { ID, VAR_XY, VAR_T, NUM, OP, OP_CROSS, EQ, LP, RP, COMMA };

    struct Token {
        TokenType type;
        std::string_view content;
        int pos;
    };

    // 用于类型推导的中间状态
    enum class ValType { SCALAR, VECTOR, ANY, VOID };

    struct TypeInfo {
        ValType type;
        bool has_xy;
        int pos;
    };

    struct LexResult {
        std::vector<Token> tokens;
        SyntaxCheckResult error;
    };

    // =========================================================
    // 2. 辅助函数 (重命名以解决歧义)
    // =========================================================

    // 修复：重命名为 get_op_prec_internal 避免 ADL 歧义
    int get_op_prec_internal(TokenType t, std::string_view c) {
        if (t == TokenType::COMMA) return 1;
        if (t == TokenType::OP_CROSS) return 2; // ox
        if (c == "+" || c == "-") return 3;
        if (c == "*" || c == "/") return 4;
        if (c == "^") return 5;
        return 0;
    }

    // =========================================================
    // 3. 词法分析器
    // =========================================================
    LexResult tokenize(std::string_view expr) {
        std::vector<Token> tokens;
        int n = (int)expr.length();
        for (int i = 0; i < n; ) {
            char c = expr[i];
            if (std::isspace(c)) { i++; continue; }

            // A. 数字识别
            bool is_sign = (c == '-' || c == '+');
            bool is_ctx_start = (tokens.empty() || tokens.back().type == TokenType::OP ||
                                 tokens.back().type == TokenType::OP_CROSS ||
                                 tokens.back().type == TokenType::LP || tokens.back().type == TokenType::COMMA ||
                                 tokens.back().type == TokenType::EQ);

            if (std::isdigit(c) || c == '.' || (is_sign && is_ctx_start)) {
                int start = i;
                if (is_sign) i++;
                if (is_sign && (i >= n || (!std::isdigit(expr[i]) && expr[i] != '.'))) {
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

            // B. 标识符
            if (std::isalpha(c) || c == '_' || (uint8_t)c > 127) {
                int start = i;
                while (i < n && (std::isalnum(expr[i]) || expr[i] == '_' || (uint8_t)expr[i] > 127)) i++;
                std::string_view content = expr.substr(start, i - start);

                if (content == "ox") tokens.push_back({TokenType::OP_CROSS, content, start});
                else if (content == "x" || content == "y") tokens.push_back({TokenType::VAR_XY, content, start});
                else if (content == "t") tokens.push_back({TokenType::VAR_T, content, start});
                else tokens.push_back({TokenType::ID, content, start});
                continue;
            }

            // C. 符号
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

    // 命名检查 (修复：GeometryGraph类型不完整问题通常是因为头文件未包含，此处代码逻辑无误)
    bool is_name_illegal(std::string_view name, const GeometryGraph& graph, bool is_call) {
        if (is_call) return false;
        for (auto const& [fn, m] : GeometryGraph::BUILT_IN_FUNCTIONS) if (name == fn) return true;
        for (auto const& [fn, m] : graph.dynamic_functions) if (name == fn) return true;
        return false;
    }

    // =========================================================
    // 4. 类型推导器
    // =========================================================
    struct TypeChecker {
        std::stack<TypeInfo> val_stack;
        std::stack<Token> op_stack;
        const GeometryGraph& graph;
        SyntaxCheckResult error;

        TypeChecker(const GeometryGraph& g) : graph(g) { error.success = true; }

        void push_val(ValType t, bool xy, int pos) { val_stack.push({t, xy, pos}); }

        bool apply_op(const Token& op) {
            if (val_stack.empty()) return false;

            // 逗号
            if (op.type == TokenType::COMMA) {
                if (val_stack.size() < 2) return false;
                TypeInfo rhs = val_stack.top(); val_stack.pop();
                TypeInfo lhs = val_stack.top(); val_stack.pop();
                if (rhs.has_xy || lhs.has_xy) { error = {false, false, SyntaxErrorCode::ERR_VECTOR_RESTRICTION, "Vector component cannot contain x/y", op.pos}; return false; }
                push_val(ValType::VECTOR, false, op.pos);
                return true;
            }
            // 叉乘
            if (op.type == TokenType::OP_CROSS) {
                if (val_stack.size() < 2) return false;
                TypeInfo rhs = val_stack.top(); val_stack.pop();
                TypeInfo lhs = val_stack.top(); val_stack.pop();
                if (lhs.type == ValType::SCALAR || rhs.type == ValType::SCALAR) {
                    error = {false, false, SyntaxErrorCode::ERR_INVALID_CROSS_OP, "Cross product needs vectors", op.pos}; return false;
                }
                push_val(ValType::SCALAR, lhs.has_xy || rhs.has_xy, op.pos);
                return true;
            }
            // 基础运算
            if (val_stack.size() < 2) { // 单目
                if (op.content == "-" || op.content == "+") return true; // 类型不变
                return false;
            }
            TypeInfo rhs = val_stack.top(); val_stack.pop();
            TypeInfo lhs = val_stack.top(); val_stack.pop();
            bool c_xy = lhs.has_xy || rhs.has_xy;

            if (op.content == "+" || op.content == "-") {
                bool lv = (lhs.type == ValType::VECTOR); bool rv = (rhs.type == ValType::VECTOR);
                bool ls = (lhs.type == ValType::SCALAR); bool rs = (rhs.type == ValType::SCALAR);
                if ((ls && rv) || (lv && rs)) { error = {false, false, SyntaxErrorCode::ERR_TYPE_MISMATCH, "Scalar +/- Vector", op.pos}; return false; }
                push_val((lv||rv)?ValType::VECTOR:ValType::SCALAR, c_xy, op.pos);
            }
            else if (op.content == "*") {
                ValType rt = ValType::SCALAR;
                if (lhs.type == ValType::VECTOR && rhs.type == ValType::VECTOR) rt = ValType::SCALAR;
                else if (lhs.type == ValType::VECTOR || rhs.type == ValType::VECTOR) rt = ValType::VECTOR;
                push_val(rt, c_xy, op.pos);
            }
            else if (op.content == "/") {
                if (lhs.type == ValType::VECTOR || rhs.type == ValType::VECTOR) { error = {false, false, SyntaxErrorCode::ERR_TYPE_MISMATCH, "Div Vector", op.pos}; return false; }
                push_val(ValType::SCALAR, c_xy, op.pos);
            }
            else if (op.content == "^") {
                if (lhs.type == ValType::VECTOR || rhs.type == ValType::VECTOR) { error = {false, false, SyntaxErrorCode::ERR_TYPE_MISMATCH, "Pow Vector", op.pos}; return false; }
                push_val(ValType::SCALAR, c_xy, op.pos);
            }
            return true;
        }
    };

    // 递归类型推导
    struct RecursiveTypeCheck {
        const GeometryGraph& g;
        SyntaxCheckResult& res;

        TypeInfo eval(const std::vector<Token>& sub) {
            if (!res.success || sub.empty()) return {ValType::VOID, false, 0};
            TypeChecker tc(g);

            for (int i = 0; i < (int)sub.size(); ++i) {
                const auto& t = sub[i];
                if (t.type == TokenType::NUM || t.type == TokenType::VAR_T) tc.push_val(ValType::SCALAR, false, t.pos);
                else if (t.type == TokenType::VAR_XY) tc.push_val(ValType::SCALAR, true, t.pos);
                else if (t.type == TokenType::ID) {
                    if (i+1 < (int)sub.size() && sub[i+1].type == TokenType::LP) {
                        int rp = -1; int d = 0;
                        for(int k=i+1; k<(int)sub.size(); ++k) {
                            if(sub[k].type==TokenType::LP) d++; else if(sub[k].type==TokenType::RP) if(--d==0) {rp=k; break;}
                        }
                        tc.push_val(ValType::SCALAR, false, t.pos); i = rp;
                    } else tc.push_val(ValType::ANY, false, t.pos);
                }
                else if (t.type == TokenType::LP) {
                    int d = 0, rp = -1; bool has_comma = false;
                    for(int k=i; k<(int)sub.size(); ++k) {
                        if(sub[k].type==TokenType::LP) d++; else if(sub[k].type==TokenType::RP) if(--d==0) {rp=k; break;}
                        if(sub[k].type==TokenType::COMMA && d==1) has_comma = true;
                    }
                    if (has_comma) { // 向量构造
                        std::vector<Token> seg; int id = 0;
                        for(int k=i+1; k<rp; ++k) {
                            if(sub[k].type==TokenType::LP) id++; else if(sub[k].type==TokenType::RP) id--;
                            else if(sub[k].type==TokenType::COMMA && id==0) {
                                TypeInfo ti = eval(seg); if(!res.success) return ti;
                                if(ti.has_xy) { res = {false, false, SyntaxErrorCode::ERR_VECTOR_RESTRICTION, "Vector no xy", sub[k].pos}; return ti; }
                                seg.clear(); continue;
                            }
                            seg.push_back(sub[k]);
                        }
                        TypeInfo ti = eval(seg); if(!res.success) return ti;
                        if(ti.has_xy) { res = {false, false, SyntaxErrorCode::ERR_VECTOR_RESTRICTION, "Vector no xy", rp}; return ti; }
                        tc.push_val(ValType::VECTOR, false, t.pos);
                    } else { // 括号
                        std::vector<Token> inner(sub.begin()+i+1, sub.begin()+rp);
                        TypeInfo ti = eval(inner); if(!res.success) return ti;
                        tc.push_val(ti.type, ti.has_xy, t.pos);
                    }
                    i = rp;
                }
                else if (t.type == TokenType::OP || t.type == TokenType::OP_CROSS || t.type == TokenType::COMMA) {
                    while (!tc.op_stack.empty()) {
                        int curr = get_op_prec_internal(t.type, t.content); // 修复调用
                        int top = get_op_prec_internal(tc.op_stack.top().type, tc.op_stack.top().content); // 修复调用
                        if (top >= curr) { if(!tc.apply_op(tc.op_stack.top())) { res=tc.error; return {ValType::VOID, false, 0}; } tc.op_stack.pop(); }
                        else break;
                    }
                    tc.op_stack.push(t);
                }
            }
            while(!tc.op_stack.empty()) { if(!tc.apply_op(tc.op_stack.top())) { res=tc.error; return {ValType::VOID, false, 0}; } tc.op_stack.pop(); }
            return tc.val_stack.empty() ? TypeInfo{ValType::VOID, false, 0} : tc.val_stack.top();
        }
    };

} // namespace anonymous

// =========================================================
// 5. 主校验逻辑
// =========================================================
SyntaxCheckResult check_syntax(std::string_view expression, const GeometryGraph& graph) {
    auto lex = tokenize(expression);
    if (!lex.error.success) return lex.error;
    const auto& tokens = lex.tokens;
    if (tokens.empty()) return {false, false, SyntaxErrorCode::ERR_EMPTY_EXPRESSION, "Empty expression"};

    // LHS 检查
    int eq_idx = -1; int eq_cnt = 0;
    for (int i = 0; i < (int)tokens.size(); ++i) if (tokens[i].type == TokenType::EQ) { eq_idx = i; eq_cnt++; }
    if (eq_cnt > 1) return {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Multiple assignments", tokens[eq_idx].pos};

    int depth = 0; std::vector<bool> ctx_stack;
    const FuncMeta* m_ptr = nullptr; int m_pos = -1;

    for (int i = 0; i < (int)tokens.size(); ++i) {
        const auto& t = tokens[i];
        if (i > 0) {
            auto pt = tokens[i-1].type; auto ct = t.type;
            bool p_op = (pt==TokenType::NUM || pt==TokenType::ID || pt==TokenType::VAR_XY || pt==TokenType::VAR_T || pt==TokenType::RP);
            bool c_op = (ct==TokenType::NUM || ct==TokenType::ID || ct==TokenType::VAR_XY || ct==TokenType::VAR_T || ct==TokenType::LP);
            if (p_op && c_op) if (!(pt == TokenType::ID && ct == TokenType::LP)) return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing op", t.pos};
        }

        if (t.type == TokenType::LP) { depth++; ctx_stack.push_back(i > 0 && tokens[i-1].type == TokenType::ID); }
        else if (t.type == TokenType::RP) { if (--depth < 0) return {false, false, SyntaxErrorCode::ERR_UNBALANCED_PAREN, "Unexpected RP", t.pos}; ctx_stack.pop_back(); }
        else if (t.type == TokenType::COMMA) { if (depth == 0) return {false, false, SyntaxErrorCode::ERR_UNEXPECTED_COMMA, "Bad comma", t.pos}; }
        else if (t.type == TokenType::OP || t.type == TokenType::OP_CROSS) {
            bool is_u = (t.content == "-" || t.content == "+") && (i==0 || tokens[i-1].type==TokenType::LP || tokens[i-1].type==TokenType::COMMA || tokens[i-1].type==TokenType::EQ || tokens[i-1].type==TokenType::OP || tokens[i-1].type==TokenType::OP_CROSS);
            if (!is_u && (i==0 || (tokens[i-1].type!=TokenType::ID && tokens[i-1].type!=TokenType::NUM && tokens[i-1].type!=TokenType::VAR_XY && tokens[i-1].type!=TokenType::VAR_T && tokens[i-1].type!=TokenType::RP)))
                return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing left", t.pos};
            if (i==(int)tokens.size()-1 || (tokens[i+1].type!=TokenType::ID && tokens[i+1].type!=TokenType::NUM && tokens[i+1].type!=TokenType::VAR_XY && tokens[i+1].type!=TokenType::VAR_T && tokens[i+1].type!=TokenType::LP && tokens[i+1].type!=TokenType::OP && tokens[i+1].type!=TokenType::OP_CROSS))
                return {false, false, SyntaxErrorCode::ERR_MISSING_OPERAND, "Missing right", t.pos};
        }

        if (t.type == TokenType::ID) {
            bool is_c = (i+1 < (int)tokens.size() && tokens[i+1].type == TokenType::LP);
            if (is_name_illegal(t.content, graph, is_c)) return {false, false, SyntaxErrorCode::ERR_NAME_ILLEGAL, "Bad name", t.pos};
            if (is_c) {
                const FuncMeta* meta = graph.FindFunction(t.content);
                int rp=-1; auto args = get_args(tokens, i, rp);
                for(auto& a:args) if(a.tokens.empty()) return {false, false, SyntaxErrorCode::ERR_INVALID_FUNC_SYNTAX, "Empty arg", t.pos};
                if (meta) {
                    if (meta->is_macro) { if (i!=0 || rp!=(int)tokens.size()-1) return {false, true, SyntaxErrorCode::ERR_MACRO_VIOLATION, "Macro standalone", t.pos}; m_ptr = meta; m_pos = t.pos; }
                    size_t c = args.size();
                    bool c_ok = (t.content == "Length") ? (c == 1 || c == 2) : (t.content == "Area") ? (c >= 2) : (meta->variadic ? c >= meta->pos_rules.size() : c == meta->pos_rules.size());
                    if (!c_ok) return {false, false, SyntaxErrorCode::ERR_WRONG_ARG_COUNT, "Arg count err", t.pos};
                    for (size_t k = 0; k < c; ++k) {
                        LocalArgType rule = (k < meta->pos_rules.size()) ? (LocalArgType)meta->pos_rules[k] : (meta->variadic ? (LocalArgType)meta->pos_rules.back() : LocalArgType::FULLY_MIXED);
                        if (args[k].tokens.size() > 1 && (rule == LocalArgType::ID_ONLY || rule == LocalArgType::NUM_ONLY || rule == LocalArgType::VAR_ONLY)) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Complex forbidden", args[k].tokens[0].pos};
                        TokenType ft = args[k].tokens[0].type;
                        if (rule == LocalArgType::ID_ONLY && ft != TokenType::ID) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need ID", args[k].tokens[0].pos};
                        if (rule == LocalArgType::NUM_ONLY && ft != TokenType::NUM) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need Num", args[k].tokens[0].pos};
                        if (rule == LocalArgType::VAR_ONLY && (ft != TokenType::VAR_XY && ft != TokenType::VAR_T)) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "Need var", args[k].tokens[0].pos};
                        if (rule == LocalArgType::MIXED && (ft == TokenType::VAR_XY || ft == TokenType::VAR_T)) return {false, false, SyntaxErrorCode::ERR_INVALID_ARG_TYPE, "No var allowed", args[k].tokens[0].pos};
                    }
                } else if (eq_idx == -1 || i != 0 || rp != eq_idx - 1) return {false, false, SyntaxErrorCode::ERR_UNKNOWN_TOKEN, "Unknown func", t.pos};
            }
        }
    }
    if (depth != 0) return {false, false, SyntaxErrorCode::ERR_UNBALANCED_PAREN, "Unclosed", 0};
    if (eq_idx != -1) {
        bool v = false;
        if (eq_idx == 1 && (tokens[0].type == TokenType::ID || tokens[0].type == TokenType::VAR_XY || tokens[0].type == TokenType::VAR_T)) v = true;
        else if (eq_idx > 2 && tokens[0].type == TokenType::ID && tokens[1].type == TokenType::LP) { int drp = -1; get_args(tokens, 0, drp); if (drp == eq_idx - 1) v = true; }
        if (!v) return {false, false, SyntaxErrorCode::ERR_EMPTY_EQUAL_SIDE, "Invalid LHS", tokens[eq_idx].pos};
    }
    if (m_ptr && eq_idx != -1) return {false, false, SyntaxErrorCode::ERR_MACRO_VIOLATION, "Macro assign", m_pos};

    // Pass 2: Type Check
    int start = (eq_idx == -1) ? 0 : eq_idx + 1;
    if (start < (int)tokens.size()) {
        std::vector<Token> rhs(tokens.begin() + start, tokens.end());
        SyntaxCheckResult tr = {true};
        RecursiveTypeCheck rtc{graph, tr};
        rtc.eval(rhs);
        if (!tr.success) return tr;
    }

    return {true, m_ptr != nullptr};
}

} // namespace CAS::Parser