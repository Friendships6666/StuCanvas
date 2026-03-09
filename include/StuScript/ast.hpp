#ifndef STUSCRIPT_AST_HPP
#define STUSCRIPT_AST_HPP

#include "lexer.hpp"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include <cstdint>
#include <cstring>

namespace StuScript {

// --- 1. 类型系统定义 ---
struct Type {
    enum Kind : uint8_t {
        Void,
        I8, U8, I16, U16, I32, U32, I64, U64, // 完整的整数类型
        F32, F64, Bool, Pointer, Struct, Alias, String,
        External, // 外置类型 (如 BigInt<256>)
        Unknown
    };
    Kind kind = Unknown;
    uint8_t pointer_level = 0;
    llvm::StringRef name = "";

    // 模板参数或附加参数 (如 BigInt<256> 中的 256)
    llvm::SmallVector<int64_t, 2> params;

    static Type getBasic(Kind k) { return Type{k, 0, "", {}}; }
    bool isPointer() const { return pointer_level > 0; }

    // 辅助判定函数
    bool isInteger() const { return kind >= I8 && kind <= U64; }
    bool isSignedInteger() const { return kind == I8 || kind == I16 || kind == I32 || kind == I64; }
    bool isUnsignedInteger() const { return kind == U8 || kind == U16 || kind == U32 || kind == U64; }
    bool isFloat() const { return kind == F32 || kind == F64; }
    bool isNumeric() const { return isInteger() || isFloat(); }

    unsigned getBitWidth() const {
        switch(kind) {
            case I8:  case U8:  return 8;
            case I16: case U16: return 16;
            case I32: case U32: return 32;
            case I64: case U64: return 64;
            case F32: return 32;
            case F64: return 64;
            default: return 0;
        }
    }

    bool operator==(const Type& other) const {
        if (kind != other.kind || pointer_level != other.pointer_level)
            return false;
        // 如果是结构体、别名或外置类型，才比较名称和参数
        if (kind == Struct || kind == Alias || kind == External) {
            if (name != other.name) return false;
            if (params.size() != other.params.size()) return false;
            for (size_t i = 0; i < params.size(); ++i) {
                if (params[i] != other.params[i]) return false;
            }
        }
        return true;
    }
    bool operator!=(const Type& other) const { return !(*this == other); }
};

// --- 2. AST 节点类型枚举 ---
enum class ASTKind : uint16_t {
    Literal, Variable, Binary, Unary, Call, Member, ArrayAccess,
    Block, If, While, For, Return, VarDecl,
    FnDecl, StructDecl, Break, Continue, Cast
};

// --- 3. 节点私有数据结构 ---
struct ASTNode;

struct IfBranch {
    ASTNode* condition; // nullptr 表示最后的 else 分支
    ASTNode* body;
};

struct IfData {
    llvm::ArrayRef<IfBranch> branches;
};

struct ForData {
    ASTNode* init;
    ASTNode* condition;
    ASTNode* update;
    ASTNode* body;
};

struct CastData {
    ASTNode* expr;
    Type target_type;
};

struct BinaryData {
    ASTNode *left, *right;
    TokenType op;
};

struct UnaryData {
    ASTNode* target;
    TokenType op;
};

struct CallData {
    ASTNode* callee;
    llvm::ArrayRef<ASTNode*> args;
};

struct MemberData {
    ASTNode* object;
    llvm::StringRef member_name;
    uint32_t index;
};

struct WhileData {
    ASTNode* condition;
    ASTNode* body;
};

struct VarDeclData {
    llvm::StringRef name;
    Type type_ann;
    ASTNode* init;
};

struct FieldData {
    llvm::StringRef name;
    Type type;
};

struct FnData {
    llvm::StringRef name;
    Type ret_type;
    llvm::ArrayRef<VarDeclData*> params;
    ASTNode* body;
};

struct StructData {
    llvm::StringRef name;
    llvm::ArrayRef<FieldData*> fields;
};

// --- 4. 统一的 AST 节点 ---
struct alignas(8) ASTNode {
    ASTKind kind;
    Token token;
    Type resolved_ty;
    bool is_lvalue = false;

    union DataUnion {
        BinaryData binary{};
        UnaryData unary;
        CallData call;
        MemberData member;
        IfData if_stmt;
        ForData for_stmt;
        WhileData while_stmt;
        VarDeclData var_decl;
        FnData fn_decl;
        StructData struct_decl;
        CastData cast;
        llvm::ArrayRef<ASTNode*> block;
        ASTNode* return_value;
        llvm::StringRef s_ref;

        DataUnion() { std::memset(this, 0, sizeof(DataUnion)); }
    } as;

};

// --- 5. AST 上下文 (内存池工厂) ---
class ASTContext {
    llvm::BumpPtrAllocator allocator;

public:
    template<typename T, typename... Args>
    T* alloc(Args&&... args) {
        return new (allocator.Allocate<T>()) T(std::forward<Args>(args)...);
    }

    template<typename T>
    llvm::ArrayRef<T> copyArray(const std::vector<T>& vec) {
        if (vec.empty()) return {};
        T* data = allocator.Allocate<T>(vec.size());
        std::uninitialized_copy(vec.begin(), vec.end(), data);
        return llvm::ArrayRef<T>(data, vec.size());
    }

    ASTNode* createBinary(Token op, ASTNode* L, ASTNode* R) {
        auto* n = alloc<ASTNode>(ASTKind::Binary, op);
        n->as.binary = {L, R, op.type};
        return n;
    }

    ASTNode* createLiteral(Token tok) {
        auto* n = alloc<ASTNode>(ASTKind::Literal, tok);
        n->as.s_ref = tok.lexeme;
        return n;
    }

