#include "huge_templates.hpp"
#include <iostream>
int main() {
    std::cout << "[*] 运行传统 HPP 模式..." << std::endl;
    std::cout << "结果: " << call_all_templates(1.0) << std::endl;
    return 0;
}
