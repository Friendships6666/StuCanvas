// --- 文件路径: include/CAS/symbolic/GraphicSimplify.h ---

#ifndef GRAPHIC_SIMPLIFY_H
#define GRAPHIC_SIMPLIFY_H

#include "../../../pch.h"


namespace CAS::GraphicSimplify {

    struct Expression { virtual ~Expression() = default; };
    struct Constant : Expression { double value; explicit Constant(double v) : value(v) {} };
    struct Symbol : Expression { std::string name; explicit Symbol(std::string n) : name(std::move(n)) {} };

    struct RationalNumber : Expression {
        long long num;
        long long den;
        RationalNumber(long long n, long long d) {
            if (d == 0) throw std::runtime_error("分母不能为零");
            long long common = std::gcd(n, d);
            num = n / common;
            den = d / common;
            if (den < 0) { num = -num; den = -den; }
        }
        double to_double() const { return static_cast<double>(num) / den; }
    };

    struct Function : Expression {
        std::string op;
        std::vector<std::shared_ptr<Expression>> args;
        Function(std::string o, std::vector<std::shared_ptr<Expression>> a) : op(std::move(o)), args(std::move(a)) {}
    };

    // ====================================================================
    //  MODIFIED: 接口变更
    //  - 移除旧的 constant_fold 声明。
    //  - 新增 generate_rpn_from_ast，它接收一个 AST 并返回一个包含两个 RPN 字符串的 pair。
    //    pair.first  -> Normal RPN (用于主计算)
    //    pair.second -> Check RPN (用于 lerp 中的检查)
    // ====================================================================
    std::pair<std::string, std::string> generate_rpn_from_ast(const std::shared_ptr<Expression>& ast);
    std::pair<std::string, std::string> constant_fold(const std::shared_ptr<Expression> &ast);

} // namespace CAS::GraphicSimplify

#endif // GRAPHIC_SIMPLIFY_H