    ASTNode* createVariable(Token tok) {
        auto* n = alloc<ASTNode>(ASTKind::Variable, tok);
        n->as.s_ref = tok.lexeme;
        return n;
    }

    ASTNode* createBlock(Token lbrace, const std::vector<ASTNode*>& stmts) {
        auto* n = alloc<ASTNode>(ASTKind::Block, lbrace);
        n->as.block = copyArray(stmts);
        return n;
    }

    ASTNode* createVarDecl(Token name, Type ty, ASTNode* init) {
        auto* n = alloc<ASTNode>(ASTKind::VarDecl, name);
        n->as.var_decl = {name.lexeme, ty, init};
        return n;
    }

    ASTNode* createFnDecl(Token name, Type ret, const std::vector<VarDeclData*>& params, ASTNode* body) {
        auto* n = alloc<ASTNode>(ASTKind::FnDecl, name);
        std::vector<VarDeclData*> arenaParams;
        for(auto* p : params) {
            auto* pAlloc = alloc<VarDeclData>(*p);
            arenaParams.push_back(pAlloc);
        }
        n->as.fn_decl = {name.lexeme, ret, copyArray(arenaParams), body};
        return n;
    }

    ASTNode* createReturn(Token retTok, ASTNode* val) {
        auto* n = alloc<ASTNode>(ASTKind::Return, retTok);
        n->as.return_value = val;
        return n;
    }

    ASTNode* createUnary(Token op, ASTNode* target) {
        auto* n = alloc<ASTNode>(ASTKind::Unary, op);
        n->as.unary = {target, op.type};
        return n;
    }
};

// --- 6. 修正后的静态访问者 ---
template <typename Derived>
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    void visit(ASTNode* node) {
        if (!node) return;
        switch (node->kind) {
            case ASTKind::Literal:      static_cast<Derived*>(this)->visitLiteral(node); break;
            case ASTKind::Variable:     static_cast<Derived*>(this)->visitVariable(node); break;
            case ASTKind::Binary:       static_cast<Derived*>(this)->visitBinary(node); break;
            case ASTKind::Unary:        static_cast<Derived*>(this)->visitUnary(node); break;
            case ASTKind::Call:         static_cast<Derived*>(this)->visitCall(node); break;
            case ASTKind::Member:       static_cast<Derived*>(this)->visitMember(node); break;
            case ASTKind::Block:        static_cast<Derived*>(this)->visitBlock(node); break;
            case ASTKind::If:           static_cast<Derived*>(this)->visitIf(node); break;
            case ASTKind::While:        static_cast<Derived*>(this)->visitWhile(node); break;
            case ASTKind::Return:       static_cast<Derived*>(this)->visitReturn(node); break;
            case ASTKind::For:          static_cast<Derived*>(this)->visitFor(node); break;
            case ASTKind::Break:        static_cast<Derived*>(this)->visitBreak(node); break;
            case ASTKind::Continue:     static_cast<Derived*>(this)->visitContinue(node); break;
            case ASTKind::Cast:         static_cast<Derived*>(this)->visitCast(node); break;
            case ASTKind::VarDecl:      static_cast<Derived*>(this)->visitVarDecl(node); break;
            case ASTKind::FnDecl:       static_cast<Derived*>(this)->visitFnDecl(node); break;
            case ASTKind::StructDecl:   static_cast<Derived*>(this)->visitStructDecl(node); break;
            case ASTKind::ArrayAccess:  static_cast<Derived*>(this)->visitArrayAccess(node); break;
            default: break;
        }
    }

    virtual void visitArrayAccess(ASTNode* n) {
        visit(n->as.binary.left);
        visit(n->as.binary.right);
    }

    virtual void visitCast(ASTNode* n) {
        visit(n->as.cast.expr);
    }

    virtual void visitBlock(ASTNode* n) {
        for (auto* item : n->as.block) visit(item);
    }

    virtual void visitFor(ASTNode* n) {
        if (n->as.for_stmt.init) visit(n->as.for_stmt.init);
        if (n->as.for_stmt.condition) visit(n->as.for_stmt.condition);
        if (n->as.for_stmt.update) visit(n->as.for_stmt.update);
        visit(n->as.for_stmt.body);
    }

    virtual void visitWhile(ASTNode* n) {
        visit(n->as.while_stmt.condition);
        visit(n->as.while_stmt.body);
    }

    virtual void visitBinary(ASTNode* n) {
        visit(n->as.binary.left);
        visit(n->as.binary.right);
    }

    virtual void visitUnary(ASTNode* n) { visit(n->as.unary.target); }

    virtual void visitReturn(ASTNode* n) {
        if(n->as.return_value) visit(n->as.return_value);
    }

    virtual void visitLiteral(ASTNode* n) {}
    virtual void visitVariable(ASTNode* n) {}
    virtual void visitCall(ASTNode* n) {
        visit(n->as.call.callee);
        for(auto* arg : n->as.call.args) visit(arg);
    }
    virtual void visitMember(ASTNode* n) { visit(n->as.member.object); }
    virtual void visitIf(ASTNode* n) {
        for (auto& branch : n->as.if_stmt.branches) {
            if (branch.condition) visit(branch.condition);
            visit(branch.body);
        }
    }
    virtual void visitVarDecl(ASTNode* n) {
        if (n->as.var_decl.init) visit(n->as.var_decl.init);
    }
    virtual void visitFnDecl(ASTNode* n) { visit(n->as.fn_decl.body); }
    virtual void visitStructDecl(ASTNode* n) {}
    virtual void visitBreak(ASTNode* n) {}
    virtual void visitContinue(ASTNode* n) {}
};

} // namespace StuScript

#endif