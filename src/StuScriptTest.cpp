#include <iostream>
#include <string>

// LLVM 核心
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Host.h"

// StuScript 组件
#include "StuScript/lexer.hpp"
#include "StuScript/ast.hpp"
#include "StuScript/parser.hpp"
#include "StuScript/Sema.hpp"
#include "StuScript/Diagnostic.hpp"
#include "StuScript/Codegen.hpp"

using namespace StuScript;

// 辅助：打印编译错误
void dumpDiagnostics(const DiagEngine& diag) {
    for (const auto& d : diag.getDiagnostics()) {
        printf("Error [%u:%u] (Code %u): %.*s %.*s\n",
               d.line, d.col, (uint32_t)d.code,
               (int)d.args[0].size(), d.args[0].data(),
               (int)d.args[1].size(), d.args[1].data());
    }
}

// 核心：运行 LLVM 优化流水线 (O3)
void runOptimizations(llvm::Module& M) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string Error;
    auto Triple = llvm::sys::getDefaultTargetTriple();
    auto Target = llvm::TargetRegistry::lookupTarget(Triple, Error);

    llvm::TargetOptions opt;
    auto CPU = "generic";
    auto Features = "";
    auto TM = Target->createTargetMachine(Triple, CPU, Features, opt, llvm::Reloc::PIC_);

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(TM);

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
    MPM.run(M, MAM);
}

int main() {
    // 综合测试源码：涵盖各种 as 类型转换与数组/指针交互
    std::string source = R"(
    struct Vector3 {
        x: f32;
        y: f32;
        z: f32;
    }

    // 综合测试函数
    fn process_data(data: *f32, count: i32) -> void {
        // 1. 测试指针强转：将 f32 指针重解释为 Vector3 指针 (HPC 中常见的 SOA 到 AOS 转换或流式读取)
        let vec_ptr = data as *Vector3;

        let high_prec_accum: f64 = 0.0;

        for (let i = 0; i < count; i = i + 1) {
            // 2. 测试整型扩展：i32 扩展为 i64 用于安全的 64 位数组寻址
            let idx64 = i as i64;

            // 3. 测试整型到浮点转换
            let offset_val = i as f32;

            // 4. 测试精度提升：f32 -> f64，用于防止累加丢失精度
            let current_val = data[idx64];
            let val64 = current_val as f64;

            high_prec_accum = high_prec_accum + val64 + (offset_val as f64);
        }

        // 5. 测试浮点截断 (f64 -> f32)
        // 假设将计算完毕的结果强制写回首地址
        data[0] = high_prec_accum as f32;

        // 6. 测试黑魔法内存清零：通过将 *f32 视为 *i32 快速写入位模式
        // 在实际 VM 或硬件中常用于 bitwise 操作
        let int_ptr = data as *i32;
        int_ptr[1] = 0; // 等价于 data[1] = 0.0，但绕过了浮点流水线
    }
)";

    // --- 1. 前端阶段 ---
    ASTContext astCtx;
    DiagEngine diag;

    Lexer lexer(source);
    llvm::SmallVector<Token, 128> tokens;
    lexer.scanAll(tokens);

    Parser parser(tokens, astCtx, diag);
    auto rawProgram = parser.parseProgram();
    llvm::SmallVector<ASTNode*, 32> program;
    for (auto* n : rawProgram) program.push_back(n);

    if (diag.hasError()) {
        std::cerr << "\n[!] Syntax Error detected!\n";
        dumpDiagnostics(diag);
        return 1;
    }

    Sema sema(astCtx, diag);
    if (!sema.analyze(program)) {
        std::cerr << "\n[!] Semantic Error detected!\n";
        dumpDiagnostics(diag);
        return 1;
    }

    // --- 2. IR 生成阶段 ---
    llvm::LLVMContext llvmCtx;
    llvm::Module myModule("StuScript_Cast_Test", llvmCtx);
    Codegen generator(llvmCtx, myModule, diag);

    if (!generator.generate(program)) {
        std::cerr << "\n[!] Codegen failed or Module verification failed!\n";
        return 1;
    }

    // std::cout << "\n=== [ 原始 LLVM IR ] ===\n";
    // myModule.print(llvm::outs(), nullptr);

    // --- 3. 优化阶段 ---
    runOptimizations(myModule);

    std::cout << "\n=== [ 优化后的 LLVM IR (O3) ] ===\n";
    // 你会在这里观察到：
    // 1. i32 as i64 变成了 sext (或因为循环归纳变量直接被 LLVM 优化为 64 位)
    // 2. as f64 变成了 fpext
    // 3. as *i32 和 as *Vector3 在 LLVM 15+ (Opaque Pointers) 时代会完全“消失”
    //    而在 load/store 时会直接体现为 store i32 0, ptr %...
    myModule.print(llvm::outs(), nullptr);

    return 0;
}