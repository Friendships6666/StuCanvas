/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include <iostream>
#include <string>
#include "typst_c.hpp" // 包含 FFI 对齐与 RAII 包装头文件

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "🚀 [C++ Main] Starting StuCanvas-Typst Compiler Test..." << std::endl;
    std::cout << "====================================================" << std::endl;

    // 1. 构建测试用的 Typst 排版标记文本
    // 包含：标准几何图形、多维变换矩阵、线性渐变、圆角容器、实例化字形以及嵌套 Group。
    const char *typst_markup = R"(
        #set page(width: 250pt, height: 250pt, margin: 15pt)

        // 1. 测试标准矩形
        #place(dx: 10pt, dy: 15pt)[
            #rect(width: 80pt, height: 40pt, fill: rgb("#00599c"))
        ]

        // 2. 测试圆/椭圆（底层会自动编译退化为 4 段三阶贝塞尔曲线，不引起编译崩溃）
        #place(dx: 120pt, dy: 20pt)[
            #circle(radius: 20pt, fill: gradient.linear(blue, red))
        ]

        // 3. 测试自由路径与描边（虚线、线帽、斜角）
        #place(dx: 15pt, dy: 90pt)[
            #curve(
                stroke: (paint: rgb("#ff7700"), thickness: 4pt, cap: "round", join: "miter", dash: (array: (8pt, 4pt), phase: 2pt)),
                curve.move((0pt, 0pt)),
                curve.line((60pt, 0pt)),
                curve.cubic((80pt, 20pt), (20pt, 40pt), (100pt, 60pt)),
                curve.close()
            )
        ]

        // 4. 测试实例化文字排版（支持字形几何的 FFI 单实例高效去重）
        #place(dx: 150pt, dy: 100pt)[
            #text(size: 28pt, fill: rgb("#111111"))[Hello]
        ]

        // 5. 测试具有剪裁蒙版（Clip）的群组（Group）嵌套
        #place(dx: 50pt, dy: 170pt)[
            #rect(width: 150pt, height: 50pt, radius: 8pt, stroke: 2pt + green, fill: rgb("#eaeaea"))[
                #set align(center + horizon)
                #text(size: 14pt, fill: red)[StuCanvas NLE Engine]
            ]
        ]
    )";

    struct test {
      float a;
      double b;
      char c;
   };
   test Test;
 

   std::cout << Test.a << std::endl;
    // 💡 提示：该路径必须指向包含 .ttf / .otf 字体文件的实际目录，以供 Typst 排版时加载字体。
    // 在 Linux 下通常可以指向 "/usr/share/fonts" 或本地项目的 "fonts" 文件夹。
    const char* fonts_directory = "fonts";

    std::cout << "[C++ Main] Compiling Typst markup on Rust side..." << std::endl;

    // 2. 调用高层托管的 compile 函数
    // 利用 C++20/C++23 移动语义和 Result 析构管理器保护堆空间
    auto result = StuCanvas::compile(typst_markup, fonts_directory);

    // 3. 诊断与解析结果打印
    if (result.is_success()) {
        std::cout << "\n✅ [C++ Main] Typst compilation succeeded!" << std::endl;
        std::cout << "[C++ Main] Handing over Outline (Address: " << result.get_outline() << ") to Rust debug printer...\n" << std::endl;

        // 调用 Rust 侧的极致可视化打印函数，检查各几何体尺寸、对齐对顶角、仿射矩阵及 stops 空间
        ::stucanvas_print_detailed_outline(result.get_outline());

        std::cout << "\n[C++ Main] Debug print finished." << std::endl;
    } else {
        std::cerr << "\n❌ [C++ Main] Typst compilation failed!" << std::endl;
        std::cerr << "-------------------- ERROR LOG --------------------" << std::endl;
        std::cerr << result.get_error() << std::endl;
        std::cerr << "---------------------------------------------------" << std::endl;
    }

    std::cout << "\n[C++ Main] Exiting main(). RAII UniquePtrs will now auto-deallocate all FFI memory heaps." << std::endl;
    // 💡 生命周期自愈：当 result 离开生命周期，其内部的 OutlineUniquePtr 会在 C++ 侧自动析构，
    // 并调用 Rust 侧 stucanvas_free_outline 回收所有 instance、geometry、stops、dash 堆内存 [25.1]。
    return 0;
}
