#ifndef STUSCRIPT_AST_HPP
#define STUSCRIPT_AST_HPP

#include "lexer.hpp"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include <cstdint>

namespace StuScript {

// --- 1. 类型系统定义 ---
struct Type {
    enum Kind : uint8_t { 
        Void, I32, I64, F32, F64, Bool, Pointer, Struct, Alias,String, Unknown
    };
    Kind kind = Unknown;
    uint8_t pointer_level = 0;
    llvm::StringRef name = "";

    static Type getBasic(Kind k) { return Type{k, 0, ""}; }
    bool isPointer() const { return pointer_level > 0; }
    // 增加比较操作符，用于类型检查
    bool operator==(const Type& other) const {
        if (kind != other.kind || pointer_level != other.pointer_level)
            return false;
        // 如果是结构体或别名，才比较名称
        if (kind == Struct || kind == Alias) {
            return name == other.name;
        }
        // 基础类型（I32, F32 等）只要 kind 一致即可
        return true;
    }
    bool operator!=(const Type& other) const { return !(*this == other); }

    // 辅助：是否是数值类型
    bool isNumeric() const {
        return (kind >= I32 && kind <= F64) && pointer_level == 0;
    }
};

// --- 2. AST 节点类型枚举 ---
enum class ASTKind : uint16_t {
    Literal, Variable, Binary, Unary, Call, Member, ArrayAccess,
    Block, If, While, Return, VarDecl,
    FnDecl, StructDecl
};

struct ASTNode;

// --- 3. 节点私有数据结构 ---
struct BinaryData { ASTNode *left, *right; TokenType op; };
struct UnaryData  { ASTNode *target; TokenType op; };
struct CallData   { ASTNode *callee; llvm::ArrayRef<ASTNode*> args; };
struct MemberData { ASTNode *object; llvm::StringRef member_name; uint32_t index; };
struct IfData     { ASTNode *condition; ASTNode *then_stmt; ASTNode *else_stmt; };
struct WhileData  { ASTNode *condition; ASTNode *body; };
struct VarDeclData{ llvm::StringRef name; Type type_ann; ASTNode *init; };
struct FieldData  { llvm::StringRef name; Type type; };
struct FnData     { llvm::StringRef name; Type ret_type; llvm::ArrayRef<VarDeclData*> params; ASTNode *body; };
struct StructData { llvm::StringRef name; llvm::ArrayRef<FieldData*> fields; };

// --- 4. 统一的 AST 节点 ---
struct alignas(8) ASTNode {
    ASTKind kind;
    Token token;
    Type resolved_ty;
    bool is_lvalue = false; // <--- 新增：标记该表达式是否可以取地址/赋值

    // 命名联合体以增强兼容性
    union DataUnion {
        BinaryData binary{};
        UnaryData unary;
        CallData call;
        MemberData member;
        IfData if_stmt;
        WhileData while_stmt;
        VarDeclData var_decl;
        FnData fn_decl;
        StructData struct_decl;
        llvm::ArrayRef<ASTNode*> block;
        ASTNode* return_value;
        llvm::StringRef s_ref;

        // 必须提供构造函数，因为内部包含带构造函数的类(StringRef/ArrayRef)
        DataUnion() { std::memset(this, 0, sizeof(DataUnion)); }
    } as;

    ASTNode(ASTKind k, Token t) : kind(k), token(t) {}
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

    // --- 修正后的工厂方法 ---
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
        // 需要将参数也拷贝到 Arena
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
    void visit(ASTNode* node) {
        if (!node) return;
        switch (node->kind) {
            case ASTKind::Literal:   static_cast<Derived*>(this)->visitLiteral(node); break;
            case ASTKind::Variable:  static_cast<Derived*>(this)->visitVariable(node); break;
            case ASTKind::Binary:    static_cast<Derived*>(this)->visitBinary(node); break;
            case ASTKind::Unary:     static_cast<Derived*>(this)->visitUnary(node); break;
            case ASTKind::Call:      static_cast<Derived*>(this)->visitCall(node); break;
            case ASTKind::Member:    static_cast<Derived*>(this)->visitMember(node); break;
            case ASTKind::Block:     static_cast<Derived*>(this)->visitBlock(node); break;
            case ASTKind::If:        static_cast<Derived*>(this)->visitIf(node); break;
            case ASTKind::While:     static_cast<Derived*>(this)->visitWhile(node); break;
            case ASTKind::Return:    static_cast<Derived*>(this)->visitReturn(node); break;
            case ASTKind::VarDecl:   static_cast<Derived*>(this)->visitVarDecl(node); break;
            case ASTKind::FnDecl:    static_cast<Derived*>(this)->visitFnDecl(node); break;
            case ASTKind::StructDecl: static_cast<Derived*>(this)->visitStructDecl(node); break;
            case ASTKind::ArrayAccess: static_cast<Derived*>(this)->visitArrayAccess(node); break;
            default: break;
        }
    }
    // 增加默认实现
    void visitArrayAccess(ASTNode* n) {
        visit(n->as.binary.left);
        visit(n->as.binary.right);
    }

    void visitBlock(ASTNode* n) {
        for (auto* item : n->as.block) visit(item);
    }

    void visitBinary(ASTNode* n) {
        visit(n->as.binary.left);
        visit(n->as.binary.right);
    }

    void visitUnary(ASTNode* n) { visit(n->as.unary.target); }

    void visitReturn(ASTNode* n) {
        if(n->as.return_value) visit(n->as.return_value);
    }

    void visitLiteral(ASTNode* n) {}
    void visitVariable(ASTNode* n) {}
    void visitCall(ASTNode* n) {}
    void visitMember(ASTNode* n) {}
    void visitIf(ASTNode* n) {}
    void visitWhile(ASTNode* n) {}
    void visitVarDecl(ASTNode* n) {}
    void visitFnDecl(ASTNode* n) {}
    void visitStructDecl(ASTNode* n) {}
};

} // namespace StuScript

#endif