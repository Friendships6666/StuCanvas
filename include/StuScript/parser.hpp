#ifndef STUSCRIPT_PARSER_HPP
#define STUSCRIPT_PARSER_HPP

#include "lexer.hpp"
#include "ast.hpp"
#include "Diagnostic.hpp"
#include "llvm/ADT/SmallVector.h"
#include <vector>
#include <string>

namespace StuScript {

class Parser {
public:
    Parser(const llvm::SmallVectorImpl<Token>& tokens, ASTContext& ctx, DiagEngine& diag)
        : tokens_(tokens), ctx_(ctx), diag_(diag), cur_(0) {}

    // 程序入口：解析一系列顶级声明
    std::vector<ASTNode*> parseProgram() {
        std::vector<ASTNode*> decls;
        while (!isAtEnd()) {
            ASTNode* decl = parseDeclaration();
            if (decl) {
                decls.push_back(decl);
            } else {
                synchronize();
            }
        }
        return decls;
    }

private:
    const llvm::SmallVectorImpl<Token>& tokens_;
    ASTContext& ctx_;
    DiagEngine& diag_;
    uint32_t cur_;

    // =========================================================================
    // 1. Precedence & Binding Power (普兰特解析法核心表)
    // =========================================================================
    enum Precedence {
        PREC_NONE = 0,
        PREC_ASSIGN,      // =
        PREC_OR,          // ||
        PREC_AND,         // &&
        PREC_EQUALITY,    // == !=
        PREC_COMPARISON,  // < > <= >=
        PREC_TERM,        // + -
        PREC_FACTOR,      // * / %
        PREC_UNARY,       // ! - * & (前缀)
        PREC_CALL,        // . () [] .* as (后缀与连接)
        PREC_PRIMARY
    };

    int getBindingPower(TokenType type) {
        switch (type) {
            case TokenType::EQUAL:       return PREC_ASSIGN;
            case TokenType::OR_OR:       return PREC_OR;
            case TokenType::AND_AND:     return PREC_AND;
            case TokenType::EQ_EQ:
            case TokenType::NOT_EQ:      return PREC_EQUALITY;
            case TokenType::LT:
            case TokenType::GT:
            case TokenType::LT_EQ:
            case TokenType::GT_EQ:       return PREC_COMPARISON;
            case TokenType::PLUS:
            case TokenType::MINUS:       return PREC_TERM;
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:     return PREC_FACTOR;
            case TokenType::LPAREN:      // 函数调用 ()
            case TokenType::LBRACKET:    // 数组访问 []
            case TokenType::DOT:         // 成员访问 .
            case TokenType::DOT_STAR:    // 后缀 .*
            case TokenType::AS:          // 类型转换 as
                                         return PREC_CALL;
            default:                     return PREC_NONE;
        }
    }

    // =========================================================================
    // 2. Pratt Parser 核心递归逻辑
    // =========================================================================

    ASTNode* parseExpression() {
        return parsePrecedence(PREC_ASSIGN);
    }

    ASTNode* parsePrecedence(int precedence) {
        if (isAtEnd()) return nullptr;
        Token token = advance();

        // 执行 NUD (Null Denominator) - 处理前缀/字面量
        ASTNode* left = nud(token);
        if (!left) return nullptr;

        // 执行 LED (Left Denominator) - 处理中缀/后缀
        while (precedence <= getBindingPower(peek().type)) {
            Token op = advance();
            left = led(left, op);
            if (!left) return nullptr;
        }

        return left;
    }

    // 前缀处理器
    ASTNode* nud(Token token) {
        switch (token.type) {
            case TokenType::NUMBER:
            case TokenType::STRING:
                return ctx_.createLiteral(token);

            case TokenType::IDENTIFIER:
                return ctx_.createVariable(token);

            case TokenType::LPAREN: {
                ASTNode* expr = parseExpression();
                consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);
                return expr;
            }

            case TokenType::MINUS:
            case TokenType::STAR:      // 解引用 *ptr
            case TokenType::AMPERSAND: // 取地址 &var
            {
                ASTNode* operand = parsePrecedence(PREC_UNARY);
                return ctx_.createUnary(token, operand);
            }

            default:
                diag_.report(token.line, token.column, DiagCode::P_ExpectExpression, DiagLevel::Error, token.lexeme);
                return nullptr;
        }
    }

