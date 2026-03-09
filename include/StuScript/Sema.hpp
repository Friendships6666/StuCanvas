#ifndef STUSCRIPT_SEMA_HPP
#define STUSCRIPT_SEMA_HPP

#include "ast.hpp"
#include "Diagnostic.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string_view>

namespace StuScript {

struct Symbol {
    Type type;
    bool is_lvalue;
};

class Sema : public ASTVisitor<Sema> {
public:
    explicit Sema(ASTContext& ctx, DiagEngine& diag) : ctx_(ctx), diag_(diag) {
        scopes_.emplace_back();
    }

    bool analyze(const llvm::SmallVectorImpl<ASTNode*>& program) {
        for (auto* node : program) {
            if (node->kind == ASTKind::StructDecl) collectStruct(node);
            else if (node->kind == ASTKind::FnDecl) collectFunction(node);
        }
        for (auto* node : program) {
            visit(node);
        }
        return !diag_.hasError();
    }

private:
    ASTContext& ctx_;
    DiagEngine& diag_;
    llvm::SmallVector<llvm::StringMap<Symbol>, 8> scopes_;

    struct StructInfo {
        llvm::StringMap<uint32_t> fieldToIndex;
        llvm::SmallVector<Type, 4> fieldTypes;
    };
    llvm::StringMap<StructInfo> structRegistry_;

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    struct ScopeGuard {
        Sema& s;
        ScopeGuard(Sema& sema) : s(sema) { s.pushScope(); }
        ~ScopeGuard() { s.popScope(); }
    };

    void defineSymbol(llvm::StringRef name, Type ty, bool is_lvalue) {
        if (scopes_.empty()) return;
        scopes_.back().insert({name, Symbol{ty, is_lvalue}});
    }

