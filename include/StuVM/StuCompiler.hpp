#ifndef STUVM_COMPILER_HPP
#define STUVM_COMPILER_HPP

#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <iomanip>

// Clang & LLVM 核心头文件
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Frontend/FrontendOptions.h"

// LLVM 支持库与反汇编必需的头文件
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"

// LLD 链接器头文件
#include "lld/Common/Driver.h"

LLD_HAS_DRIVER(elf)

namespace StuVM {

// =========================================================================
// 指令语义结构体：用于虚拟机理解每一条指令的“意图”
// =========================================================================
struct SemanticOperand {
    enum Type { REG, IMM, INVALID };
    Type type;
    int64_t value;      // 寄存器编号或立即数值
    std::string name;   // 易读名称 (如 "x10", "v8")
};

struct InstructionSemantic {
    uint64_t address;
    std::string mnemonic;   // 助记符 (如 vadd.vv)
    unsigned opcode;        // LLVM 内部 Opcode ID
    std::vector<SemanticOperand> operands;
    
    // 指令分类属性 (来自 MCInstrDesc)
    bool isBranch;
    bool isLoad;
    bool isStore;
    bool isVector;          // 是否为向量指令
    bool hasSideEffects;
};

class StuCompiler {
public:
    StuCompiler() {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllDisassemblers();
    }

    // 编译 C++ 代码并链接
    bool compileAndLink(const std::string& cppCode, const std::string& outputElf = "stu_final.elf") {
        auto CI = std::make_unique<clang::CompilerInstance>();
        auto VFS = llvm::vfs::getRealFileSystem();
        CI->createDiagnostics(*VFS);

        std::vector<const char*> Args = {
            "-xc++", "-O3",
            "-mllvm", "-riscv-v-vector-bits-min=256",
            "-mllvm", "-riscv-v-vector-bits-max=256"
        };
        clang::CompilerInvocation::CreateFromArgs(CI->getInvocation(), Args, CI->getDiagnostics());

        auto& TargetOpts = CI->getTargetOpts();
        TargetOpts.Triple = "riscv64-unknown-linux-musl";
        TargetOpts.CPU = "generic-rv64";
        TargetOpts.ABI = "lp64d";
        TargetOpts.FeaturesAsWritten = {"+m", "+a", "+f", "+d", "+v", "+zvl256b", "-c"};


        if (!CI->createTarget()) return false;

        setupSysroot(CI->getHeaderSearchOpts());

        auto Buffer = llvm::MemoryBuffer::getMemBufferCopy(cppCode);
        CI->getPreprocessorOpts().addRemappedFile("input.cpp", Buffer.release());
        CI->getFrontendOpts().Inputs.push_back(clang::FrontendInputFile("input.cpp", clang::Language::CXX));
        CI->getFrontendOpts().OutputFile = "stu_exec.o";




        return linkBinary(outputElf);
    }

