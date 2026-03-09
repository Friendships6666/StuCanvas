#ifndef STUSCRIPT_CODEGEN_HPP
#define STUSCRIPT_CODEGEN_HPP

#include "ast.hpp"
#include "Diagnostic.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"

namespace StuScript {

class Codegen : public ASTVisitor<Codegen> {
public:
    Codegen(llvm::LLVMContext& ctx, llvm::Module& mod, DiagEngine& diag)
        : context_(ctx), module_(mod), diag_(diag), builder_(ctx) {
        lastVal_ = nullptr;
    }

    bool generate(const llvm::SmallVectorImpl<ASTNode*>& program) {
        try {
            // Step 1: 预定义所有结构体
            for (auto* node : program) {
                if (node->kind == ASTKind::StructDecl) defineStructType(node);
            }
            // Step 2: 生成全局变量与函数
            for (auto* node : program) {
                if (node->kind == ASTKind::FnDecl) visit(node);
                else if (node->kind == ASTKind::VarDecl) generateGlobal(node);
            }

            // 调试：打印生成的原始 IR，便于排错
            llvm::errs() << "\n========== [ Debug: Generated LLVM IR ] ==========\n";
            module_.print(llvm::errs(), nullptr);
            llvm::errs() << "==================================================\n\n";

            // 严格校验
            if (llvm::verifyModule(module_, &llvm::errs())) {
                llvm::errs() << "[!] Module verification failed! See LLVM error messages above.\n";
                return false;
            }
            return true;

        } catch (...) {
            llvm::errs() << "[!] Exception caught during Codegen!\n";
            return false;
        }
    }

private:
    llvm::LLVMContext& context_;
    llvm::Module& module_;
    DiagEngine& diag_;
    llvm::IRBuilder<> builder_;

    llvm::StringMap<llvm::Value*> valueMap_;
    llvm::StringMap<llvm::StructType*> structTypes_; // 显式管理结构体类型
    llvm::Value* lastVal_;
    llvm::SmallVector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>, 8> loopStack_;

    void terminateBlock(llvm::BasicBlock* dest) {
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(dest);
        }
    }

    llvm::Type* mapType(const Type& t) {
        if (t.pointer_level > 0) return builder_.getPtrTy(); // Opaque Pointer
switch (t.kind) {
            case Type::I8:  case Type::U8:  return builder_.getInt8Ty();
            case Type::I16: case Type::U16: return builder_.getInt16Ty();
            case Type::I32: case Type::U32: return builder_.getInt32Ty();
            case Type::I64: case Type::U64: return builder_.getInt64Ty();
            case Type::F32: return builder_.getFloatTy();
            case Type::F64: return builder_.getDoubleTy();
            case Type::Bool: return builder_.getInt1Ty();
            case Type::Void: return builder_.getVoidTy();
            case Type::Struct: {
                auto it = structTypes_.find(t.name.str());
                if (it != structTypes_.end()) return it->second;
                return builder_.getInt32Ty();
            }
            default:           return builder_.getInt32Ty();
        }
    }

    llvm::Value* accessValue(ASTNode* n) {
        if (!n) return nullptr;
        lastVal_ = nullptr;
        visit(n);
        llvm::Value* val = lastVal_;
        if (n->is_lvalue && val) {
            uint32_t align = (n->resolved_ty.kind == Type::Struct) ? 16 : 4;
            return builder_.CreateAlignedLoad(mapType(n->resolved_ty), val, llvm::Align(align));
        }
        return val;
    }

    llvm::Value* getEffectiveAddress(ASTNode* n) {
        lastVal_ = nullptr;
        visit(n);
        llvm::Value* addr = lastVal_;
        if (!addr) return nullptr;

        // 核心修正：如果是保存在栈上的指针变量，Load 它以获取真正的数据基地址
        if (n->resolved_ty.pointer_level > 0 && n->is_lvalue) {
            return builder_.CreateLoad(builder_.getPtrTy(), addr, "base.ptr");
        }
        return addr;
    }

