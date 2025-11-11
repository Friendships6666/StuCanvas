// --- 文件路径: include/CAS/ConstantFolding.h ---

#ifndef CONSTANT_FOLDING_H
#define CONSTANT_FOLDING_H

#include "../../../pch.h"


namespace CAS::ConstantFolding {

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

    std::shared_ptr<Expression> constant_fold(const std::shared_ptr<Expression>& ast);
    std::shared_ptr<Expression> rewrite_powers_for_cpp_engine(const std::shared_ptr<Expression>& ast); // <-- 新增声明

} // namespace CAS::ConstantFolding

#endif // CONSTANT_FOLDING_H