    Symbol* resolveSymbol(llvm::StringRef name) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[i].find(name);
            if (it != scopes_[i].end()) return &(it->second);
        }
        return nullptr;
    }

    std::string_view getTypeName(const Type& t) {
        if (!t.name.empty()) return std::string_view(t.name.data(), t.name.size());
switch (t.kind) {
            case Type::I8:  return "i8";   case Type::U8:  return "u8";
            case Type::I16: return "i16";  case Type::U16: return "u16";
            case Type::I32: return "i32";  case Type::U32: return "u32";
            case Type::I64: return "i64";  case Type::U64: return "u64";
            case Type::F32: return "f32";  case Type::F64: return "f64";
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
        defineSymbol(data.name, data.ret_type, false);
    }

public:
    void visitVarDecl(ASTNode* n) {
        auto& data = n->as.var_decl;
        if (data.init) {
            visit(data.init);
            if (data.type_ann.kind == Type::Unknown) {
                data.type_ann = data.init->resolved_ty;
            }
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

    // --- 新增：处理 as 类型强转的语义分析 ---
    void visitCast(ASTNode* n) {
        auto* srcNode = n->as.cast.expr;
visit(srcNode); 

        Type srcTy = srcNode->resolved_ty;
        Type dstTy = n->as.cast.target_type;

        if (srcTy.kind == Type::Unknown) return;

        bool is_valid = false;
        // 1. 同类型转换或数值类型互转 (i8-u64, f32-f64)
        if (srcTy == dstTy || (srcTy.isNumeric() && dstTy.isNumeric())) {
            is_valid = true;
        }
        // 2. 指针之间的重解释 (Bitcast)
        else if (srcTy.isPointer() && dstTy.isPointer()) {
            is_valid = true;
        }
        // 3. 指针与 64 位整数互转 (用于 HPC 偏移计算)
        else if ((srcTy.isPointer() && dstTy.getBitWidth() == 64) ||
                 (srcTy.kind == Type::I64 && dstTy.isPointer()) ||
                 (srcTy.kind == Type::U64 && dstTy.isPointer())) {
            is_valid = true;
        }

        if (!is_valid) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                         getTypeName(srcTy), getTypeName(dstTy));
        }
        n->resolved_ty = dstTy;
        n->is_lvalue = false;
    }

    // 纯粹的二元运算，剥离了 ArrayAccess
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
        } else {
            if (L->resolved_ty != R->resolved_ty) {
diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                             getTypeName(L->resolved_ty), getTypeName(R->resolved_ty));
            }
            n->resolved_ty = L->resolved_ty;
            n->is_lvalue = false;
        }
    }

    // 专设的 ArrayAccess 检查逻辑
    void visitArrayAccess(ASTNode* n) {
        visit(n->as.binary.left);  // Array
visit(n->as.binary.right); // Index

        auto* L = n->as.binary.left;
        auto* R = n->as.binary.right;

        if (L->resolved_ty.kind == Type::Unknown || R->resolved_ty.kind == Type::Unknown) {
            n->resolved_ty = Type::getBasic(Type::Unknown);
            return;
        }

        // 数组下标在 HPC 中必须是整数类型
        if (!R->resolved_ty.isInteger()) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                         "integer index", getTypeName(R->resolved_ty));
        }
        if (L->resolved_ty.pointer_level == 0) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_InvalidDereference, DiagLevel::Error);
            n->resolved_ty = Type::getBasic(Type::Unknown);
        } else {
            // 完美推导：*f32 -> 降级为 f32
            n->resolved_ty = L->resolved_ty;
            n->resolved_ty.pointer_level--;
        }

        // 核心：标记为左值，Codegen 才会去 load 它！
        n->is_lvalue = true;
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
                n->resolved_ty = Type::getBasic(Type::Unknown);
            } else {
                n->resolved_ty = targetTy;
                n->resolved_ty.pointer_level++;
                n->is_lvalue = false;
            }
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
llvm::StringRef lexeme = n->token.lexeme;
            // 提取数值与后缀 (如 100u64)
            size_t suffix_pos = lexeme.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
            llvm::StringRef num_part = (suffix_pos == llvm::StringRef::npos) ? lexeme : lexeme.substr(0, suffix_pos);
            llvm::StringRef suffix = (suffix_pos == llvm::StringRef::npos) ? "" : lexeme.substr(suffix_pos);
            bool has_dot = num_part.find('.') != llvm::StringRef::npos;

            if (suffix.empty()) {
                n->resolved_ty = Type::getBasic(has_dot ? Type::F64 : Type::I32);
            } else {
                if (suffix == "f32")      n->resolved_ty = Type::getBasic(Type::F32);
                else if (suffix == "f64") n->resolved_ty = Type::getBasic(Type::F64);
                else if (suffix == "i8")  n->resolved_ty = Type::getBasic(Type::I8);
                else if (suffix == "u8")  n->resolved_ty = Type::getBasic(Type::U8);
                else if (suffix == "i16") n->resolved_ty = Type::getBasic(Type::I16);
                else if (suffix == "u16") n->resolved_ty = Type::getBasic(Type::U16);
                else if (suffix == "i32") n->resolved_ty = Type::getBasic(Type::I32);
                else if (suffix == "u32") n->resolved_ty = Type::getBasic(Type::U32);
                else if (suffix == "i64") n->resolved_ty = Type::getBasic(Type::I64);
                else if (suffix == "u64") n->resolved_ty = Type::getBasic(Type::U64);
                else {
                    diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error, "Valid numeric suffix", suffix);
                    n->resolved_ty = Type::getBasic(Type::Unknown);
                }
            }
        } else if (n->token.type == TokenType::STRING) {
            n->resolved_ty = Type::getBasic(Type::String);
        }
        n->is_lvalue = false;
    }

    void visitIf(ASTNode* n) {
        for (auto& branch : n->as.if_stmt.branches) {
            if (branch.condition) {
                visit(branch.condition);
                // 确保 condition 返回的是布尔型或数字型
                if (branch.condition->resolved_ty.kind == Type::Struct) {
                    diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                                 "bool/numeric", getTypeName(branch.condition->resolved_ty));
                }
            }
            visit(branch.body);
        }
    }

    void visitFor(ASTNode* n) {
        ScopeGuard guard(*this);
        if (n->as.for_stmt.init) visit(n->as.for_stmt.init);
        if (n->as.for_stmt.condition) {
            visit(n->as.for_stmt.condition);
            if (n->as.for_stmt.condition->resolved_ty.kind == Type::Struct) {
                diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                             "bool/numeric", getTypeName(n->as.for_stmt.condition->resolved_ty));
            }
        }
        if (n->as.for_stmt.update) visit(n->as.for_stmt.update);
        visit(n->as.for_stmt.body);
    }

    void visitWhile(ASTNode* n) {
        visit(n->as.while_stmt.condition);
        if (n->as.while_stmt.condition->resolved_ty.kind == Type::Struct) {
            diag_.report(n->token.line, n->token.column, DiagCode::S_TypeMismatch, DiagLevel::Error,
                         "bool/numeric", getTypeName(n->as.while_stmt.condition->resolved_ty));
        }
        visit(n->as.while_stmt.body);
    }

    void visitReturn(ASTNode* n) {
        if (n->as.return_value) visit(n->as.return_value);
    }

    void visitBreak(ASTNode* n) {}
    void visitContinue(ASTNode* n) {}

    void visitCall(ASTNode* n) {
        visit(n->as.call.callee);
        for (auto* arg : n->as.call.args) visit(arg);
        n->resolved_ty = n->as.call.callee->resolved_ty;
        n->is_lvalue = false;
    }
};

} // namespace StuScript

#endif