    void addLoopMetadata(llvm::Instruction* term) {
        llvm::Metadata* mds[] = {
            llvm::MDNode::get(context_, {}),
            llvm::MDNode::get(context_, {llvm::MDString::get(context_, "llvm.loop.vectorize.enable"),
                                         llvm::ConstantAsMetadata::get(builder_.getTrue())}),
            // 提示使用更宽的向量以压榨 SIMD 性能
            llvm::MDNode::get(context_, {llvm::MDString::get(context_, "llvm.loop.vectorize.width"),
                                         llvm::ConstantAsMetadata::get(builder_.getInt32(8))})
        };
        auto* node = llvm::MDNode::get(context_, mds);
        node->replaceOperandWith(0, node); // 自引用
        term->setMetadata(llvm::LLVMContext::MD_loop, node);
    }

    void defineStructType(ASTNode* n) {
        auto& data = n->as.struct_decl;
        auto* st = llvm::StructType::create(context_, data.name.str());
        structTypes_[data.name.str()] = st;

        llvm::SmallVector<llvm::Type*, 8> fields;
        for (auto* f : data.fields) fields.push_back(mapType(f->type));
        st->setBody(fields);
    }

    void generateGlobal(ASTNode* n) {
        auto& data = n->as.var_decl;
        auto* ty = mapType(data.type_ann);
        valueMap_[data.name.str()] = new llvm::GlobalVariable(module_, ty, false,
            llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(ty), data.name.str());
    }

public:
    void visitFnDecl(ASTNode* n) {
        auto& data = n->as.fn_decl;
        llvm::SmallVector<llvm::Type*, 8> argTys;
        for (auto* p : data.params) argTys.push_back(mapType(p->type_ann));
        auto* FT = llvm::FunctionType::get(mapType(data.ret_type), argTys, false);
        auto* F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, data.name.str(), module_);

        uint32_t idx = 0;
        for (auto& arg : F->args()) {
            arg.setName(data.params[idx]->name.str());
            // 注入 restrict (noalias) 以开启向量化
            if (data.params[idx]->type_ann.pointer_level > 0) arg.addAttr(llvm::Attribute::NoAlias);
            idx++;
        }

        auto* entry = llvm::BasicBlock::Create(context_, "entry", F);
        builder_.SetInsertPoint(entry);
        valueMap_.clear();
        for (auto& arg : F->args()) {
            auto* alloca = builder_.CreateAlloca(arg.getType(), nullptr, arg.getName());
            alloca->setAlignment(llvm::Align(8));
            builder_.CreateStore(&arg, alloca);
            valueMap_[arg.getName()] = alloca;
        }

        visit(data.body);

