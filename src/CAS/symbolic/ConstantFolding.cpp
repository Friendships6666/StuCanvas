// --- 文件路径: src/CAS/ConstantFolding.cpp ---

#include "../../../include/CAS/symbolic/ConstantFolding.h"
#include "../../../pch.h"
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <set>
#include <limits>


namespace CAS::ConstantFolding {
    namespace { // 匿名命名空间开始

        // ====================================================================
        //  前向声明 (Forward Declarations)
        // ====================================================================
        // ====================================================================
        //  前向声明 (Forward Declarations) - 已修正
        // ====================================================================
        std::shared_ptr<Expression> try_factor_expression(const std::shared_ptr<Expression>& expr);
        std::shared_ptr<Expression> expand_powers(const std::shared_ptr<Expression>& expr);
        std::shared_ptr<Expression> rewrite_roots_recursive(const std::shared_ptr<Expression>& expr);
        std::shared_ptr<Expression> rewrite_powers_recursive(const std::shared_ptr<Expression>& expr);
        std::shared_ptr<Expression> convert_constants_to_rationals_recursive(const std::shared_ptr<Expression>& expr);
        std::shared_ptr<Expression> convert_rationals_to_constants_recursive(const std::shared_ptr<Expression>& expr);

        // --- 修正点: 更新以下两个声明，使其包含默认参数 ---
        std::shared_ptr<Expression> standardize_functions_recursive(const std::shared_ptr<Expression>& expr, bool block_tan_replacement = false);
        std::shared_ptr<Expression> constant_fold_recursive(const std::shared_ptr<Expression>& ast, bool block_power_negation = false);



        // ====================================================================
        //  预处理与转换辅助函数 (Preprocessing & Conversion Helpers)
        // ====================================================================
        // ... (rewrite_roots_recursive, to_rational, etc. 保持不变) ...

        // --- 文件路径: src/CAS/ConstantFolding.cpp ---
// 放在匿名命名空间内，其他辅助函数的前面或后面均可

// --- 文件路径: src/CAS/ConstantFolding.cpp ---
// 位于匿名命名空间内

// 定义哪些函数会 "冻结" 其内部参数的特定替换
const std::set<std::string> blocking_ops_for_tan_and_power = {
    "Sin", "Cos", "Tan", "Ln", "Log"
};
        // --- 文件路径: src/CAS/ConstantFolding.cpp ---
        // 位于匿名命名空间内

        // 辅助函数: 递归地查找表达式中的所有符号 (变量)
        void get_symbols_recursive(const std::shared_ptr<Expression>& expr, std::set<std::string>& symbols) {
            if (!expr) return;
            if (auto sym = std::dynamic_pointer_cast<Symbol>(expr)) {
                symbols.insert(sym->name);
            } else if (auto func = std::dynamic_pointer_cast<Function>(expr)) {
                for (const auto& arg : func->args) {
                    get_symbols_recursive(arg, symbols);
                }
            }
        }

