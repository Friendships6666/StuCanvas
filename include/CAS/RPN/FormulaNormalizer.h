#ifndef FORMULA_NORMALIZER_H
#define FORMULA_NORMALIZER_H

#include <string>
#include <string_view>
#include "../include/graph/GeoGraph.h"

namespace CAS::Parser {

    /**
     * @brief 公式规范化器
     * 职责：
     * 1. 去除所有空白字符
     * 2. 符号折叠 (Sign Folding):
     *    - 消除一元正号 (+3 -> 3)
     *    - 合并连续符号 (1 - - 3 -> 1 + 3, 1 + - 3 -> 1 - 3)
     *    - 变量前的一元负号标准化 (-x -> (0-x) 或保留 -x 视后续 RPN 需求，这里采用保留 -x 但紧凑化)
     * 3. 显式优先级 (Explicit Precedence):
     *    - 3+2*4 -> 3+(2*4)
     *    - 根据运算符优先级自动添加括号，确保计算顺序在字符串层面唯一。
     */
    class FormulaNormalizer {
    public:
        /**
         * @brief 执行规范化流程
         * @param input 原始输入字符串 (假设已通过 SyntaxChecker)
         * @param graph 几何图上下文 (用于识别函数名)
         * @return 规范化后的字符串
         */
        static std::string Normalize(std::string_view input, const GeometryGraph& graph);

    private:
        // 内部 Token 定义 (独立于 SyntaxChecker，专注于重写)
        enum class NormTokenType { ID, NUM, OP, LP, RP, COMMA, EQ };
        struct NormToken {
            NormTokenType type;
            std::string content;
            bool is_unary = false; // 标记是否为一元运算符 (+/-)
        };

        // 步骤 1: 词法切分 (同时识别一元/二元上下文)
        static std::vector<NormToken> Tokenize(std::string_view input, const GeometryGraph& graph);

        // 步骤 2: 符号折叠 (处理 +-+- 链)
        static std::vector<NormToken> FoldSigns(const std::vector<NormToken>& tokens);

        // 步骤 3: 优先级括号重构 (Shunting Yard to String)
        static std::string AddParentheses(const std::vector<NormToken>& tokens);
    };

} // namespace CAS::Parser

#endif // FORMULA_NORMALIZER_H