    // 核心：反汇编并输出语义结构体
    void analyzeElf(const std::string& elfPath) {
        auto BinaryOrErr = llvm::object::createBinary(elfPath);
        if (!BinaryOrErr) return;

        llvm::object::ObjectFile* Obj = llvm::cast<llvm::object::ObjectFile>(BinaryOrErr.get().getBinary());
        std::string TripleName = "riscv64-unknown-linux-musl";
        std::string Error;
        const llvm::Target* TheTarget = llvm::TargetRegistry::lookupTarget(TripleName, Error);

        auto MRI = std::unique_ptr<llvm::MCRegisterInfo>(TheTarget->createMCRegInfo(TripleName));
        llvm::MCTargetOptions MCOptions;
        auto MAI = std::unique_ptr<llvm::MCAsmInfo>(TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
        auto MCII = std::unique_ptr<llvm::MCInstrInfo>(TheTarget->createMCInstrInfo());
        auto STI = std::unique_ptr<llvm::MCSubtargetInfo>(TheTarget->createMCSubtargetInfo(TripleName, "generic-rv64", "+m,+a,+f,+d,+v,+zvl256b"));
        llvm::MCContext Ctx(llvm::Triple(TripleName), MAI.get(), MRI.get(), STI.get());
        auto DisAsm = std::unique_ptr<llvm::MCDisassembler>(TheTarget->createMCDisassembler(*STI, Ctx));
        auto IP = std::unique_ptr<llvm::MCInstPrinter>(TheTarget->createMCInstPrinter(llvm::Triple(TripleName), 0, *MAI, *MCII, *MRI));

        std::cout << "\n--- STUVM SEMANTIC ANALYSIS ---\n";

        for (const llvm::object::SectionRef& Section : Obj->sections()) {
            if (!Section.isText()) continue;
            
            llvm::StringRef Contents = Section.getContents().get();
            llvm::ArrayRef<uint8_t> Bytes(reinterpret_cast<const uint8_t*>(Contents.data()), Contents.size());
            uint64_t Address = Section.getAddress();
            uint64_t Size;

            for (uint64_t Index = 0; Index < Bytes.size(); Index += Size) {
                llvm::MCInst Inst;
                if (DisAsm->getInstruction(Inst, Size, Bytes.slice(Index), Address + Index, llvm::nulls()) 
                    == llvm::MCDisassembler::Success) {
                    
                    // 构造语义结构体
                    InstructionSemantic sem;
                    sem.address = Address + Index;
                    sem.opcode = Inst.getOpcode();
                    
                    const llvm::MCInstrDesc &Desc = MCII->get(Inst.getOpcode());
                    sem.mnemonic = MCII->getName(Inst.getOpcode()).str();
                    sem.isBranch = Desc.isBranch();
                    sem.isLoad = Desc.mayLoad();
                    sem.isStore = Desc.mayStore();
                    sem.hasSideEffects = Desc.hasUnmodeledSideEffects();
                    // 简单判断是否是向量指令 (RVV 的 Opcode 名字通常包含 'V' 或是在特定分类下)
                    sem.isVector = sem.mnemonic.find("V") == 0 || sem.mnemonic.find(".V") != std::string::npos;

                    // 解析操作数
                    for (unsigned i = 0; i < Inst.getNumOperands(); ++i) {
                        const llvm::MCOperand &Op = Inst.getOperand(i);
                        SemanticOperand sOp;
                        if (Op.isReg()) {
                            sOp.type = SemanticOperand::REG;
                            sOp.value = Op.getReg();
                            sOp.name = MRI->getName(Op.getReg());
                        } else if (Op.isImm()) {
                            sOp.type = SemanticOperand::IMM;
                            sOp.value = Op.getImm();
                            sOp.name = std::to_string(Op.getImm());
                        } else {
                            sOp.type = SemanticOperand::INVALID;
                        }
                        sem.operands.push_back(sOp);
                    }

                    // 打印精简语义
                    printSemantic(sem);
                } else {
                    Size = 4;
                }
            }
        }
    }

private:
    void setupSysroot(clang::HeaderSearchOptions& HSOpts) {
        HSOpts.Sysroot = "/opt/stuvm-sysroot";
        HSOpts.UseBuiltinIncludes = false;
        HSOpts.UseStandardSystemIncludes = false;
        HSOpts.UseStandardCXXIncludes = false;
        HSOpts.AddPath("/opt/stuvm-sysroot/usr/include/c++/v1", clang::frontend::System, false, true);
        HSOpts.AddPath("/usr/lib/clang/21/include", clang::frontend::System, false, true);
        HSOpts.AddPath("/opt/stuvm-sysroot/include", clang::frontend::System, false, true);
        HSOpts.AddPath("/opt/stuvm-sysroot/usr/include", clang::frontend::System, false, true);
    }

    bool linkBinary(const std::string& output) {
        std::vector<const char*> Args = {
            "ld.lld", "-static", "-o", output.c_str(), "--no-relax",
            "/opt/stuvm-sysroot/lib/crt1.o", "/opt/stuvm-sysroot/lib/crti.o",
            "stu_exec.o", "-L/opt/stuvm-sysroot/usr/lib", "-L/opt/stuvm-sysroot/lib",
            "-lc++", "-lc++abi", "-lunwind", "-lc",
            "/opt/stuvm-sysroot/usr/lib/linux/libclang_rt.builtins-riscv64.a",
            "/opt/stuvm-sysroot/lib/crtn.o", "--gc-sections"
        };
        return lld::elf::link(Args, llvm::outs(), llvm::errs(), false, false);
    }

    void printSemantic(const InstructionSemantic& sem) {
        // 只打印你关心的核心函数（如 main 或 stu_compute）周边的地址会更清晰
        // 这里为了演示，打印所有指令的语义摘要
        std::cout << "[0x" << std::hex << sem.address << "] " 
                  << std::left << std::setw(12) << sem.mnemonic;
        
        std::cout << " | Ops: ";
        for(auto &op : sem.operands) {
            if(op.type == SemanticOperand::REG) std::cout << "Reg(" << op.name << ") ";
            else if(op.type == SemanticOperand::IMM) std::cout << "Imm(" << op.value << ") ";
        }

        if(sem.isVector) std::cout << " [SIMD]";
        if(sem.isBranch) std::cout << " [JUMP]";
        std::cout << std::dec << "\n";
    }
};

} // namespace StuVM

#endif