        // 辅助函数: 判断一个表达式是否包含任何符号 (变量)
        bool has_symbols(const std::shared_ptr<Expression>& expr) {
            std::set<std::string> symbols;
            get_symbols_recursive(expr, symbols);
            return !symbols.empty();
        }

std::shared_ptr<Expression> standardize_functions_recursive(const std::shared_ptr<Expression>& expr, bool block_tan_replacement) {
    if (!expr) {
        return nullptr;
    }

    auto func = std::dynamic_pointer_cast<Function>(expr);
    if (!func) {
        return expr;
    }

    // --- 步骤 1: 无条件替换 (这些规则总是生效) ---

    // 规则: Log(base, number) -> Ln(number) / Ln(base)
    if (func->op == "Log" && func->args.size() == 2) {
        // 先递归处理参数，再应用规则
        auto base = standardize_functions_recursive(func->args[0], block_tan_replacement);
        auto number = standardize_functions_recursive(func->args[1], block_tan_replacement);
        auto ln_number = std::make_shared<Function>("Ln", std::vector<std::shared_ptr<Expression>>{number});
        auto ln_base = std::make_shared<Function>("Ln", std::vector<std::shared_ptr<Expression>>{base});
        return std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{ln_number, ln_base});
    }
    // 规则: Sec(x) -> 1 / Cos(x)
    if (func->op == "Sec" && func->args.size() == 1) {
        auto arg = standardize_functions_recursive(func->args[0], block_tan_replacement);
        auto cos_x = std::make_shared<Function>("Cos", std::vector<std::shared_ptr<Expression>>{arg});
        return std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{std::make_shared<Constant>(1.0), cos_x});
    }
    // 规则: Csc(x) -> 1 / Sin(x)
    if (func->op == "Csc" && func->args.size() == 1) {
        auto arg = standardize_functions_recursive(func->args[0], block_tan_replacement);
        auto sin_x = std::make_shared<Function>("Sin", std::vector<std::shared_ptr<Expression>>{arg});
        return std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{std::make_shared<Constant>(1.0), sin_x});
    }
    // 规则: Cot(x) -> Cos(x) / Sin(x)
    if (func->op == "Cot" && func->args.size() == 1) {
        auto arg = standardize_functions_recursive(func->args[0], block_tan_replacement);
        auto cos_x = std::make_shared<Function>("Cos", std::vector<std::shared_ptr<Expression>>{arg});
        auto sin_x = std::make_shared<Function>("Sin", std::vector<std::shared_ptr<Expression>>{arg});
        return std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{cos_x, sin_x});
    }

    // --- 步骤 2: 递归处理子节点，并确定是否需要传递 "冻结" 标记 ---
    bool should_block_children = blocking_ops_for_tan_and_power.count(func->op) > 0;

    std::vector<std::shared_ptr<Expression>> standardized_args;
    standardized_args.reserve(func->args.size());

    // 对 Power 函数进行特殊处理
    if (func->op == "Power" && func->args.size() == 2) {
        // Power 的底数(base)不被冻结
        standardized_args.push_back(standardize_functions_recursive(func->args[0], block_tan_replacement));
        // Power 的指数(exponent)被冻结
        standardized_args.push_back(standardize_functions_recursive(func->args[1], true));
    } else {
        // 其他函数统一处理
        for (const auto& arg : func->args) {
            standardized_args.push_back(standardize_functions_recursive(arg, should_block_children));
        }
    }

    auto new_func = std::make_shared<Function>(func->op, standardized_args);

    // --- 步骤 3: 条件性替换 Tan (只有在当前上下文未被冻结时) ---
    if (!block_tan_replacement) {
        if (new_func->op == "Tan" && new_func->args.size() == 1) {
            auto arg = new_func->args[0];
            auto sin_x = std::make_shared<Function>("Sin", std::vector<std::shared_ptr<Expression>>{arg});
            auto cos_x = std::make_shared<Function>("Cos", std::vector<std::shared_ptr<Expression>>{arg});
            return std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{sin_x, cos_x});
        }
    }

    return new_func;
}

        std::shared_ptr<Expression> rewrite_roots_recursive(const std::shared_ptr<Expression>& expr) {
            if (!expr) return nullptr;

            auto func = std::dynamic_pointer_cast<Function>(expr);
            if (!func) return expr;

            // 递归处理子节点
            std::vector<std::shared_ptr<Expression>> new_args;
            new_args.reserve(func->args.size());
            for (const auto& arg : func->args) {
                new_args.push_back(rewrite_roots_recursive(arg));
            }
            auto rewritten_func = std::make_shared<Function>(func->op, new_args);

            // 转换 Sqrt
            if (rewritten_func->op == "Sqrt" && rewritten_func->args.size() == 1) {
                return std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{
                    rewritten_func->args[0],
                    std::make_shared<RationalNumber>(1, 2)
                });
            }

            // 转换 Root
            if (rewritten_func->op == "Root" && rewritten_func->args.size() == 2) {
                if (auto index_const = std::dynamic_pointer_cast<Constant>(rewritten_func->args[1])) {
                    double index_val = index_const->value;
                    if (index_val > 0 && std::abs(index_val - std::round(index_val)) < 1e-9) {
                        long long n = static_cast<long long>(std::round(index_val));
                        return std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{
                            rewritten_func->args[0],
                            std::make_shared<RationalNumber>(1, n)
                        });
                    }
                }
            }

            return rewritten_func;
        }

        std::optional<std::pair<long long, long long>> to_rational(double val, long long max_denominator = 10000) {
            if (std::abs(val) < 1e-9 || std::abs(val - std::round(val)) < 1e-9) {
                return std::nullopt; // 零或整数不需要转换
            }
            const double epsilon = 1e-9;
            long long p0 = 0, q0 = 1, p1 = 1, q1 = 0;
            double x = val;
            long long a;

            do {
                a = static_cast<long long>(std::floor(x));
                long long p2 = a * p1 + p0;
                long long q2 = a * q1 + q0;

                if (q2 > max_denominator) {
                    break;
                }

                p0 = p1; q0 = q1;
                p1 = p2; q1 = q2;

                if (std::abs(x - static_cast<double>(a)) < epsilon) {
                    break;
                }
                x = 1.0 / (x - a);
            } while (true);

            if (q1 != 0 && std::abs(val - static_cast<double>(p1) / q1) < epsilon) {
                return std::make_pair(p1, q1);
            }

            return std::nullopt;
        }

        std::shared_ptr<Expression> convert_constants_to_rationals_recursive(const std::shared_ptr<Expression>& expr) {
            if (!expr) return nullptr;

            if (auto con = std::dynamic_pointer_cast<Constant>(expr)) {
                if (auto rat_pair = to_rational(con->value)) {
                    return std::make_shared<RationalNumber>(rat_pair->first, rat_pair->second);
                }
            }

            if (auto func = std::dynamic_pointer_cast<Function>(expr)) {
                std::vector<std::shared_ptr<Expression>> new_args;
                new_args.reserve(func->args.size());
                for (const auto& arg : func->args) {
                    new_args.push_back(convert_constants_to_rationals_recursive(arg));
                }
                return std::make_shared<Function>(func->op, new_args);
            }

            return expr;
        }

        std::shared_ptr<Expression> convert_rationals_to_constants_recursive(const std::shared_ptr<Expression>& expr) {
            if (!expr) return nullptr;

            if (auto rat = std::dynamic_pointer_cast<RationalNumber>(expr)) {
                return std::make_shared<Constant>(rat->to_double());
            }

            if (auto func = std::dynamic_pointer_cast<Function>(expr)) {
                std::vector<std::shared_ptr<Expression>> new_args;
                new_args.reserve(func->args.size());
                for (const auto& arg : func->args) {
                    new_args.push_back(convert_rationals_to_constants_recursive(arg));
                }
                return std::make_shared<Function>(func->op, new_args);
            }

            return expr;
        }

        // ====================================================================
        //  核心辅助函数 (Core Helper Functions)
        // ====================================================================
        // ... (expression_to_string, to_numeric, etc. 保持不变) ...
        std::string expression_to_string(const std::shared_ptr<Expression>& expr) {
            if (!expr) return "";
            if (auto con = std::dynamic_pointer_cast<Constant>(expr)) {
                std::ostringstream ss;
                ss.precision(std::numeric_limits<double>::max_digits10);
                ss << con->value;
                return ss.str();
            }
            if (auto sym = std::dynamic_pointer_cast<Symbol>(expr)) {
                return sym->name;
            }
            if (auto rat = std::dynamic_pointer_cast<RationalNumber>(expr)) {
                return std::to_string(rat->num) + "/" + std::to_string(rat->den);
            }
            if (auto func = std::dynamic_pointer_cast<Function>(expr)) {
                std::string result = func->op + "(";
                std::vector<std::string> arg_strings;
                arg_strings.reserve(func->args.size());
                for (const auto& arg : func->args) {
                    arg_strings.push_back(expression_to_string(arg));
                }

                if (func->op == "Add" || func->op == "Multiply") {
                    std::sort(arg_strings.begin(), arg_strings.end());
                }

                for (size_t i = 0; i < arg_strings.size(); ++i) {
                    result += arg_strings[i];
                    if (i < arg_strings.size() - 1) {
                        result += ",";
                    }
                }
                result += ")";
                return result;
            }
            std::ostringstream address;
            address << (void const *)expr.get();
            return "unknown_type_at_" + address.str();
        }

        std::optional<double> to_numeric(const std::shared_ptr<Expression>& expr) {
            if (auto con = std::dynamic_pointer_cast<Constant>(expr)) return con->value;
            if (auto rat = std::dynamic_pointer_cast<RationalNumber>(expr)) return rat->to_double();
            return std::nullopt;
        }

        void get_factors(const std::shared_ptr<Expression>& expr, std::vector<std::shared_ptr<Expression>>& factors) {
            if (auto func = std::dynamic_pointer_cast<Function>(expr); func && func->op == "Multiply") {
                for (const auto& arg : func->args) {
                    get_factors(arg, factors);
                }
            } else {
                factors.push_back(expr);
            }
        }

        std::shared_ptr<Expression> rebuild_expression_from_factors(const std::vector<std::shared_ptr<Expression>>& factors) {
            if (factors.empty()) return std::make_shared<Constant>(1.0);
            if (factors.size() == 1) return factors[0];
            return std::make_shared<Function>("Multiply", factors);
        }

        void get_variables(const std::shared_ptr<Expression>& expr, std::set<std::string>& vars) {
            if (!expr) return;
            if (auto sym = std::dynamic_pointer_cast<Symbol>(expr)) {
                vars.insert(sym->name);
            } else if (auto func = std::dynamic_pointer_cast<Function>(expr)) {
                for (const auto& arg : func->args) {
                    get_variables(arg, vars);
                }
            }
        }


        // ====================================================================
        //  幂函数重写 (Power Rewriting) - 内部实现
        // ====================================================================

        // --- 修正点: 整个 rewrite_powers_recursive 逻辑更新 ---
        std::shared_ptr<Expression> rewrite_powers_recursive(const std::shared_ptr<Expression>& expr) {
            auto func = std::dynamic_pointer_cast<Function>(expr);
            if (!func) return expr;

            std::vector<std::shared_ptr<Expression>> rewritten_args;
            rewritten_args.reserve(func->args.size());
            for(const auto& arg : func->args) {
                rewritten_args.push_back(rewrite_powers_recursive(arg));
            }

            auto rewritten_expr = std::make_shared<Function>(func->op, rewritten_args);

            if (rewritten_expr->op == "Power" && rewritten_expr->args.size() == 2) {
                auto base = rewritten_expr->args[0];
                auto exponent = rewritten_expr->args[1];

                if (auto rat_exp = std::dynamic_pointer_cast<RationalNumber>(exponent)) {
                    long long p = rat_exp->num;
                    long long q = rat_exp->den;

                    // 如果分母为偶数，则表达式在实数域中仅对非负底数有定义。
                    // 不应添加 Abs()，因为这会错误地改变函数的定义域。
                    // 保持表达式原样，让计算引擎处理定义域问题。
                    if (std::abs(q) % 2 == 0) {
                        return rewritten_expr;
                    }
                    // 如果分母为奇数，我们可以根据分子的奇偶性进行改写。
                    else {
                        // 分母奇，分子偶 (例如 x^(2/3)) -> 偶函数
                        if (std::abs(p) % 2 == 0) {
                            auto abs_base = std::make_shared<Function>("Abs", std::vector<std::shared_ptr<Expression>>{base});
                            return std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{abs_base, exponent});
                        }
                        // 分母奇，分子奇 (例如 x^(1/3)) -> 奇函数
                        else {
                            auto abs_base = std::make_shared<Function>("Abs", std::vector<std::shared_ptr<Expression>>{base});
                            auto sign_base = std::make_shared<Function>("Sign", std::vector<std::shared_ptr<Expression>>{base});
                            auto abs_pow = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{abs_base, exponent});
                            return std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{sign_base, abs_pow});
                        }
                    }
                }
            }
            return rewritten_expr;
        }


        // ====================================================================
        //  多项式因式分解及其他 (Polynomial Factoring & Others)
        // ====================================================================
        // ... (所有其他函数，如 parse_polynomial, constant_fold_recursive 等，保持不变) ...
        std::optional<std::map<int, double>> parse_polynomial(const std::shared_ptr<Expression>& expr, const std::string& var) {
            std::map<int, double> coefficients;
            std::function<void(const std::shared_ptr<Expression>&)> process_term =
                [&](const std::shared_ptr<Expression>& term) {

                    if (auto add_func = std::dynamic_pointer_cast<Function>(term); add_func && add_func->op == "Add") {
                        for (const auto& arg : add_func->args) process_term(arg);
                        return;
                    }

                    double final_coeff = 1.0;
                    std::shared_ptr<Expression> current_term = term;

                    if (auto neg_func = std::dynamic_pointer_cast<Function>(current_term); neg_func && neg_func->op == "Negate" && neg_func->args.size() == 1) {
                        final_coeff = -1.0;
                        current_term = neg_func->args[0];
                    }

                    if (auto num = to_numeric(current_term)) {
                        coefficients[0] += final_coeff * (*num);
                    } else if (auto sym = std::dynamic_pointer_cast<Symbol>(current_term); sym && sym->name == var) {
                        coefficients[1] += final_coeff;
                    } else if (auto mul_func = std::dynamic_pointer_cast<Function>(current_term); mul_func && mul_func->op == "Multiply") {
                        double term_coeff = 1.0; int term_power = 0; bool is_simple = true;
                        for (const auto& factor : mul_func->args) {
                            if (auto num = to_numeric(factor)) term_coeff *= *num;
                            else if (auto sym = std::dynamic_pointer_cast<Symbol>(factor); sym && sym->name == var) term_power++;
                            else { is_simple = false; break; }
                        }
                        if (is_simple) coefficients[term_power] += final_coeff * term_coeff;
                    } else if (auto pow_func = std::dynamic_pointer_cast<Function>(current_term); pow_func && pow_func->op == "Power" && pow_func->args.size() == 2) {
                        if (auto sym = std::dynamic_pointer_cast<Symbol>(pow_func->args[0]); sym && sym->name == var) {
                            if (auto exp = to_numeric(pow_func->args[1])) {
                                if (std::abs(*exp - std::round(*exp)) < 1e-9) {
                                    coefficients[static_cast<int>(std::round(*exp))] += final_coeff;
                                }
                            }
                        }
                    }
            };

            process_term(expr);
            return coefficients;
        }

        std::shared_ptr<Expression> try_factor_quadratic(const std::map<int, double>& coeffs, const std::string& var) {
            if (coeffs.empty() || coeffs.rbegin()->first > 2) return nullptr;
            double a = coeffs.count(2) ? coeffs.at(2) : 0.0, b = coeffs.count(1) ? coeffs.at(1) : 0.0, c = coeffs.count(0) ? coeffs.at(0) : 0.0;
            if (a == 0) return nullptr;
            double discriminant = b * b - 4 * a * c;
            if (discriminant < 0) return nullptr;
            double sqrt_d = std::sqrt(discriminant);
            if (std::abs(sqrt_d - std::round(sqrt_d)) > 1e-9) return nullptr;
            double root1 = (-b + sqrt_d) / (2 * a), root2 = (-b - sqrt_d) / (2 * a);
            auto factor1 = std::make_shared<Function>("Add", std::vector<std::shared_ptr<Expression>>{std::make_shared<Symbol>(var), std::make_shared<Constant>(-root1)});
            auto factor2 = std::make_shared<Function>("Add", std::vector<std::shared_ptr<Expression>>{std::make_shared<Symbol>(var), std::make_shared<Constant>(-root2)});
            if (a == 1.0) return std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{factor1, factor2});
            return std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{std::make_shared<Constant>(a), factor1, factor2});
        }

        std::shared_ptr<Expression> try_factor_expression(const std::shared_ptr<Expression>& expr) {
            std::set<std::string> vars;
            get_variables(expr, vars);

            if (vars.size() != 1) {
                return expr;
            }
            std::string var = *vars.begin();

            if (auto poly_coeffs = parse_polynomial(expr, var)) {
                if (!poly_coeffs->empty() && poly_coeffs->rbegin()->first <= 2) {
                    if (auto factored = try_factor_quadratic(*poly_coeffs, var)) {
                        return constant_fold_recursive(factored);
                    }
                }
            }
            return expr;
        }

        std::shared_ptr<Expression> expand_powers(const std::shared_ptr<Expression>& expr) {
            auto func = std::dynamic_pointer_cast<Function>(expr);
            if (!func) return expr;

            std::vector<std::shared_ptr<Expression>> expanded_args;
            expanded_args.reserve(func->args.size());
            for(const auto& arg : func->args) {
                expanded_args.push_back(expand_powers(arg));
            }

            if (func->op == "Power" && expanded_args.size() == 2) {
                if (auto exp_val = to_numeric(expanded_args[1])) {
                    double exp = *exp_val;
                    if (exp > 0 && std::abs(exp - std::round(exp)) < 1e-9) {
                        int n = static_cast<int>(std::round(exp));
                        if (n == 1) return expanded_args[0];
                        std::vector<std::shared_ptr<Expression>> factors(n, expanded_args[0]);
                        return std::make_shared<Function>("Multiply", factors);
                    }
                }
            }

            return std::make_shared<Function>(func->op, expanded_args);
        }



