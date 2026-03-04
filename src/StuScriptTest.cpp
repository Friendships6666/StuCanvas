#include <iostream>
#include <cstdio>
#include <string_view>

// 引入编译器各组件
#include "StuScript/lexer.hpp"
#include "StuScript/ast.hpp"
#include "StuScript/parser.hpp"
#include "StuScript/Sema.hpp"
#include "StuScript/Diagnostic.hpp"

using namespace StuScript;

// --- 1. 国际化展示层 (展示层才允许 I/O 操作) ---
enum class Lang { CN, EN };

void translateAndPrint(const Diagnostic& d, Lang lang) {
    // 高性能安全打印：处理非空结尾的 std::string_view
    auto safe_print = [](std::string_view sv) {
        if (sv.empty()) printf("?");
        else printf("%.*s", static_cast<int>(sv.size()), sv.data());
    };

    if (lang == Lang::CN) {
        printf("错误 [%u:%u] (代码 %u): ", d.line, d.col, static_cast<uint16_t>(d.code));
        switch (d.code) {
            case DiagCode::P_UnexpectedToken:
                printf("意外的符号 '"); safe_print(d.args[0]); printf("'"); break;
            case DiagCode::S_UndefinedVariable:
                printf("变量 '"); safe_print(d.args[0]); printf("' 未定义"); break;
            case DiagCode::S_TypeMismatch:
                printf("类型不匹配: 期望 '"); safe_print(d.args[0]);
                printf("', 实际为 '"); safe_print(d.args[1]); printf("'"); break;
            case DiagCode::S_MemberNotFound:
                printf("结构体 '"); safe_print(d.args[0]);
                printf("' 中找不到成员 '"); safe_print(d.args[1]); printf("'"); break;
            case DiagCode::S_NotAnLValue:
                printf("赋值错误: 左侧必须是一个可写入的变量"); break;
            case DiagCode::S_InvalidDereference:
                printf("无法解引用: 对象不是指针类型"); break;
            default: printf("语义解析阶段发现异常"); break;
        }
    } else {
        printf("Error [%u:%u] (Code %u): ", d.line, d.col, static_cast<uint16_t>(d.code));
        // ... 此处可扩展英文、日文等 10 种语言分支
        printf("General compilation error.");
    }
    printf("\n");
}

// --- 2. AST 打印机 (用于验证 Sema 注入的元数据) ---
class MetadataPrinter : public ASTVisitor<MetadataPrinter> {
public:
    void visitVarDecl(ASTNode* n) {
        printf("  [VarDecl] %.*s (Type: ", (int)n->as.var_decl.name.size(), n->as.var_decl.name.data());
        printType(n->resolved_ty);
        printf(", %s)\n", n->is_lvalue ? "LValue" : "RValue");
    }

    void visitMember(ASTNode* n) {
        printf("    [MemberAccess] .%.*s (GEP_INDEX: %u)\n",
               (int)n->as.member.member_name.size(), n->as.member.member_name.data(),
               n->as.member.index);
        visit(n->as.member.object);
    }

    void visitFnDecl(ASTNode* n) {
        printf("[Function] %.*s\n", (int)n->as.fn_decl.name.size(), n->as.fn_decl.name.data());
        visit(n->as.fn_decl.body);
    }

    void visitBlock(ASTNode* n) { for (auto* s : n->as.block) visit(s); }
    void visitBinary(ASTNode* n) { visit(n->as.binary.left); visit(n->as.binary.right); }

private:
    void printType(const Type& t) {
        for (int i = 0; i < t.pointer_level; ++i) printf("*");
        if (!t.name.empty()) printf("%.*s", (int)t.name.size(), t.name.data());
        else printf("basic_type");
    }
};

// --- 3. 驱动流水线 ---
void runTest(const std::string& label, std::string_view source) {
    printf("\n>>> 测试场景: %s <<<\n", label.data());

    ASTContext ctx;
    DiagEngine diag;

    // 1. Lexer
    Lexer lexer(llvm::StringRef(source.data(), source.size()));
    llvm::SmallVector<Token, 128> tokens;
    lexer.scanAll(tokens);

    // 2. Parser (禁用异常，返回列表)
    Parser parser(tokens, ctx, diag);
    llvm::SmallVector<ASTNode*, 16> program;
    auto vec = parser.parseProgram();
    for (auto* n : vec) program.push_back(n);

    // 3. Sema (仅在语法无错时进行)
    if (!diag.hasError()) {
        Sema sema(ctx, diag);
        if (sema.analyze(program)) {
            printf("[*] 语义分析通过! 元数据提取如下:\n");
            MetadataPrinter printer;
            for (auto* n : program) printer.visit(n);
        }
    }

    // 4. 展示所有收集到的诊断信息 (多语言)
    if (diag.hasError()) {
        printf("[!] 发现语义/语法错误:\n");
        for (const auto& d : diag.getDiagnostics()) {
            translateAndPrint(d, Lang::CN);
        }
    }
    printf("------------------------------------------\n");
}

int main() {
    // 情况 A: 典型的 HPC 内存操作 (应成功)
    runTest("合法指针与结构体逻辑", R"(
        struct Vec3 { x: f32; y: f32; z: f32; }
        fn kernel(p: *Vec3) -> void {
            p.*.y = 10.5;
            let val: f32 = p.*.x;
        }
    )");

    // 情况 B: 类型不匹配 (应报错)
    runTest("类型检查拦截", R"(
        fn main() -> void {
            let a: i32 = 42;
            let b: f32 = a;
        }
    )");

    // 情况 C: 成员不存在 (应报错并显示具体成员名)
    runTest("结构体成员验证", R"(
        struct Point { x: i32; }
        fn main() -> void {
            let p: Point;
            p.z = 100;
        }
    )");

    return 0;
}