    // 中缀/后缀处理器
    ASTNode* led(ASTNode* left, Token op) {
        switch (op.type) {
            // 标准中缀二元运算 (左结合)
            case TokenType::PLUS: case TokenType::MINUS: case TokenType::STAR:
            case TokenType::SLASH: case TokenType::PERCENT: case TokenType::EQ_EQ:
            case TokenType::NOT_EQ: case TokenType::LT: case TokenType::GT:
            case TokenType::LT_EQ: case TokenType::GT_EQ: case TokenType::AND_AND:
            case TokenType::OR_OR:
            {
                ASTNode* right = parsePrecedence(getBindingPower(op.type) + 1);
                return ctx_.createBinary(op, left, right);
            }

            // 赋值 (右结合)
            case TokenType::EQUAL: {
                ASTNode* right = parsePrecedence(PREC_ASSIGN);
                return ctx_.createBinary(op, left, right);
            }

            // 函数调用 left(...)
            case TokenType::LPAREN: {
                std::vector<ASTNode*> args;
                if (!check(TokenType::RPAREN)) {
                    do {
                        ASTNode* arg = parseExpression();
                        if (arg) args.push_back(arg);
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);
                auto* call = ctx_.alloc<ASTNode>(ASTKind::Call, op);
                call->as.call = { left, ctx_.copyArray(args) };
                return call;
            }

            // 数组访问 left[idx]
            case TokenType::LBRACKET: {
                ASTNode* index = parseExpression();
                consume(TokenType::RBRACKET, DiagCode::P_UnexpectedToken);
                auto* arr = ctx_.alloc<ASTNode>(ASTKind::ArrayAccess, op);
                arr->as.binary = { left, index, TokenType::LBRACKET };
                return arr;
            }

            // 成员访问 left.member
            case TokenType::DOT: {
                consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
                Token memberTok = previous();
                auto* mem = ctx_.alloc<ASTNode>(ASTKind::Member, memberTok);
                mem->as.member = { left, memberTok.lexeme, 0 };
                return mem;
            }

            // 后缀解引用 left.*
            case TokenType::DOT_STAR: {
                return ctx_.createUnary(op, left);
            }

            // 类型转换 left as Type
            case TokenType::AS: {
                Type targetTy = parseType();
                auto* castNode = ctx_.alloc<ASTNode>(ASTKind::Cast, op);
                castNode->as.cast = { left, targetTy };
                return castNode;
            }

            default: return nullptr;
        }
    }

    // =========================================================================
    // 3. 类型与声明解析 (递归下降)
    // =========================================================================

    Type parseType() {
        Type t;
        while (match(TokenType::STAR)) t.pointer_level++;

        if (!consume(TokenType::IDENTIFIER, DiagCode::P_ExpectTypeName))
            return Type::getBasic(Type::Unknown);

        Token id = previous();
        t.name = id.lexeme;

        // 解析模板参数 (如 BigInt<256>)
        if (match(TokenType::LT)) {
            do {
                if (match(TokenType::NUMBER)) {
                    // 这里暂简处理，将字面量转为整数存储
                    t.params.push_back(std::stoll(previous().lexeme.str()));
                }
            } while (match(TokenType::COMMA));
            consume(TokenType::GT, DiagCode::P_UnexpectedToken);
        }

        // 基础类型映射
        if (t.name == "i8")  t.kind = Type::I8;
        else if (t.name == "u8")  t.kind = Type::U8;
        else if (t.name == "i16") t.kind = Type::I16;
        else if (t.name == "u16") t.kind = Type::U16;
        else if (t.name == "i32") t.kind = Type::I32;
        else if (t.name == "u32") t.kind = Type::U32;
        else if (t.name == "i64") t.kind = Type::I64;
        else if (t.name == "u64") t.kind = Type::U64;
        else if (t.name == "f32") t.kind = Type::F32;
        else if (t.name == "f64") t.kind = Type::F64;
        else if (t.name == "bool") t.kind = Type::Bool;
        else if (t.name == "void") t.kind = Type::Void;
        else if (t.name == "BigInt" || t.name == "BigFloat") t.kind = Type::External;
        else t.kind = Type::Struct;

        return t;
    }

    ASTNode* parseDeclaration() {
        if (match(TokenType::FN)) return parseFunction();
        if (match(TokenType::STRUCT)) return parseStruct();
        if (match(TokenType::LET)) return parseVarDecl(true);
        return nullptr;
    }

    ASTNode* parseFunction() {
        consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
        Token name = previous();
        consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken);

        std::vector<VarDeclData*> params;
        if (!check(TokenType::RPAREN)) {
            do {
                consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
                Token pName = previous();
                consume(TokenType::COLON, DiagCode::P_UnexpectedToken);
                Type pType = parseType();
                params.push_back(ctx_.alloc<VarDeclData>(pName.lexeme, pType, nullptr));
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);

        Type retType = Type::getBasic(Type::Void);
        if (match(TokenType::ARROW)) retType = parseType();

        ASTNode* body = parseBlock();
        return ctx_.createFnDecl(name, retType, params, body);
    }

    ASTNode* parseStruct() {
        consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
        Token name = previous();
        consume(TokenType::LBRACE, DiagCode::P_UnexpectedToken);

        std::vector<FieldData*> fields;
        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
            Token fName = previous();
            consume(TokenType::COLON, DiagCode::P_UnexpectedToken);
            Type fType = parseType();
            consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
            fields.push_back(ctx_.alloc<FieldData>(fName.lexeme, fType));
        }
        consume(TokenType::RBRACE, DiagCode::P_UnexpectedToken);
        auto* n = ctx_.alloc<ASTNode>(ASTKind::StructDecl, name);
        n->as.struct_decl = { name.lexeme, ctx_.copyArray(fields) };
        return n;
    }

