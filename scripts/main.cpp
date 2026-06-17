#include "huge_templates.hpp"
#include <iostream>

int main() {
    std::cout << "[*] 正在执行百万级模板函数调用..." << std::endl;
    double result = call_all_templates(1.0);
    std::cout << "[+] 计算完毕。累加和为: " << result << std::endl;
    return 0;
}
