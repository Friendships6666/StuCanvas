#ifndef STUSCRIPT_SEMA_HPP
#define STUSCRIPT_SEMA_HPP

#include "ast.hpp"
#include "Diagnostic.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string_view>

namespace StuScript {

// --- 符号表条目 ---
struct Symbol {
    Type type;
    bool is_lvalue;
};

class Sema : public ASTVisitor<Sema> {
public:
    explicit Sema(ASTContext& ctx, DiagEngine& diag) : ctx_(ctx), diag_(diag) {
        // 确保全局作用域存在。SmallVector 预留 8 层嵌套深度在栈上
        scopes_.emplace_back();
    }

    // 核心入口：返回 bool 表示语义分析是否完全通过
    bool analyze(const llvm::SmallVectorImpl<ASTNode*>& program) {
        // 第一遍：预收集所有顶级定义，支持函数/结构体的相互引用
        for (auto* node : program) {
            if (node->kind == ASTKind::StructDecl) collectStruct(node);
            else if (node->kind == ASTKind::FnDecl) collectFunction(node);
        }

        // 第二遍：递归检查所有具体逻辑
        for (auto* node : program) {
            visit(node);
        }

        // 如果 DiagEngine 记录了 Error 级别的错误，分析即为失败
        return !diag_.hasError();
    }

private:
    ASTContext& ctx_;
    DiagEngine& diag_;

    // 作用域栈：使用 llvm::SmallVector 管理嵌套，使用 llvm::StringMap 优化字符串查找
    llvm::SmallVector<llvm::StringMap<Symbol>, 8> scopes_;

    // 结构体元数据注册表
    struct StructInfo {
        llvm::StringMap<uint32_t> fieldToIndex;
        llvm::SmallVector<Type, 4> fieldTypes;
    };
    llvm::StringMap<StructInfo> structRegistry_;

    // --- 内部辅助 ---

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    // RAII 作用域管理守卫
    struct ScopeGuard {
        Sema& s;
        ScopeGuard(Sema& sema) : s(sema) { s.pushScope(); }
        ~ScopeGuard() { s.popScope(); }
    };

    void defineSymbol(llvm::StringRef name, Type ty, bool is_lvalue) {
        if (scopes_.empty()) return;
        // StringMap 的插入效率极高
        scopes_.back().insert({name, Symbol{ty, is_lvalue}});
    }

    Symbol* resolveSymbol(llvm::StringRef name) {
        // 从当前作用域向全局作用域逆向查找
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[i].find(name);
            if (it != scopes_[i].end()) return &(it->second);
        }
        return nullptr;
    }

    // 基础类型名映射：确保多语言诊断时显示干净的类型名称
    std::string_view getTypeName(const Type& t) {
        if (!t.name.empty()) return std::string_view(t.name.data(), t.name.size());
        switch (t.kind) {
            case Type::I32:    return "i32";
            case Type::I64:    return "i64";
            case Type::F32:    return "f32";
            case Type::F64:    return "f64";
            case Type::Bool:   return "bool";
            case Type::Void:   return "void";
            case Type::String: return "string";
            default:           return "unknown";
        }
    }

    void collectStruct(ASTNode* n) {
        auto& data = n->as.struct_decl;
        StructInfo info;
        for (uint32_t i = 0; i < data.fields.size(); ++i) {
            info.fieldToIndex.insert({data.fields[i]->name, i});
            info.fieldTypes.push_back(data.fields[i]->type);
        }
        structRegistry_.insert({data.name, std::move(info)});
    }

    void collectFunction(ASTNode* n) {
        auto& data = n->as.fn_decl;
        // 函数名作为 RValue 存入全局作用域
        defineSymbol(data.name, data.ret_type, false);
    }

public:
    // --- 访问者实现 ---