// --- 文件路径: src/CAS/ConstantFolding.cpp ---
// 位于匿名命名空间内

// --- 文件路径: src/CAS/ConstantFolding.cpp ---
// 位于匿名命名空间内

std::shared_ptr<Expression> constant_fold_recursive(const std::shared_ptr<Expression>& ast, bool block_power_negation) {
    auto func = std::dynamic_pointer_cast<Function>(ast);
    if (!func) {
        return ast;
    }

    // --- 步骤 1: 递归地化简所有子节点，并根据上下文传递冻结标记 ---
    bool should_block_children = blocking_ops_for_tan_and_power.count(func->op) > 0;

    for (size_t i = 0; i < func->args.size(); ++i) {
        bool block_this_arg = should_block_children;
        if (func->op == "Power" && i == 1) {
            block_this_arg = true;
        }
        func->args[i] = constant_fold_recursive(func->args[i], block_this_arg);
    }

    // --- 步骤 2: 对所有参数都已化简为数值的函数进行求值 ---
    bool all_args_are_numeric = true;
    std::vector<double> numeric_args;
    for (const auto& arg : func->args) {
        if (auto num = to_numeric(arg)) {
            numeric_args.push_back(*num);
        } else {
            all_args_are_numeric = false;
            break;
        }
    }
    if (all_args_are_numeric && !numeric_args.empty()) {
        try {
            if (func->op == "Add") { double s=0; for(double v:numeric_args) s+=v; return std::make_shared<Constant>(s); }
            if (func->op == "Multiply") { double p=1; for(double v:numeric_args) p*=v; return std::make_shared<Constant>(p); }
            if (func->op == "Divide" && numeric_args.size()==2 && numeric_args[1]!=0) return std::make_shared<Constant>(numeric_args[0]/numeric_args[1]);
            if (func->op == "Negate" && numeric_args.size()==1) return std::make_shared<Constant>(-numeric_args[0]);
            if (func->op == "Tan" && numeric_args.size()==1) return std::make_shared<Constant>(std::tan(numeric_args[0]));
            if (func->op == "Power" && numeric_args.size()==2) return std::make_shared<Constant>(std::pow(numeric_args[0], numeric_args[1]));
            if (func->op == "Abs" && numeric_args.size()==1) return std::make_shared<Constant>(std::abs(numeric_args[0]));
            if (func->op == "Sin" && numeric_args.size()==1) return std::make_shared<Constant>(std::sin(numeric_args[0]));
            if (func->op == "Cos" && numeric_args.size()==1) return std::make_shared<Constant>(std::cos(numeric_args[0]));
            if (func->op == "Ln" && numeric_args.size()==1) return std::make_shared<Constant>(std::log(numeric_args[0]));

            if (func->op == "Tan" && numeric_args.size()==1) return std::make_shared<Constant>(std::tan(numeric_args[0]));
            if (func->op == "Power" && numeric_args.size()==2) return std::make_shared<Constant>(std::pow(numeric_args[0], numeric_args[1]));
            if (func->op == "Abs" && numeric_args.size()==1) return std::make_shared<Constant>(std::abs(numeric_args[0]));
            if (func->op == "Sign" && numeric_args.size()==1) return std::make_shared<Constant>((numeric_args[0] > 0.0) - (numeric_args[0] < 0.0));
            if (func->op == "Sign" && numeric_args.size()==1) return std::make_shared<Constant>((numeric_args[0] > 0.0) - (numeric_args[0] < 0.0));
        } catch (...) { }
    }

    // --- 步骤 3: 应用针对特定函数的代数化简规则 ---

    // 化简 Power(base, exponent)
    if (func->op == "Power" && func->args.size() == 2) {
        auto base = func->args[0];
        auto exponent = func->args[1];
        std::optional<double> exp_val = to_numeric(exponent);

        if (!block_power_negation && exp_val && *exp_val < 0.0) {
            std::shared_ptr<Expression> new_exponent;
            if (auto exp_const = std::dynamic_pointer_cast<Constant>(exponent)) {
                new_exponent = std::make_shared<Constant>(-(exp_const->value));
            } else if (auto exp_rat = std::dynamic_pointer_cast<RationalNumber>(exponent)) {
                new_exponent = std::make_shared<RationalNumber>(-(exp_rat->num), exp_rat->den);
            } else {
                new_exponent = std::make_shared<Constant>(-(*exp_val));
            }
            auto new_power = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{base, new_exponent});
            auto new_division = std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{
                std::make_shared<Constant>(1.0), new_power
            });
            return constant_fold_recursive(new_division, false);
        }

        // 规则: (a/b)^c -> a^c / b^c, 但仅当 b 包含变量时
        if (auto div_base = std::dynamic_pointer_cast<Function>(base); div_base && div_base->op == "Divide") {
            auto numerator = div_base->args[0];
            auto denominator = div_base->args[1];

            if (has_symbols(denominator)) {
                auto new_num = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{numerator, exponent});
                auto new_den = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{denominator, exponent});
                auto new_division = std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{new_num, new_den});
                return constant_fold_recursive(new_division, false);
            }
        }

        if (exp_val) {
            if (*exp_val == 1.0) return base;
            if (*exp_val == 0.0) return std::make_shared<Constant>(1.0);
        }
        if (auto base_val = to_numeric(base)) {
            if (*base_val == 1.0) return std::make_shared<Constant>(1.0);
        }
    }

    // 化简 Add(...)
    if (func->op == "Add") {
        std::vector<std::shared_ptr<Expression>> flattened_args;
        for (const auto& arg : func->args) {
            if (auto sub = std::dynamic_pointer_cast<Function>(arg); sub && sub->op == "Add") {
                flattened_args.insert(flattened_args.end(), sub->args.begin(), sub->args.end());
            } else { flattened_args.push_back(arg); }
        }

        struct FractionalComponent {
            std::shared_ptr<Expression> num;
            std::shared_ptr<Expression> den;
        };
        std::vector<FractionalComponent> fractional_parts;
        std::vector<std::shared_ptr<Expression>> whole_parts;

        for (const auto& arg : flattened_args) {
            if (auto div = std::dynamic_pointer_cast<Function>(arg); div && div->op == "Divide") {
                fractional_parts.push_back({div->args[0], div->args[1]});
            } else if (auto neg = std::dynamic_pointer_cast<Function>(arg); neg && neg->op == "Negate" && neg->args.size() == 1) {
                if (auto div = std::dynamic_pointer_cast<Function>(neg->args[0]); div && div->op == "Divide") {
                    auto negated_num = std::make_shared<Function>("Negate", std::vector<std::shared_ptr<Expression>>{div->args[0]});
                    fractional_parts.push_back({negated_num, div->args[1]});
                } else {
                    whole_parts.push_back(arg);
                }
            } else if (auto abs_func = std::dynamic_pointer_cast<Function>(arg); abs_func && abs_func->op == "Abs" && abs_func->args.size() == 1) {
                if (auto div_inner = std::dynamic_pointer_cast<Function>(abs_func->args[0]); div_inner && div_inner->op == "Divide") {
                    auto new_num = std::make_shared<Function>("Abs", std::vector<std::shared_ptr<Expression>>{div_inner->args[0]});
                    auto new_den = std::make_shared<Function>("Abs", std::vector<std::shared_ptr<Expression>>{div_inner->args[1]});
                    fractional_parts.push_back({new_num, new_den});
                } else {
                    whole_parts.push_back(arg);
                }
            } else if (auto sign_func = std::dynamic_pointer_cast<Function>(arg); sign_func && sign_func->op == "Sign" && sign_func->args.size() == 1) {
                if (auto div_inner = std::dynamic_pointer_cast<Function>(sign_func->args[0]); div_inner && div_inner->op == "Divide") {
                    auto new_num = std::make_shared<Function>("Sign", std::vector<std::shared_ptr<Expression>>{div_inner->args[0]});
                    auto new_den = std::make_shared<Function>("Sign", std::vector<std::shared_ptr<Expression>>{div_inner->args[1]});
                    fractional_parts.push_back({new_num, new_den});
                } else {
                    whole_parts.push_back(arg);
                }
            }
            else if (auto power_func = std::dynamic_pointer_cast<Function>(arg); power_func && power_func->op == "Power" && power_func->args.size() == 2) {
                if (auto div_base = std::dynamic_pointer_cast<Function>(power_func->args[0]); div_base && div_base->op == "Divide") {
                    auto exponent = power_func->args[1];
                    auto new_num = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{div_base->args[0], exponent});
                    auto new_den = std::make_shared<Function>("Power", std::vector<std::shared_ptr<Expression>>{div_base->args[1], exponent});
                    fractional_parts.push_back({new_num, new_den});
                } else {
                    whole_parts.push_back(arg);
                }
            }
            else {
                whole_parts.push_back(arg);
            }
        }

        if (!fractional_parts.empty() && flattened_args.size() > 1) {
            std::map<std::string, std::shared_ptr<Expression>> unique_denominators_map;
            for (const auto& frac : fractional_parts) {
                unique_denominators_map[expression_to_string(frac.den)] = frac.den;
            }
            std::vector<std::shared_ptr<Expression>> unique_denominators;
            for (const auto& pair : unique_denominators_map) {
                unique_denominators.push_back(pair.second);
            }
            auto common_denominator = rebuild_expression_from_factors(unique_denominators);
            std::vector<std::shared_ptr<Expression>> new_numerator_terms;

            if (!whole_parts.empty()) {
                auto whole_sum = (whole_parts.size() == 1) ? whole_parts[0] : std::make_shared<Function>("Add", whole_parts);
                new_numerator_terms.push_back(std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{whole_sum, common_denominator}));
            }

            for (const auto& frac : fractional_parts) {
                std::vector<std::shared_ptr<Expression>> multiplier_factors;
                std::string current_den_str = expression_to_string(frac.den);
                for (const auto& d : unique_denominators) {
                    if (expression_to_string(d) != current_den_str) {
                        multiplier_factors.push_back(d);
                    }
                }
                if (multiplier_factors.empty()) {
                    new_numerator_terms.push_back(frac.num);
                } else {
                    auto multiplier = rebuild_expression_from_factors(multiplier_factors);
                    new_numerator_terms.push_back(std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{frac.num, multiplier}));
                }
            }

            auto final_numerator = std::make_shared<Function>("Add", new_numerator_terms);
            return constant_fold_recursive(std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{final_numerator, common_denominator}), false);
        } else {
            double sum = 0.0;
            std::map<std::string, double> coeffs;
            std::map<std::string, std::shared_ptr<Expression>> models;
            for (const auto& arg : flattened_args) {
                if (auto num = to_numeric(arg)) { sum += *num; continue; }
                double c=1.0; std::shared_ptr<Expression> term=arg;
                if (auto f=std::dynamic_pointer_cast<Function>(arg); f && f->op=="Negate" && f->args.size()==1) { term=f->args[0]; c=-1.0; }
                else if (f && f->op=="Multiply") {
                    double p=1.0; std::vector<std::shared_ptr<Expression>> s_args;
                    for(const auto& ma : f->args) { if(auto n=to_numeric(ma)) p*=*n; else s_args.push_back(ma); }
                    if(!s_args.empty()) { c=p; term=(s_args.size()==1)?s_args[0]:std::make_shared<Function>("Multiply",s_args); }
                }
                std::string key=expression_to_string(term);
                coeffs[key]+=c; models.try_emplace(key,term);
            }
            std::vector<std::shared_ptr<Expression>> new_args;
            for (auto const& [key, c] : coeffs) {
                if(c==0.0) continue;
                auto m=models[key];
                if(c==1.0) new_args.push_back(m);
                else if(c==-1.0) new_args.push_back(std::make_shared<Function>("Negate", std::vector<std::shared_ptr<Expression>>{m}));
                else new_args.push_back(std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{std::make_shared<Constant>(c), m}));
            }
            if (sum!=0.0 || new_args.empty()) new_args.push_back(std::make_shared<Constant>(sum));
            if (new_args.empty()) return std::make_shared<Constant>(0.0);
            if (new_args.size()==1) return new_args[0];
            func->args = new_args;
        }
    }

    if (func->op == "Multiply") {
        std::vector<std::shared_ptr<Expression>> flattened_args;
        for(const auto& arg : func->args) {
            if(auto sub = std::dynamic_pointer_cast<Function>(arg); sub && sub->op == "Multiply") {
                flattened_args.insert(flattened_args.end(), sub->args.begin(), sub->args.end());
            } else {
                flattened_args.push_back(arg);
            }
        }

        std::vector<std::shared_ptr<Expression>> numerator_factors;
        std::vector<std::shared_ptr<Expression>> denominator_factors;
        bool has_fractions = false;

        for(const auto& arg : flattened_args) {
            if (auto div = std::dynamic_pointer_cast<Function>(arg); div && div->op == "Divide") {
                has_fractions = true;
                get_factors(div->args[0], numerator_factors);
                get_factors(div->args[1], denominator_factors);
            } else {
                numerator_factors.push_back(arg);
            }
        }

        if (!has_fractions) {
            double product = 1.0;
            std::vector<std::shared_ptr<Expression>> non_numeric_args;
            for(const auto& arg : flattened_args) {
                if(auto num = to_numeric(arg)) {
                    product *= *num;
                } else {
                    non_numeric_args.push_back(arg);
                }
            }
            if (product == 0.0) return std::make_shared<Constant>(0.0);
            if (product != 1.0 || non_numeric_args.empty()) {
                non_numeric_args.insert(non_numeric_args.begin(), std::make_shared<Constant>(product));
            }
            if (non_numeric_args.empty()) return std::make_shared<Constant>(1.0);
            if (non_numeric_args.size() == 1) return non_numeric_args[0];

            func->args = non_numeric_args;
            return func;
        }

        auto new_num = rebuild_expression_from_factors(numerator_factors);
        auto new_den = rebuild_expression_from_factors(denominator_factors);

        return constant_fold_recursive(std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{new_num, new_den}), false);
    }

    if (func->op == "Divide" && func->args.size() == 2) {
        auto numerator = func->args[0];
        auto denominator = func->args[1];
        auto num_as_div = std::dynamic_pointer_cast<Function>(numerator);
        if (num_as_div && num_as_div->op != "Divide") num_as_div = nullptr;
        auto den_as_div = std::dynamic_pointer_cast<Function>(denominator);
        if (den_as_div && den_as_div->op != "Divide") den_as_div = nullptr;

        if (num_as_div || den_as_div) {
            std::shared_ptr<Expression> a = num_as_div ? num_as_div->args[0] : numerator;
            std::shared_ptr<Expression> b = num_as_div ? num_as_div->args[1] : std::make_shared<Constant>(1.0);
            std::shared_ptr<Expression> c = den_as_div ? den_as_div->args[0] : denominator;
            std::shared_ptr<Expression> d = den_as_div ? den_as_div->args[1] : std::make_shared<Constant>(1.0);
            auto new_num = std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{a, d});
            auto new_den = std::make_shared<Function>("Multiply", std::vector<std::shared_ptr<Expression>>{b, c});
            return constant_fold_recursive(std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{new_num, new_den}), false);
        }

        auto factored_num = try_factor_expression(numerator);
        auto factored_den = try_factor_expression(denominator);
        auto expanded_num = expand_powers(factored_num);
        auto expanded_den = expand_powers(factored_den);
        std::vector<std::shared_ptr<Expression>> num_factors, den_factors;
        get_factors(expanded_num, num_factors);
        get_factors(expanded_den, den_factors);
        std::map<std::string, int> num_factor_counts;
        for(const auto& f : num_factors) num_factor_counts[expression_to_string(f)]++;
        std::map<std::string, std::shared_ptr<Expression>> unique_factors;
        for (const auto& f : num_factors) unique_factors[expression_to_string(f)] = f;
        for (const auto& f : den_factors) unique_factors[expression_to_string(f)] = f;
        std::vector<std::shared_ptr<Expression>> rem_den_factors;
        for(const auto& f : den_factors) {
            std::string key = expression_to_string(f);
            if(num_factor_counts.count(key) && num_factor_counts[key] > 0) {
                num_factor_counts[key]--;
            } else { rem_den_factors.push_back(f); }
        }
        std::vector<std::shared_ptr<Expression>> rem_num_factors;
        for (auto const& [key, count] : num_factor_counts) {
            for (int i = 0; i < count; ++i) { rem_num_factors.push_back(unique_factors[key]); }
        }
        if (rem_num_factors.size() == num_factors.size() && rem_den_factors.size() == den_factors.size()) {
            return func;
        }
        auto new_num = rebuild_expression_from_factors(rem_num_factors);
        auto new_den = rebuild_expression_from_factors(rem_den_factors);
        if (auto den_const = to_numeric(new_den)) {
            if (*den_const == 1.0) return new_num;
            if (*den_const == 0.0) return func;
        }
        return constant_fold_recursive(std::make_shared<Function>("Divide", std::vector<std::shared_ptr<Expression>>{new_num, new_den}), false);
    }

    return func;
}



    } // namespace CAS::ConstantFolding
    std::shared_ptr<Expression> rewrite_powers_for_cpp_engine(const std::shared_ptr<Expression>& ast) {
        // 这些辅助函数定义在上面的匿名命名空间中，在此处依然可见
        auto rewritten_ast = rewrite_powers_recursive(ast);
        return convert_rationals_to_constants_recursive(rewritten_ast);
    }

    // --- 文件路径: src/CAS/ConstantFolding.cpp ---
    // 这是公共接口函数，位于匿名命名空间之外

    std::shared_ptr<Expression> constant_fold(const std::shared_ptr<Expression>& ast) {
        // --- 步骤 1 (新增): 标准化 Log, Tan 等函数 ---
        auto standardized_functions_ast = standardize_functions_recursive(ast);

        // 步骤 2: 将根式 (Sqrt, Root) 转换为幂函数形式
        auto standardized_roots_ast = rewrite_roots_recursive(standardized_functions_ast);

        // 步骤 3: 将浮点数常量转换为内部有理数表示，以便精确计算
        auto preprocessed_ast = convert_constants_to_rationals_recursive(standardized_roots_ast);

        // 步骤 4: 执行核心的代数化简和常量折叠
        auto simplified_ast = constant_fold_recursive(preprocessed_ast);

        // 步骤 5: 将表达式重写为 C++ 引擎友好的形式 (例如，处理分数次幂的定义域)
        return rewrite_powers_for_cpp_engine(simplified_ast);
    }
}