        if (!builder_.GetInsertBlock()->getTerminator()) {
            if (data.ret_type.kind == Type::Void) builder_.CreateRetVoid();
            else builder_.CreateUnreachable();
        }
    }

    void visitBinary(ASTNode* n) {
        if (n->token.type == TokenType::EQUAL) {
auto* R = accessValue(n->as.binary.right);
            lastVal_ = nullptr;
            visit(n->as.binary.left);
            auto* LAddr = lastVal_;

            if (LAddr != nullptr && R != nullptr) {
                uint32_t align = (R->getType()->isPointerTy()) ? 8 : 4;
                builder_.CreateAlignedStore(R, LAddr, llvm::Align(align));
            }
            lastVal_ = LAddr;
            return;
        }

        auto* L = accessValue(n->as.binary.left);
        auto* R = accessValue(n->as.binary.right);
        if (!L || !R) { lastVal_ = nullptr; return; }

        // 统一使用 Sema 推导出的逻辑类型
        Type lTy = n->as.binary.left->resolved_ty;
        bool isFP = lTy.isFloat();
        bool isSigned = lTy.isSignedInteger();

        switch (n->token.type) {
            case TokenType::PLUS:   lastVal_ = isFP ? builder_.CreateFAdd(L, R) : builder_.CreateAdd(L, R); break;
            case TokenType::MINUS:  lastVal_ = isFP ? builder_.CreateFSub(L, R) : builder_.CreateSub(L, R); break;
            case TokenType::STAR:   lastVal_ = isFP ? builder_.CreateFMul(L, R) : builder_.CreateMul(L, R); break;
            case TokenType::SLASH:  lastVal_ = isFP ? builder_.CreateFDiv(L, R) : (isSigned ? builder_.CreateSDiv(L, R) : builder_.CreateUDiv(L, R)); break;
            case TokenType::PERCENT: lastVal_ = isFP ? builder_.CreateFRem(L, R) : (isSigned ? builder_.CreateSRem(L, R) : builder_.CreateURem(L, R)); break;
            case TokenType::LT:     lastVal_ = isFP ? builder_.CreateFCmpOLT(L, R) : (isSigned ? builder_.CreateICmpSLT(L, R) : builder_.CreateICmpULT(L, R)); break;
            case TokenType::GT:     lastVal_ = isFP ? builder_.CreateFCmpOGT(L, R) : (isSigned ? builder_.CreateICmpSGT(L, R) : builder_.CreateICmpUGT(L, R)); break;
            case TokenType::LT_EQ:  lastVal_ = isFP ? builder_.CreateFCmpOLE(L, R) : (isSigned ? builder_.CreateICmpSLE(L, R) : builder_.CreateICmpULE(L, R)); break;
            case TokenType::GT_EQ:  lastVal_ = isFP ? builder_.CreateFCmpOGE(L, R) : (isSigned ? builder_.CreateICmpSGE(L, R) : builder_.CreateICmpUGE(L, R)); break;
            case TokenType::EQ_EQ:  lastVal_ = isFP ? builder_.CreateFCmpOEQ(L, R) : builder_.CreateICmpEQ(L, R); break;
            case TokenType::NOT_EQ: lastVal_ = isFP ? builder_.CreateFCmpONE(L, R) : builder_.CreateICmpNE(L, R); break;
            case TokenType::AND_AND: lastVal_ = builder_.CreateAnd(L, R); break;
            case TokenType::OR_OR:   lastVal_ = builder_.CreateOr(L, R); break;
            default: break;
        }
    }

    // =========================================================================
    // --- 新增：处理 Cast (as) 节点的 Codegen ---
    // =========================================================================
    void visitCast(ASTNode* n) {
        llvm::Value* srcVal = accessValue(n->as.cast.expr);
if (!srcVal) return;

        Type srcTy = n->as.cast.expr->resolved_ty;
        Type dstTy = n->as.cast.target_type;
        llvm::Type* llvmDstTy = mapType(dstTy);

        if (srcTy == dstTy) {
            lastVal_ = srcVal;
            return;
        }

        if (srcTy.isPointer() && dstTy.isPointer()) {
            lastVal_ = builder_.CreateBitCast(srcVal, llvmDstTy, "ptr_cast");
        }
        else if (srcTy.isPointer() && dstTy.kind == Type::I64) {
            lastVal_ = builder_.CreatePtrToInt(srcVal, llvmDstTy, "ptr2int");
        }
        else if (srcTy.kind == Type::I64 && dstTy.isPointer()) {
            lastVal_ = builder_.CreateIntToPtr(srcVal, llvmDstTy, "int2ptr");
        }
        else if (srcTy.isNumeric() && dstTy.isNumeric()) {
            bool srcIsFP = srcTy.isFloat();
            bool dstIsFP = dstTy.isFloat();

            if (!srcIsFP && !dstIsFP) {
                unsigned srcBits = srcTy.getBitWidth();
                unsigned dstBits = dstTy.getBitWidth();
                if (srcBits < dstBits) {
                    if (srcTy.isSignedInteger()) lastVal_ = builder_.CreateSExt(srcVal, llvmDstTy, "sext");
                    else lastVal_ = builder_.CreateZExt(srcVal, llvmDstTy, "zext");
                } else if (srcBits > dstBits) {
                    lastVal_ = builder_.CreateTrunc(srcVal, llvmDstTy, "trunc");
                } else {
                    lastVal_ = srcVal;
                }
            }
            else if (srcIsFP && dstIsFP) {
                if (srcTy.kind < dstTy.kind) lastVal_ = builder_.CreateFPExt(srcVal, llvmDstTy, "fpext");
                else lastVal_ = builder_.CreateFPTrunc(srcVal, llvmDstTy, "fptrunc");
            }
            else if (!srcIsFP && dstIsFP) {
                if (srcTy.isSignedInteger()) lastVal_ = builder_.CreateSIToFP(srcVal, llvmDstTy, "sitofp");
                else lastVal_ = builder_.CreateUIToFP(srcVal, llvmDstTy, "uitofp");
            }
            else if (srcIsFP && !dstIsFP) {
                if (dstTy.isSignedInteger()) lastVal_ = builder_.CreateFPToSI(srcVal, llvmDstTy, "fptosi");
                else lastVal_ = builder_.CreateFPToUI(srcVal, llvmDstTy, "fptoui");
            }
        }
        else {
            // Sema 层理应拦截掉其他情况，如果漏过来，则什么都不做
            lastVal_ = srcVal;
        }
    }

    void visitIf(ASTNode* n) {
        auto& branches = n->as.if_stmt.branches;
        auto* F = builder_.GetInsertBlock()->getParent();
        auto* mergeBB = llvm::BasicBlock::Create(context_, "if.end");

        for (const auto& br : branches) {
            if (br.condition) {
                auto* cond = accessValue(br.condition);
                if (!cond->getType()->isIntegerTy(1)) cond = builder_.CreateIsNotNull(cond);

                auto* thenBB = llvm::BasicBlock::Create(context_, "if.then", F);
                auto* nextCondBB = llvm::BasicBlock::Create(context_, "if.next");

                builder_.CreateCondBr(cond, thenBB, nextCondBB);

                builder_.SetInsertPoint(thenBB);
                visit(br.body);
                terminateBlock(mergeBB);

                nextCondBB->insertInto(F);
                builder_.SetInsertPoint(nextCondBB);
            } else {
                visit(br.body);
                break;
            }
        }
        terminateBlock(mergeBB);
        mergeBB->insertInto(F);
        builder_.SetInsertPoint(mergeBB);
    }

    void visitFor(ASTNode* n) {
        auto& data = n->as.for_stmt;
        auto* F = builder_.GetInsertBlock()->getParent();
        if (data.init) visit(data.init);

        auto* condBB = llvm::BasicBlock::Create(context_, "for.cond", F);
        auto* bodyBB = llvm::BasicBlock::Create(context_, "for.body", F);
        auto* stepBB = llvm::BasicBlock::Create(context_, "for.step", F);
        auto* endBB  = llvm::BasicBlock::Create(context_, "for.end", F);

        loopStack_.push_back({stepBB, endBB});
        builder_.CreateBr(condBB);
        builder_.SetInsertPoint(condBB);

        if (data.condition) {
            auto* cond = accessValue(data.condition);
            if (!cond->getType()->isIntegerTy(1)) cond = builder_.CreateIsNotNull(cond);
            builder_.CreateCondBr(cond, bodyBB, endBB);
        } else builder_.CreateBr(bodyBB);

        builder_.SetInsertPoint(bodyBB);
        visit(data.body);
        terminateBlock(stepBB);

        builder_.SetInsertPoint(stepBB);
        if (data.update) visit(data.update);

        auto* backBr = builder_.CreateBr(condBB);
        addLoopMetadata(backBr);

        builder_.SetInsertPoint(endBB);
        loopStack_.pop_back();
    }

    void visitWhile(ASTNode* n) {
        auto* F = builder_.GetInsertBlock()->getParent();
        auto* condBB = llvm::BasicBlock::Create(context_, "while.cond", F);
        auto* bodyBB = llvm::BasicBlock::Create(context_, "while.body", F);
        auto* endBB  = llvm::BasicBlock::Create(context_, "while.end", F);

        loopStack_.push_back({condBB, endBB});
        builder_.CreateBr(condBB);
        builder_.SetInsertPoint(condBB);

        auto* cond = accessValue(n->as.while_stmt.condition);
        if (!cond->getType()->isIntegerTy(1)) cond = builder_.CreateIsNotNull(cond);
        builder_.CreateCondBr(cond, bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        visit(n->as.while_stmt.body);
        auto* backBr = builder_.CreateBr(condBB);
        addLoopMetadata(backBr);

        builder_.SetInsertPoint(endBB);
        loopStack_.pop_back();
    }

    void visitMember(ASTNode* n) {
        visit(n->as.member.object);
        auto* ptr = lastVal_;
        if (!ptr) return;

        Type baseTy = n->as.member.object->resolved_ty;
        baseTy.pointer_level = 0;
        auto* structTy = mapType(baseTy);

        lastVal_ = builder_.CreateStructGEP(structTy, ptr, n->as.member.index, "member");
    }

    void visitArrayAccess(ASTNode* n) {
        auto* idx = accessValue(n->as.binary.right);
        auto* base = getEffectiveAddress(n->as.binary.left);
        if (!base || !idx) return;

        lastVal_ = builder_.CreateInBoundsGEP(mapType(n->resolved_ty), base, idx, "arrayidx");
    }

    void visitUnary(ASTNode* n) {
        if (n->token.type == TokenType::STAR || n->token.type == TokenType::DOT_STAR) {
            lastVal_ = accessValue(n->as.unary.target);
        } else if (n->token.type == TokenType::AMPERSAND) {
            // 遇到 '&' 不调用 accessValue，只单纯的走访 AST，使得 lastVal_ 保持为被访问元素的地址。
            lastVal_ = nullptr;
            visit(n->as.unary.target);
        } else if (n->token.type == TokenType::MINUS) {
            auto* v = accessValue(n->as.unary.target);
            lastVal_ = v->getType()->isFloatingPointTy() ? builder_.CreateFNeg(v) : builder_.CreateNeg(v);
        }
    }

    void visitVariable(ASTNode* n) {
        lastVal_ = valueMap_.lookup(n->as.s_ref.str());
    }

    void visitLiteral(ASTNode* n) {
        if (n->resolved_ty.kind == Type::F32) {
if (n->resolved_ty.isFloat()) {
            llvm::StringRef lexeme = n->token.lexeme;
            size_t suffix_pos = lexeme.find_first_of("fF");
            llvm::StringRef num_part = (suffix_pos == llvm::StringRef::npos) ? lexeme : lexeme.substr(0, suffix_pos);
            lastVal_ = llvm::ConstantFP::get(mapType(n->resolved_ty), num_part);
        } 
        else if (n->resolved_ty.isInteger()) {
            llvm::StringRef lexeme = n->token.lexeme;
            size_t suffix_pos = lexeme.find_first_of("iuIU");
            llvm::StringRef num_part = (suffix_pos == llvm::StringRef::npos) ? lexeme : lexeme.substr(0, suffix_pos);
            lastVal_ = llvm::ConstantInt::get(context_, llvm::APInt(n->resolved_ty.getBitWidth(), num_part, 10));
        }
        } else {
            lastVal_ = llvm::ConstantInt::get(context_, llvm::APInt(32, n->token.lexeme.str(), 10));
        }
    }

    void visitReturn(ASTNode* n) {
        if (n->as.return_value) builder_.CreateRet(accessValue(n->as.return_value));
        else builder_.CreateRetVoid();
    }

    void visitBreak(ASTNode*) { if (!loopStack_.empty()) builder_.CreateBr(loopStack_.back().second); }
    void visitContinue(ASTNode*) { if (!loopStack_.empty()) builder_.CreateBr(loopStack_.back().first); }
    void visitBlock(ASTNode* n) { for (auto* s : n->as.block) visit(s); }

    void visitVarDecl(ASTNode* n) {
        auto& data = n->as.var_decl;
        auto* F = builder_.GetInsertBlock()->getParent();
        auto* ty = mapType(data.type_ann);

        llvm::IRBuilder<> TmpB(&F->getEntryBlock(), F->getEntryBlock().begin());
        auto* alloca = TmpB.CreateAlloca(ty, nullptr, data.name.str());
        alloca->setAlignment(llvm::Align(16));

        if (data.init) builder_.CreateStore(accessValue(data.init), alloca);
        valueMap_[data.name.str()] = alloca;
    }
};

} // namespace StuScript

#endif