    void visitVarDecl(ASTNode* n) {
        auto& data = n->as.var_decl;
        if (data.init) {
            visit(data.init);
            // 自动类型推导
            if (data.type_ann.kind == Type::Unknown) {
                data.type_ann = data.init->resolved_ty;
            }
            // 显式类型检查
            else if (data.init->resolved_ty.kind != Type::Unknown && data.type_ann != data.init->resolved_ty) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                             getTypeName(data.type_ann), getTypeName(data.init->resolved_ty));
            }
        }
        defineSymbol(data.name, data.type_ann, true);
        n->resolved_ty = data.type_ann;
    }

    void visitFnDecl(ASTNode* n) {
        auto& data = n->as.fn_decl;
        ScopeGuard guard(*this);
        for (auto* param : data.params) {
            defineSymbol(param->name, param->type_ann, true);
        }
        visit(data.body);
    }

    void visitVariable(ASTNode* n) {
        Symbol* sym = resolveSymbol(n->as.s_ref);
        if (!sym) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_UndefinedVariable, DiagLevel::Error, n->as.s_ref);
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }
        n->resolved_ty = sym->type;
        n->is_lvalue = sym->is_lvalue;
    }

    void visitBinary(ASTNode* n) {
        visit(n->as.binary.left);
        visit(n->as.binary.right);

        auto* L = n->as.binary.left;
        auto* R = n->as.binary.right;

        if (L->resolved_ty.kind == Type::Unknown || R->resolved_ty.kind == Type::Unknown) {
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }

        if (n->token.type == TokenType::EQUAL) {
            if (!L->is_lvalue) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_NotAnLValue, DiagLevel::Error);
            }
            if (L->resolved_ty != R->resolved_ty) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                             getTypeName(L->resolved_ty), getTypeName(R->resolved_ty));
            }
            n->resolved_ty = L->resolved_ty;
            n->is_lvalue = true;
        } else if (n->token.type == TokenType::LBRACKET) {
            if (L->resolved_ty.pointer_level == 0) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_InvalidDereference, DiagLevel::Error);
                n->resolved_ty = Type::getBasic(Type::Unknown);
            } else {
                n->resolved_ty = L->resolved_ty;
                n->resolved_ty.pointer_level--;
            }
            n->is_lvalue = true;
        } else {
            if (L->resolved_ty != R->resolved_ty) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                             getTypeName(L->resolved_ty), getTypeName(R->resolved_ty));
            }
            n->resolved_ty = L->resolved_ty;
            n->is_lvalue = false;
        }
    }

    void visitMember(ASTNode* n) {
        visit(n->as.member.object);
        Type objTy = n->as.member.object->resolved_ty;

        if (objTy.kind == Type::Unknown) return;

        if (objTy.kind != Type::Struct || objTy.pointer_level > 0) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_InvalidDereference, DiagLevel::Error);
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }

        auto it = structRegistry_.find(objTy.name);
        if (it == structRegistry_.end()) {
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }

        auto& info = it->second;
        auto fieldName = n->as.member.member_name;
        auto fieldIt = info.fieldToIndex.find(fieldName);

        if (fieldIt == info.fieldToIndex.end()) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_MemberNotFound, DiagLevel::Error,
                         objTy.name, fieldName);
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }

        n->as.member.index = fieldIt->second;
        n->resolved_ty = info.fieldTypes[n->as.member.index];
        n->is_lvalue = n->as.member.object->is_lvalue;
    }

    void visitUnary(ASTNode* n) {
        visit(n->as.unary.target);
        Type& targetTy = n->as.unary.target->resolved_ty;

        if (targetTy.kind == Type::Unknown) return;

        if (n->token.type == TokenType::STAR || n->token.type == TokenType::DOT_STAR) {
            if (targetTy.pointer_level == 0) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_InvalidDereference, DiagLevel::Error);
                n->resolved_ty = Type::getBasic(Type::Unknown);
            } else {
                n->resolved_ty = targetTy;
                n->resolved_ty.pointer_level--;
                n->is_lvalue = true;
            }
        } else if (n->token.type == TokenType::AMPERSAND) {
            if (!n->as.unary.target->is_lvalue) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_NotAnLValue, DiagLevel::Error);
            }
            n->resolved_ty = targetTy;
            n->resolved_ty.pointer_level++;
            n->is_lvalue = false;
        } else {
            n->resolved_ty = targetTy;
            n->is_lvalue = false;
        }
    }

    void visitBlock(ASTNode* n) {
        ScopeGuard guard(*this);
        for (auto* stmt : n->as.block) visit(stmt);
    }

    void visitLiteral(ASTNode* n) {
        if (n->token.type == TokenType::NUMBER) {
            if (n->token.lexeme.find('.') != llvm::StringRef::npos) {
                n->resolved_ty = Type::getBasic(Type::F32);
            } else {
                n->resolved_ty = Type::getBasic(Type::I32);
            }
        } else if (n->token.type == TokenType::STRING) {
            n->resolved_ty = Type::getBasic(Type::String);
        }
        n->is_lvalue = false;
    }

    void visitIf(ASTNode* n) {
        visit(n->as.if_stmt.condition);
        visit(n->as.if_stmt.then_stmt);
        if (n->as.if_stmt.else_stmt) visit(n->as.if_stmt.else_stmt);
    }

    void visitWhile(ASTNode* n) {
        visit(n->as.while_stmt.condition);
        visit(n->as.while_stmt.body);
    }

    void visitReturn(ASTNode* n) {
        if (n->as.return_value) visit(n->as.return_value);
    }

    void visitCall(ASTNode* n) {
        visit(n->as.call.callee);
        for (auto* arg : n->as.call.args) visit(arg);
        n->resolved_ty = n->as.call.callee->resolved_ty;
        n->is_lvalue = false;
    }
};

} // namespace StuScript

#endif