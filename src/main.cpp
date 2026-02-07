#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include "../include/CAS/RPN/FormulaNormalizer.h"
#include "../include/graph/GeoGraph.h"

using namespace CAS::Parser;

#define C_RESET   "\033[0m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"

void run_norm_test(const std::string& input, GeometryGraph& graph) {
    std::string actual = FormulaNormalizer::Normalize(input, graph);
    std::cout << "  " << std::left << std::setw(25) << ("[" + input + "]")
              << " -> " << C_GREEN << "[" << actual << "]" << C_RESET << std::endl;
}

int main() {
    GeometryGraph graph;

    std::cout << C_BOLD << C_CYAN << "=== FORMULA NORMALIZER: DECIMAL & SIGN FOLDING ===\n" << C_RESET;

    // 1. 小数规范化测试
    std::cout << "\n[DECIMAL NORMALIZATION]\n";
    run_norm_test(".5", graph);             // 0.5
    run_norm_test("5.", graph);             // 5.0
    run_norm_test(".123 + 45.", graph);     // 0.123+45.0
    run_norm_test("sin(.5)", graph);        // sin(0.5)

    // 2. 负号与小数组合
    std::cout << "\n[SIGNED DECIMALS]\n";
    run_norm_test("-.5", graph);            // -0.5
    run_norm_test("- .5", graph);           // -0.5 (去空格)
    run_norm_test("1 - .5", graph);         // 1-0.5
    run_norm_test("- - .5", graph);         // 0.5 (正号抵消)
    run_norm_test("2 ^ -.5", graph);        // 2^-0.5

    // 3. 极端符号链
    std::cout << "\n[EXTREME SIGN FOLDING]\n";
    run_norm_test("1----5", graph);         // 1+5 (4个负号)
    run_norm_test("x+++++y", graph);        // x+y
    run_norm_test("a + - + - b", graph);    // a+b (2个负号)
    run_norm_test("1 - - - 1", graph);      // 1-1 (3个负号)

    // 4. 函数与赋值
    std::cout << "\n[FUNCTIONS & EQUATIONS]\n";
    run_norm_test("f( x ) = .5 * x", graph); // f(x)=0.5*x
    run_norm_test("sin(- - .1)", graph);     // sin(0.1)

    std::cout << "\n" << C_BOLD << C_YELLOW << "===============================================\n" << C_RESET;

    return 0;
}