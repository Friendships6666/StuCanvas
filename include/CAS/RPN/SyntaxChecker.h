#ifndef SYNTAX_CHECKER_H
#define SYNTAX_CHECKER_H

#include <string>
#include <string_view>
#include <cstdint>
#include "../include/graph/GeoGraph.h"
namespace CAS::Parser {

    enum class SyntaxErrorCode : uint32_t {
        SUCCESS = 0,
        ERR_EMPTY_EXPRESSION,
        ERR_UNBALANCED_PAREN,
        ERR_EMPTY_EQUAL_SIDE,
        ERR_MISSING_OPERAND,
        ERR_INVALID_FUNC_SYNTAX,
        ERR_WRONG_ARG_COUNT,
        ERR_INVALID_ARG_TYPE,
        ERR_NUMBER_FORMAT,
        ERR_UNEXPECTED_COMMA,
        ERR_MACRO_VIOLATION,
        ERR_UNKNOWN_TOKEN,
        ERR_NAME_ILLEGAL,


        ERR_TYPE_MISMATCH,      // 类型不匹配 (如 Vector + Scalar)
        ERR_VECTOR_RESTRICTION, // 向量内含有非法变量 (如 x, y)
        ERR_INVALID_CROSS_OP,    // 叉乘操作数无效

    };

    struct SyntaxCheckResult {
        bool             success = true;
        bool             is_macro = false;
        SyntaxErrorCode  error_code = SyntaxErrorCode::SUCCESS;
        std::string      error_msg;
        int32_t          error_pos = -1;
    };

    SyntaxCheckResult check_syntax(std::string_view expression, const class GeometryGraph& graph);

} // namespace CAS::Parser

#endif