    // =========================================================================
    // 4. 语句解析 (控制流)
    // =========================================================================

    ASTNode* parseStatement() {
        if (match(TokenType::IF)) return parseIf();
        if (match(TokenType::WHILE)) return parseWhile();
        if (match(TokenType::FOR)) return parseFor();
        if (match(TokenType::RETURN)) return parseReturn();
        if (check(TokenType::LBRACE)) return parseBlock();
        if (match(TokenType::BREAK)) return parseBreak();
        if (match(TokenType::CONTINUE)) return parseContinue();
        if (match(TokenType::LET)) return parseVarDecl(false);
        return parseExpressionStatement();
    }

    ASTNode* parseIf() {
        Token ifTok = previous();
        std::vector<IfBranch> branches;

        // 主 If
        consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken);
        ASTNode* cond = parseExpression();
        consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);
        ASTNode* body = parseStatement();
        branches.push_back({cond, body});

        // Elif
        while (match(TokenType::ELIF)) {
            consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken);
            ASTNode* elifCond = parseExpression();
            consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);
            ASTNode* elifBody = parseStatement();
            branches.push_back({elifCond, elifBody});
        }

        // Else
        if (match(TokenType::ELSE)) {
            branches.push_back({nullptr, parseStatement()});
        }

        auto* n = ctx_.alloc<ASTNode>(ASTKind::If, ifTok);
        n->as.if_stmt = { ctx_.copyArray<IfBranch>(branches) };
        return n;
    }

    ASTNode* parseFor() {
        Token forTok = previous();
        if (!consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken)) return nullptr;

        // 1. Init (初始化部分): 支持 let i = 0; 或 i = 0; 或直接一个分号 ;
        ASTNode* init = nullptr;
        if (!match(TokenType::SEMICOLON)) {
            if (match(TokenType::LET)) {
                init = parseVarDecl(false); // parseVarDecl 内部会处理结尾的分号
            } else {
                init = parseExpressionStatement(); // 内部会处理结尾的分号
            }
        }

        // 2. Condition (条件部分): i < count; 或直接一个分号 ;
        ASTNode* cond = nullptr;
        if (!match(TokenType::SEMICOLON)) {
            cond = parseExpression();
            if (!consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon)) return nullptr;
        }

        // 3. Update (步进部分): i = i + 1
        ASTNode* update = nullptr;
        if (!check(TokenType::RPAREN)) {
            update = parseExpression();
        }
        if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;

        // 4. Body (循环体)
        ASTNode* body = parseStatement();
        if (!body) return nullptr;

        // 核心修复：将解析出的 init, cond, update, body 存入 AST 节点的 for_stmt 结构
        auto* n = ctx_.alloc<ASTNode>(ASTKind::For, forTok);
        n->as.for_stmt = { init, cond, update, body };
        return n;
    }

    ASTNode* parseWhile() {
        Token whileTok = previous();
        consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken);
        ASTNode* cond = parseExpression();
        consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken);
        ASTNode* body = parseStatement();
        auto* n = ctx_.alloc<ASTNode>(ASTKind::While, whileTok);
        n->as.while_stmt = { cond, body };
        return n;
    }

    ASTNode* parseBlock() {
        Token open = peek();
        consume(TokenType::LBRACE, DiagCode::P_UnexpectedToken);
        std::vector<ASTNode*> stmts;
        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            ASTNode* s = parseStatement();
            if (s) stmts.push_back(s);
            else synchronize();
        }
        consume(TokenType::RBRACE, DiagCode::P_UnexpectedToken);
        return ctx_.createBlock(open, stmts);
    }

    ASTNode* parseVarDecl(bool isGlobal) {
        consume(TokenType::IDENTIFIER, DiagCode::P_UnexpectedToken);
        Token name = previous();
        Type ty = Type::getBasic(Type::Unknown);
        if (match(TokenType::COLON)) ty = parseType();
        ASTNode* init = nullptr;
        if (match(TokenType::EQUAL)) init = parseExpression();
        consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
        return ctx_.createVarDecl(name, ty, init);
    }

    ASTNode* parseExpressionStatement() {
        ASTNode* expr = parseExpression();
        if (!expr) return nullptr;
        consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
        return expr;
    }

    ASTNode* parseReturn() {
        Token t = previous();
        ASTNode* val = check(TokenType::SEMICOLON) ? nullptr : parseExpression();
        consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
        return ctx_.createReturn(t, val);
    }

    ASTNode* parseBreak() {
        Token t = previous();
        consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
        return ctx_.alloc<ASTNode>(ASTKind::Break, t);
    }

    ASTNode* parseContinue() {
        Token t = previous();
        consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon);
        return ctx_.alloc<ASTNode>(ASTKind::Continue, t);
    }

    // =========================================================================
    // 5. 基础工具
    // =========================================================================
    const Token& peek() const { return tokens_[cur_]; }
    const Token& previous() const { return tokens_[cur_ - 1]; }
    bool isAtEnd() const { return peek().type == TokenType::EOF_TOKEN; }
    const Token& advance() { if (!isAtEnd()) cur_++; return previous(); }
    bool check(TokenType type) const { return !isAtEnd() && peek().type == type; }
    bool match(TokenType type) { if (check(type)) { advance(); return true; } return false; }

    bool consume(TokenType type, DiagCode code) {
        if (check(type)) { advance(); return true; }
        diag_.report(peek().line, peek().column, code, DiagLevel::Error, peek().lexeme);
        return false;
    }

    void synchronize() {
        if (isAtEnd()) return;
        advance();
        while (!isAtEnd()) {
            if (previous().type == TokenType::SEMICOLON) return;
            switch (peek().type) {
                case TokenType::FN: case TokenType::LET: case TokenType::STRUCT:
                case TokenType::IF: case TokenType::WHILE: case TokenType::RETURN:
                case TokenType::FOR: return;
                default: break;
            }
            advance();
        }
    }
};

} // namespace StuScript

#endif