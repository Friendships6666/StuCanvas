#ifndef STUSCRIPT_DIAGNOSTIC_HPP
#define STUSCRIPT_DIAGNOSTIC_HPP

#include "llvm/ADT/SmallVector.h"
#include <string_view>
#include <cstdint>

namespace StuScript {

// --- 1. 错误代码枚举 (用于多语言映射的 Key) ---
enum class DiagCode : uint16_t {
    // Lexer
    L_InvalidChar,             // 无效字符
    L_UnterminatedString,      // 字符串未闭合

    // Parser
    P_UnexpectedToken,         // 意外的 Token (参数0: 实际得到的片段)
    P_MissingSemicolon,        // 缺少分号
    P_ExpectExpression,        // 期望表达式
    P_ExpectTypeName,          // 期望类型名

    // Sema
    S_UndefinedVariable,       // 变量未定义 (参数0: 变量名)
    S_TypeMismatch,            // 类型不匹配 (参数0: 期望类型, 参数1: 实际类型)
    S_NotAnLValue,             // 非左值不可赋值
    S_InvalidDereference,      // 非指针不可解引用
    S_MemberNotFound           // 结构体成员不存在 (参数0: 结构体名, 参数1: 成员名)
};

enum class DiagLevel : uint8_t { Note, Warning, Error, Fatal };

// --- 2. 诊断信息结构体 (纯数据，POD 布局) ---
struct Diagnostic {
    uint32_t line;
    uint32_t col;
    DiagCode code;
    DiagLevel level;

    // 使用 std::string_view 避免字符串拷贝开销
    // 存储 2 个动态参数，足以覆盖绝大多数编译错误需求
    std::string_view args[2];
    uint8_t arg_count = 0;

    // 构造函数
    Diagnostic(uint32_t l, uint32_t c, DiagCode cd, DiagLevel lv)
        : line(l), col(c), code(cd), level(lv), arg_count(0) {}
};

// --- 3. 诊断引擎 (不包含 I/O 操作) ---
class DiagEngine {
public:
    // 使用 llvm::SmallVector 替代 std::vector
    // 预留 32 个元素在栈上，避免小规模错误时的堆内存分配 (malloc)
    using DiagList = llvm::SmallVector<Diagnostic, 32>;

    // 报告错误：0 个参数
    void report(uint32_t line, uint32_t col, DiagCode code, DiagLevel level) {
        diags_.emplace_back(line, col, code, level);
        if (level >= DiagLevel::Error) has_error_ = true;
    }

    // 报告错误：1 个参数
    void report(uint32_t line, uint32_t col, DiagCode code, DiagLevel level, std::string_view arg0) {
        auto& d = diags_.emplace_back(line, col, code, level);
        d.args[0] = arg0;
        d.arg_count = 1;
        if (level >= DiagLevel::Error) has_error_ = true;
    }

    // 报告错误：2 个参数
    void report(uint32_t line, uint32_t col, DiagCode code, DiagLevel level, std::string_view arg0, std::string_view arg1) {
        auto& d = diags_.emplace_back(line, col, code, level);
        d.args[0] = arg0;
        d.args[1] = arg1;
        d.arg_count = 2;
        if (level >= DiagLevel::Error) has_error_ = true;
    }

    // 高性能状态查询
    bool hasError() const { return has_error_; }

    // 获取内部诊断列表引用 (供外部多语言打印机使用)
    const DiagList& getDiagnostics() const { return diags_; }

    // 重置引擎状态
    void clear() {
        diags_.clear();
        has_error_ = false;
    }

private:
    DiagList diags_;
    bool has_error_ = false;
};

} // namespace StuScript

#endif