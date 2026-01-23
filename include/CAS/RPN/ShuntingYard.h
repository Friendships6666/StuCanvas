#ifndef CAS_RPN_SHUNTING_YARD_H
#define CAS_RPN_SHUNTING_YARD_H

#include "RPN.h"
#include <string>
#include <vector>
#include <string_view>
#include "../include/graph/GeoGraph.h" // 需要 GeoStatus 定义
namespace CAS::Parser {

    // 使用枚举标识函数类型，比字符串比对快得多
        enum class CustomFunctionType : uint8_t  {
        NONE,
        LENGTH,
        AREA,
        EXTRACT_VALUE_X,
        EXTRACT_VALUE_Y,

        // 易于以后扩展
    };

    struct RPNBindingSlot {
        size_t rpn_index;        // RPN 数组下标
        enum class SlotType { VARIABLE, FUNCTION } type;

        // --- 变量路径 ---
        std::string source_name; // 仅存储对象名 (如 "A", "a")

        // --- 函数路径 ---
        CustomFunctionType func_type = CustomFunctionType::NONE;
        std::vector<std::string> args;
    };

    struct CompileResult {
        AlignedVector<RPNToken> bytecode;
        std::vector<RPNBindingSlot> binding_slots;

        bool        success = true; // 只保留这个标志
        std::string error_arg;      // 记录出错的符号（如 "sin(" 或 "++"）
    };

    CompileResult compile_infix_to_rpn(std::string_view expression);
}

#endif