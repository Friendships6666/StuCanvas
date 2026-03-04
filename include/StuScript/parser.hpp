#ifndef STUSCRIPT_PARSER_HPP
#define STUSCRIPT_PARSER_HPP

#include "lexer.hpp"
#include "ast.hpp"
#include "Diagnostic.hpp"
#include "llvm/ADT/SmallVector.h"
#include <vector>

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
                // 如果解析失败，不抛异常，而是同步到安全位置
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

    // --- 1. 基础工具方法 ---
    const Token& peek() const { return tokens_[cur_]; }
    const Token& previous() const { return tokens_[cur_ - 1]; }
    bool isAtEnd() const { return peek().type == TokenType::EOF_TOKEN; }

    const Token& advance() {
        if (!isAtEnd()) cur_++;
        return previous();
    }

    bool check(TokenType type) const {
        if (isAtEnd()) return false;
        return peek().type == type;
    }

    bool match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    // 替代之前的 throw，记录错误码并返回失败状态
    bool consume(TokenType type, DiagCode code) {
        if (check(type)) {
            advance();
            return true;
        }
        // 报告错误：这里可以将当前的 lexeme 作为参数传出，方便多语言显示“在该位置发现错误”
        diag_.report(peek().line, peek().column, code, DiagLevel::Error, peek().lexeme);
        return false;
    }

    // 错误恢复：跳到下一个分号或声明关键字
    void synchronize() {
        if (isAtEnd()) return;
        advance();
        while (!isAtEnd()) {
            if (previous().type == TokenType::SEMICOLON) return;
            switch (peek().type) {
                case TokenType::FN:
                case TokenType::LET:
                case TokenType::STRUCT:
                case TokenType::IF:
                case TokenType::WHILE:
                case TokenType::RETURN:
                    return;
                default: break;
            }
            advance();
        }
    }

    // --- 2. 类型解析 (支持多级指针: **i32) ---
    Type parseType() {
        Type t;
        while (match(TokenType::STAR)) {
            t.pointer_level++;
        }

        if (!check(TokenType::IDENTIFIER)) {
            diag_.report(peek().line, peek().column, DiagCode::P_ExpectTypeName, DiagLevel::Error);
            return Type::getBasic(Type::Unknown);
        }

        Token id = advance();
        t.name = id.lexeme;

        // 基础类型映射
        if (t.name == "i32") t.kind = Type::I32;
        else if (t.name == "i64") t.kind = Type::I64;
        else if (t.name == "f32") t.kind = Type::F32;
        else if (t.name == "f64") t.kind = Type::F64;
        else if (t.name == "bool") t.kind = Type::Bool;
        else if (t.name == "void") t.kind = Type::Void;
        else t.kind = Type::Struct;

        return t;
    }

    // --- 3. 顶级声明解析 ---
    ASTNode* parseDeclaration() {
        if (match(TokenType::FN)) return parseFunction();
        if (match(TokenType::STRUCT)) return parseStruct();
        if (match(TokenType::LET)) return parseVarDecl(true);

        diag_.report(peek().line, peek().column, DiagCode::P_UnexpectedToken, DiagLevel::Error, peek().lexeme);
        return nullptr;
    }

    ASTNode* parseFunction() {
        if (!check(TokenType::IDENTIFIER)) {
            diag_.report(peek().line, peek().column, DiagCode::P_UnexpectedToken, DiagLevel::Error, peek().lexeme);
            return nullptr;
        }
        Token name = advance();

        if (!consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken)) return nullptr;

        std::vector<VarDeclData*> params;
        if (!check(TokenType::RPAREN)) {
            do {
                if (!check(TokenType::IDENTIFIER)) return nullptr;
                Token pName = advance();
                if (!consume(TokenType::COLON, DiagCode::P_UnexpectedToken)) return nullptr;
                Type pType = parseType();
                params.push_back(ctx_.alloc<VarDeclData>(pName.lexeme, pType, nullptr));
            } while (match(TokenType::COMMA));
        }
        if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;

        Type retType = Type::getBasic(Type::Void);
        if (match(TokenType::ARROW)) {
            retType = parseType();
        }

        ASTNode* body = parseBlock();
        if (!body) return nullptr;

        return ctx_.createFnDecl(name, retType, params, body);
    }

    ASTNode* parseStruct() {
        if (!check(TokenType::IDENTIFIER)) return nullptr;
        Token name = advance();

        if (!consume(TokenType::LBRACE, DiagCode::P_UnexpectedToken)) return nullptr;

        std::vector<FieldData*> fields;
        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            if (!check(TokenType::IDENTIFIER)) return nullptr;
            Token fName = advance();
            if (!consume(TokenType::COLON, DiagCode::P_UnexpectedToken)) return nullptr;
            Type fType = parseType();
            if (!consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon)) return nullptr;
            fields.push_back(ctx_.alloc<FieldData>(fName.lexeme, fType));
        }
        if (!consume(TokenType::RBRACE, DiagCode::P_UnexpectedToken)) return nullptr;

        auto* n = ctx_.alloc<ASTNode>(ASTKind::StructDecl, name);
        n->as.struct_decl = { name.lexeme, ctx_.copyArray(fields) };
        return n;
    }

    // --- 4. 语句解析 ---
    ASTNode* parseStatement() {
        if (match(TokenType::IF)) return parseIf();
        if (match(TokenType::WHILE)) return parseWhile();
        if (match(TokenType::RETURN)) return parseReturn();
        if (check(TokenType::LBRACE)) return parseBlock();
        if (match(TokenType::LET)) return parseVarDecl(false);
        return parseExpressionStatement();
    }

    ASTNode* parseBlock() {
        Token open = peek();
        if (!consume(TokenType::LBRACE, DiagCode::P_UnexpectedToken)) return nullptr;

        std::vector<ASTNode*> stmts;
        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            ASTNode* s = parseStatement();
            if (s) stmts.push_back(s);
            else synchronize(); // 语句内局部恢复
        }
        if (!consume(TokenType::RBRACE, DiagCode::P_UnexpectedToken)) return nullptr;
        return ctx_.createBlock(open, stmts);
    }

    ASTNode* parseVarDecl(bool isGlobal) {
        if (!check(TokenType::IDENTIFIER)) return nullptr;
        Token name = advance();

        Type ty = Type::getBasic(Type::Unknown);
        if (match(TokenType::COLON)) {
            ty = parseType();
        }

        ASTNode* init = nullptr;
        if (match(TokenType::EQUAL)) {
            init = parseExpression();
        }
        if (!consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon)) return nullptr;
        return ctx_.createVarDecl(name, ty, init);
    }

    ASTNode* parseReturn() {
        Token retTok = previous();
        ASTNode* val = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            val = parseExpression();
        }
        if (!consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon)) return nullptr;
        return ctx_.createReturn(retTok, val);
    }

    ASTNode* parseIf() {
        Token ifTok = previous();
        if (!consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken)) return nullptr;
        ASTNode* cond = parseExpression();
        if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;

        ASTNode* thenBranch = parseStatement();
        ASTNode* elseBranch = nullptr;
        if (match(TokenType::ELSE)) {
            elseBranch = parseStatement();
        }

        auto* n = ctx_.alloc<ASTNode>(ASTKind::If, ifTok);
        n->as.if_stmt = { cond, thenBranch, elseBranch };
        return n;
    }

    ASTNode* parseWhile() {
        Token whileTok = previous();
        if (!consume(TokenType::LPAREN, DiagCode::P_UnexpectedToken)) return nullptr;
        ASTNode* cond = parseExpression();
        if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;
        ASTNode* body = parseStatement();

        auto* n = ctx_.alloc<ASTNode>(ASTKind::While, whileTok);
        n->as.while_stmt = { cond, body };
        return n;
    }

    ASTNode* parseExpressionStatement() {
        ASTNode* expr = parseExpression();
        if (!expr) return nullptr;
        if (!consume(TokenType::SEMICOLON, DiagCode::P_MissingSemicolon)) return nullptr;
        return expr;
    }

    // --- 5. 表达式解析 (优先级爬升) ---
    ASTNode* parseExpression() {
        return parseBinary(0);
    }

    ASTNode* parseBinary(int min_prec) {
        ASTNode* left = parseUnary();
        if (!left) return nullptr;

        while (true) {
            Token op = peek();
            int prec = getPrecedence(op.type);
            if (prec < min_prec) break;

            advance();
            int next_min_prec = (op.type == TokenType::EQUAL) ? prec : prec + 1;
            ASTNode* right = parseBinary(next_min_prec);
            if (!right) return nullptr;
            left = ctx_.createBinary(op, left, right);
        }
        return left;
    }

    ASTNode* parseUnary() {
        if (check(TokenType::MINUS) || check(TokenType::STAR) ||
            check(TokenType::AMPERSAND) || check(TokenType::EQUAL)) {
            Token op = advance();
            ASTNode* target = parseUnary();
            if (!target) return nullptr;
            return ctx_.createUnary(op, target);
        }
        return parsePrimary();
    }

    ASTNode* parsePrimary() {
        ASTNode* node = nullptr;

        if (match(TokenType::NUMBER) || match(TokenType::STRING)) {
            node = ctx_.createLiteral(previous());
        } else if (match(TokenType::IDENTIFIER)) {
            node = ctx_.createVariable(previous());
        } else if (match(TokenType::LPAREN)) {
            node = parseExpression();
            if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;
        } else {
            diag_.report(peek().line, peek().column, DiagCode::P_ExpectExpression, DiagLevel::Error, peek().lexeme);
            return nullptr;
        }

        // 后缀循环
        while (true) {
            if (match(TokenType::LPAREN)) {
                std::vector<ASTNode*> args;
                if (!check(TokenType::RPAREN)) {
                    do {
                        ASTNode* arg = parseExpression();
                        if (arg) args.push_back(arg);
                    } while (match(TokenType::COMMA));
                }
                if (!consume(TokenType::RPAREN, DiagCode::P_UnexpectedToken)) return nullptr;
                auto* call = ctx_.alloc<ASTNode>(ASTKind::Call, previous());
                call->as.call = { node, ctx_.copyArray(args) };
                node = call;
            }
            else if (match(TokenType::DOT)) {
                if (!check(TokenType::IDENTIFIER)) return nullptr;
                Token member = advance();
                auto* mem = ctx_.alloc<ASTNode>(ASTKind::Member, member);
                mem->as.member = { node, member.lexeme, 0 };
                node = mem;
            }
            else if (match(TokenType::DOT_STAR)) {
                node = ctx_.createUnary(previous(), node);
            }
            else if (match(TokenType::LBRACKET)) {
                ASTNode* index = parseExpression();
                if (!consume(TokenType::RBRACKET, DiagCode::P_UnexpectedToken)) return nullptr;
                auto* arr = ctx_.alloc<ASTNode>(ASTKind::ArrayAccess, previous());
                arr->as.binary = { node, index, TokenType::LBRACKET };
                node = arr;
            }
            else break;
        }
        return node;
    }

    int getPrecedence(TokenType type) {
        switch (type) {
            case TokenType::EQUAL:  return 1;
            case TokenType::EQ_EQ:
            case TokenType::NOT_EQ: return 2;
            case TokenType::LT:
            case TokenType::LT_EQ:
            case TokenType::GT:
            case TokenType::GT_EQ:  return 3;
            case TokenType::PLUS:
            case TokenType::MINUS:  return 4;
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:return 5;
            default: return -1;
        }
    }
};

} // namespace StuScript

#endif