#!/usr/bin/env python3
import argparse
import sys

def generate_benchmarks(num_templates):
    print(f"[*] 开始生成双 DAG 架构 & C++23 模块对比基准测试...")
    print(f"[-] 目标模板函数数量: {num_templates:,} 个")

    # 文件名定义
    hpp_name = "huge_templates.hpp"
    inst_name = "instantiations.cpp"
    main_header_name = "main_header.cpp"

    cppm_name = "huge_templates.cppm"
    main_module_name = "main_module.cpp"

    # =========================================================================
    # 1. 生成传统 Header 模式文件 (保持 inline，以防止传统头文件链接报错)
    # =========================================================================
    print(f"[-] 正在生成传统 HPP: {hpp_name}...")
    with open(hpp_name, "w", encoding="utf-8", newline="\n") as hpp:
        hpp.write("#pragma once\n#ifndef HUGE_TEMPLATES_HPP\n#define HUGE_TEMPLATES_HPP\n\n")
        block = []
        for i in range(num_templates):
            block.append(f"template <typename T> T func_{i}(T x) {{ return x + {i}; }}\n")
            if len(block) >= 20000:
                hpp.write("".join(block))
                block = []
        if block: hpp.write("".join(block))

        hpp.write("\n#ifdef USE_EXTERN_TEMPLATES\n")
        block = []
        for i in range(num_templates):
            block.append(f"extern template double func_{i}<double>(double);\n")
            if len(block) >= 20000:
                hpp.write("".join(block))
                block = []
        if block: hpp.write("".join(block))
        hpp.write("#endif\n\n")

        hpp.write("inline double call_all_templates(double x) {\n    double sum = 0.0;\n")
        block = []
        for i in range(num_templates):
            block.append(f"    sum += func_{i}<double>(x);\n")
            if len(block) >= 20000:
                hpp.write("".join(block))
                block = []
        if block: hpp.write("".join(block))
        hpp.write("    return sum;\n}\n#endif\n")

    with open(inst_name, "w", encoding="utf-8", newline="\n") as inst:
        inst.write('#include "huge_templates.hpp"\n\ntemplate double func_0<double>(double);\n')
        block = []
        for i in range(1, num_templates):
            block.append(f"template double func_{i}<double>(double);\n")
            if len(block) >= 20000:
                inst.write("".join(block))
                block = []
        if block: inst.write("".join(block))

    with open(main_header_name, "w", encoding="utf-8", newline="\n") as mh:
        mh.write('#include "huge_templates.hpp"\n#include <iostream>\n')
        mh.write('int main() {\n    std::cout << "[*] 运行传统 HPP 模式..." << std::endl;\n')
        mh.write('    std::cout << "结果: " << call_all_templates(1.0) << std::endl;\n    return 0;\n}\n')

    # =========================================================================
    # 2. 生成 C++23 Module 模式文件 (❌ 移除了 call_all_templates 的 inline 关键字)
    # =========================================================================
    print(f"[-] 正在生成 C++23 模块接口: {cppm_name}...")
    with open(cppm_name, "w", encoding="utf-8", newline="\n") as cppm:
        cppm.write("export module huge_templates;\n\n")
        cppm.write("export {\n")

        block = []
        for i in range(num_templates):
            block.append(f"    template <typename T> T func_{i}(T x) {{ return x + {i}; }}\n")
            if len(block) >= 20000:
                cppm.write("".join(block))
                block = []
        if block: cppm.write("".join(block))

        # 💡 这里去掉了 inline 关键字
        cppm.write("\n    double call_all_templates(double x) {\n        double sum = 0.0;\n")
        block = []
        for i in range(num_templates):
            block.append(f"        sum += func_{i}<double>(x);\n")
            if len(block) >= 20000:
                cppm.write("".join(block))
                block = []
        if block: cppm.write("".join(block))
        cppm.write("        return sum;\n    }\n}\n")

    with open(main_module_name, "w", encoding="utf-8", newline="\n") as mm:
        mm.write('import huge_templates;\n#include <iostream>\n\n')
        mm.write('int main() {\n    std::cout << "[*] 运行 C++23 模块模式..." << std::endl;\n')
        mm.write('    std::cout << "结果: " << call_all_templates(1.0) << std::endl;\n    return 0;\n}\n')

    print("\n[+] 所有测试文件生成完毕！")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="巨型百万级 HPP 与 C++23 模块对比生成器")
    parser.add_argument("-n", "--num", type=int, default=10000, help="模板函数数量 (默认: 1000000)")
    args = parser.parse_args()

    generate_benchmarks(args.num)