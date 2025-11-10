// --- 文件路径: src/CAS/ConstantFolding.cpp ---

#include "../../../include/CAS/symbolic/ConstantFolding.h" // <-- 更新 include 路径
#include "../../../pch.h"

namespace CAS::ConstantFolding {

// 辅助函数：尝试将任何表达式节点转换为一个double值。
// 如果节点不是数字（或不能被计算），返回 std::nullopt。
std::optional<double> to_numeric(const std::shared_ptr<Expression>& expr) {
    if (auto con = std::dynamic_pointer_cast<Constant>(expr)) {
        return con->value;
    }
    if (auto rat = std::dynamic_pointer_cast<RationalNumber>(expr)) {
        return rat->to_double();
    }
    return std::nullopt;
}

std::shared_ptr<Expression> constant_fold(const std::shared_ptr<Expression>& ast) {
    auto func = std::dynamic_pointer_cast<Function>(ast);
    if (!func) {
        return ast; // 基础情况
    }

    // 步骤 1: 自底向上，递归化简所有子节点
    for (auto& arg : func->args) {
        arg = constant_fold(arg);
    }

    // 步骤 2: 检查是否所有子节点现在都是可计算的数字
    bool all_args_are_numeric = true;
    std::vector<double> numeric_args;
    for (const auto& arg : func->args) {
        auto numeric_val = to_numeric(arg);
        if (numeric_val) {
            numeric_args.push_back(*numeric_val);
        } else {
            all_args_are_numeric = false;
            break;
        }
    }

    // 步骤 3: 如果所有子节点都是数字，则对整个函数求值
    if (all_args_are_numeric && !numeric_args.empty()) {
        try {
            if (func->op == "Add") {
                double sum = 0.0;
                for (double val : numeric_args) sum += val;
                return std::make_shared<Constant>(sum);
            }
            if (func->op == "Multiply") {
                double product = 1.0;
                for (double val : numeric_args) product *= val;
                return std::make_shared<Constant>(product);
            }
            if (func->op == "Divide" && numeric_args.size() == 2 && numeric_args[1] != 0.0) {
                return std::make_shared<Constant>(numeric_args[0] / numeric_args[1]);
            }
            if (func->op == "Negate" && numeric_args.size() == 1) {
                return std::make_shared<Constant>(-numeric_args[0]);
            }
            if (func->op == "Tan" && numeric_args.size() == 1) {
                return std::make_shared<Constant>(std::tan(numeric_args[0]));
            }
        } catch (...) { /* 计算出错则放弃求值 */ }
    }

    // 步骤 4: 如果不能完全求值，则对 Add 和 Multiply 进行部分化简
    if (func->op == "Add") {
        std::vector<std::shared_ptr<Expression>> non_numeric_args;
        double sum = 0.0;
        for (const auto& arg : func->args) {
            auto numeric_val = to_numeric(arg);
            if (numeric_val) {
                sum += *numeric_val;
            } else {
                non_numeric_args.push_back(arg);
            }
        }
        func->args = non_numeric_args;
        if (sum != 0.0 || func->args.empty()) {
            func->args.push_back(std::make_shared<Constant>(sum));
        }
        if (func->args.empty()) return std::make_shared<Constant>(0.0);
        if (func->args.size() == 1) return func->args[0];
    }

    if (func->op == "Multiply") {
        std::vector<std::shared_ptr<Expression>> non_numeric_args;
        double product = 1.0;
        for (const auto& arg : func->args) {
            auto numeric_val = to_numeric(arg);
            if (numeric_val) {
                product *= *numeric_val;
            } else {
                non_numeric_args.push_back(arg);
            }
        }
        if (product == 0.0) return std::make_shared<Constant>(0.0);
        func->args = non_numeric_args;
        if (product != 1.0 || func->args.empty()) {
            func->args.push_back(std::make_shared<Constant>(product));
        }
        if (func->args.empty()) return std::make_shared<Constant>(1.0);
        if (func->args.size() == 1) return func->args[0];
    }

    return func;
}

} // namespace CAS::ConstantFolding