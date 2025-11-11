// --- 文件路径: src/CAS/AST/JsonAdapter.cpp ---

#include "../../../include/CAS/AST/JsonAdapter.h"
#include "../../../include/CAS/symbolic/ConstantFolding.h"

#include "../../../pch.h"
namespace CAS::JsonAdapter {
namespace {
    using namespace CAS::ConstantFolding;

    std::shared_ptr<Expression> build_ast_from_element(simdjson::dom::element node) {
        switch (node.type()) {
            case simdjson::dom::element_type::DOUBLE:
            case simdjson::dom::element_type::INT64:
            case simdjson::dom::element_type::UINT64:

                return std::make_shared<Constant>(*node.get<double>());

            case simdjson::dom::element_type::STRING: {
                std::string_view sv = *node.get<std::string_view>();
                if (sv == "ExponentialE") {
                    return std::make_shared<Constant>(std::exp(1.0)); // 使用 std::exp(1.0)
                }
                return std::make_shared<Symbol>(std::string(sv));
            }

            // --- 新增规则: 处理 {"num": "..."} 格式 ---
            case simdjson::dom::element_type::OBJECT: {
                simdjson::dom::object obj = node.get<simdjson::dom::object>();
                auto num_field_result = obj["num"];
                // 检查对象是否包含 "num" 键
                if (!num_field_result.error()) {
                    simdjson::dom::element num_value = num_field_result.value();
                    // 检查 "num" 键对应的值是否为字符串
                    if (num_value.is_string()) {
                        std::string_view num_str_view = *num_value.get<std::string_view>();
                        try {
                            // 将字符串转换为 double 并创建 Constant 节点
                            double value = std::stod(std::string(num_str_view));
                            return std::make_shared<Constant>(value);
                        } catch (...) {
                            throw std::runtime_error("在 'num' 对象中发现无效的数字字符串。");
                        }
                    }
                }

                throw std::runtime_error("在AST中发现不支持的JSON对象。");
            }


            case simdjson::dom::element_type::ARRAY: {
                simdjson::dom::array arr = node.get<simdjson::dom::array>();
                if (arr.size() == 0) throw std::runtime_error("AST 数组节点不能为空");
                std::string op(*arr.at(0).get<std::string_view>());
                if (op == "Rational" && arr.size() == 3) {
                    long long num = arr.at(1).get<long long>();
                    long long den = arr.at(2).get<long long>();
                    return std::make_shared<RationalNumber>(num, den);
                }
                std::vector<std::shared_ptr<Expression>> args;
                args.reserve(arr.size() - 1);
                for (size_t i = 1; i < arr.size(); ++i) {
                    args.push_back(build_ast_from_element(arr.at(i)));
                }
                return std::make_shared<Function>(op, args);
            }
            default:
                throw std::runtime_error("无效的AST JSON节点类型");
        }
    }
    nlohmann::json ast_to_json_node(const std::shared_ptr<Expression>& node) {
        if (auto con = std::dynamic_pointer_cast<Constant>(node)) return con->value;
        if (auto sym = std::dynamic_pointer_cast<Symbol>(node)) return sym->name;
        if (auto rat = std::dynamic_pointer_cast<RationalNumber>(node)) {
            return {"Rational", rat->num, rat->den};
        }
        if (auto func = std::dynamic_pointer_cast<Function>(node)) {
            nlohmann::json j = nlohmann::json::array();
            j.push_back(func->op);
            for (const auto& arg : func->args) j.push_back(ast_to_json_node(arg));
            return j;
        }
        return nullptr;
    }
}
// ====================================================================
//  MODIFIED: parse_json_to_ast_simdjson
//  - 此函数现在要求输入的 JSON 必须是 ["Equal", "y", <expression>] 的格式。
//  - 它会自动将 y = f(x) 形式的方程转换为 y - f(x) 的 AST 结构，
//    以便后续的符号计算和绘图。
// ====================================================================
std::shared_ptr<Expression> parse_json_to_ast_simdjson(const std::string& json_string) {
    thread_local simdjson::dom::parser parser;
    try {
        simdjson::dom::element root = parser.parse(json_string);

        // 检查根节点是否为 ["Equal", "y", <expression>] 格式
        if (root.type() == simdjson::dom::element_type::ARRAY) {
            simdjson::dom::array arr = root.get<simdjson::dom::array>();
            if (arr.size() == 3 &&
                arr.at(0).is_string() && *arr.at(0).get<std::string_view>() == "Equal" &&
                arr.at(1).is_string() && *arr.at(1).get<std::string_view>() == "y")
            {
                // 格式匹配: y = f(x)
                // 移项得到: y - f(x) = 0
                // 我们将处理表达式 y - f(x)

                // 1. 提取右侧表达式 f(x)
                simdjson::dom::element rhs_element = arr.at(2);
                // 2. 将 f(x) 构建为 AST
                std::shared_ptr<Expression> rhs_ast = build_ast_from_element(rhs_element);

                // 3. 创建代表 "y" 的符号节点
                auto y_symbol = std::make_shared<Symbol>("y");

                // 4. 创建代表 -f(x) 的节点，即 ["Negate", f(x)]
                auto negated_rhs_ast = std::make_shared<Function>("Negate", std::vector<std::shared_ptr<Expression>>{rhs_ast});

                // 5. 构建最终的 AST: ["Add", "y", ["Negate", f(x)]]，即 y - f(x)
                return std::make_shared<Function>("Add", std::vector<std::shared_ptr<Expression>>{y_symbol, negated_rhs_ast});
            }
        }

        // 如果格式不匹配，则抛出错误
        throw std::runtime_error("输入JSON格式无效。根节点必须是 [\"Equal\", \"y\", <expression>]");

    } catch (const simdjson::simdjson_error& e) {
        throw std::runtime_error("simdjson 解析失败: " + std::string(e.what()));
    }
}
std::string ast_to_json_string(const std::shared_ptr<Expression>& ast) {
    return ast_to_json_node(ast).dump(2);
}
} // namespace CAS::JsonAdapter