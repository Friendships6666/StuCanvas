#ifndef SYNTAX_CHECKER_H
#define SYNTAX_CHECKER_H

#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include "../include/graph/GeoGraph.h"
namespace CAS::Parser {

    enum class SyntaxErrorCode : uint32_t {
        SUCCESS = 0,
        ERR_EMPTY_EXPRESSION,     // 表达式为空
        ERR_UNBALANCED_PAREN,     // 括号不闭合或多余
        ERR_EMPTY_EQUAL_SIDE,     // 等号左侧或右侧为空
        ERR_MISSING_OPERAND,      // 二元运算符 (+,*,/,^) 缺少操作数
        ERR_INVALID_FUNC_SYNTAX,  // 函数格式错误 (例如 Name(,) )
        ERR_WRONG_ARG_COUNT,      // 参数数量不正确
        ERR_INVALID_ARG_TYPE,     // 参数类型错误 (例如要求名字却给数字)
        ERR_NUMBER_FORMAT,        // 数字语法错误 (例如 123a)
        ERR_UNEXPECTED_COMMA,     // 逗号出现在非法位置
        ERR_MACRO_VIOLATION,      // 宏函数不能与其他表达式混用
        ERR_UNKNOWN_TOKEN,         // 无法识别的字符
        ERR_NAME_ILLEGAL
    };

    struct SyntaxCheckResult {
        bool             success = true;
        bool             is_macro = false;    // 是否包含宏函数
        SyntaxErrorCode  error_code = SyntaxErrorCode::SUCCESS;
        std::string      error_msg;
        int32_t          error_pos = -1;
    };

    // 唯一的对外接口：纯语法检查
    SyntaxCheckResult check_syntax(std::string_view expression, const GeometryGraph& graph);

} // namespace CAS::Parser

#endif