/***************************************************************************
* Copyright (c) 2026 Multilingual Vector & Printer Test                    *
***************************************************************************/

#include "typst_c.hpp"
#include <iostream>
// 多语言测试模板：包含中、韩、阿（从右往左连写）三种复杂的矢量文本
static const char* MULTILINGUAL_TEMPLATE = R"(
#set page(width: 250pt, height: 140pt, fill: none, margin: 10pt)
// 依次指定备用字体，确保中、韩、阿文字形都能被成功检索
#set text(font: ("Noto Sans CJK SC", "Noto Sans CJK KR", "Noto Sans Arabic", "Amiri"))

#align(center + horizon)[
  #text(size: 13pt, fill: rgb("1d2d44"))[*Multilingual Vector Render*]
  #v(4pt)
  #text(size: 11pt, fill: rgb("0077b6"))[中文测试：天下大同]
  #v(3pt)
  #text(size: 11pt, fill: rgb("0096c7"))[한국어 테스트: 안녕하세요]
  #v(3pt)
  // 阿拉伯文测试（السلام عليكم，意为：祝你平安，注意：在底层它会自动连写）
  #text(size: 11pt, fill: rgb("03045e"))[العربية: السلام عليكم]
]
)";

int main() {
    const char* fonts_dir = "fonts";

    std::cout << "====================================================" << std::endl;
    std::cout << "     Vulkan C++ 端 多语言矢量渲染与深度打印测试     " << std::endl;
    std::cout << "====================================================\n" << std::endl;

    // 1. 调用编译
    std::cout << "[Step 1] 正在进行【中、韩、阿】多语言高精度排版编译..." << std::endl;
    auto result = StuCanvas::compile(MULTILINGUAL_TEMPLATE, fonts_dir);

    if (!result) {
        std::cerr << "❌ [Error] 编译失败！请确保 fonts 目录下存在支持中/韩/阿文的字体！" << std::endl;
        std::cerr << result.get_error() << std::endl;
        return 1;
    }

    // 2. 验证去重效果 [3.2.1]
    std::cout << "▲ 编译成功！" << std::endl;
    std::cout << "  * 共享几何池中去重字形数量 (Unique Geometries): " << result->geometries.len << std::endl;
    std::cout << "  * 实例化渲染指令总数 (Total Draw Instances): " << result->instances.len << std::endl;

    // 3. 【核心需求】：直接调用 Rust 导出的高精度深度打印器
    // 它会递归拆解并打印出中、韩、阿文字形最底层的二阶/三阶贝塞尔曲线控制点 [3.2.1]
    std::cout << "\n[Step 2] 正在调用 Rust 物理打印器展开所有的字形曲线..." << std::endl;
    ::stucanvas_print_detailed_outline(result.get_outline());

    std::cout << "\n▲ [Step 3] 物理测试全部结束，C++ 作用域闭合，内存自动回收。" << std::endl;
    return 0; // result 离开生命周期，析构函数自动安全释放全部内存！
}