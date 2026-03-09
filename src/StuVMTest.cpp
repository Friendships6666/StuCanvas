#include "StuVM/StuCompiler.hpp"

int main() {
    StuVM::StuCompiler compiler;

    // 1. 定义要输入的 C++ 代码
    const std::string myCode = R"(
        #include <cstdio>
        #include <vector>
        #include <numeric>

        extern "C" void stu_compute() {
            // 使用 std::vector 触发内存分配和可能的自动向量化
            std::vector<int> v(32, 1);
            int sum = 0;
            #pragma clang loop vectorize(enable)
            for(int i=0; i<32; i++) sum += v[i];

            printf("Result: %d\n", sum);
        }

        int main() {
            stu_compute();
            return 0;
        }
    )";

    std::cout << "Step 1: Compiling and Linking..." << std::endl;
    if (compiler.compileAndLink(myCode, "stu_final.elf")) {
        std::cout << "Step 2: Analyzing Instruction Semantics..." << std::endl;
        // 2. 分析生成的 ELF 文件
        compiler.analyzeElf("stu_final.elf");
    } else {
        std::cerr << "Compilation Failed!" << std::endl;
        return 1;
    }

    return 0;
}