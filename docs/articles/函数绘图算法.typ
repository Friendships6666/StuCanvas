// --- 页面与全局排版设置 ---
#set page(
  paper: "a4",
  margin: (top: 3cm, bottom: 3cm, left: 2.5cm, right: 2.5cm),
  header: align(right)[
    #text(8pt, fill: luma(120))[探究科学可视化工具中的函数绘图算法]
  ],
  footer: [
    // 保持自 Typst 0.11 起的 #context 规范
    #align(center)[#text(9pt)[#context counter(page).display()]]
  ]
)

// 设置中英文字体结合与两端对齐
#set text(
  font: ("New Computer Modern", "Noto Serif CJK SC"),
  size: 11pt,
  lang: "zh"
)
#set par(
  justify: true,
  leading: 0.7em,
  first-line-indent: 2em
)

// --- 论文标题与作者信息 ---
#align(center)[
  #v(1em)
  #text(18pt, weight: "bold")[探究科学可视化工具中的函数绘图算法]
  #v(1.2em)
  #text(11pt)[
    *Friendships666* \
    #text(9pt, fill: luma(80))[\<tianfs6x6\@gmail.com\>]
  ]
  #v(1.5em)
]

// --- 摘要区（中立客观的技术对比视点） ---
#align(center)[
  #block(width: 90%)[
    #set par(first-line-indent: 0pt)
    #set text(size: 9.5pt)
    #rect(stroke: 0.5pt + luma(150), inset: 12pt, radius: 2pt)[
      #align(left)[
        *摘要：* 本文探讨了科学可视化中各种常见的函数绘图算法，分析了网格采样法与区间算术法等经典方案的优缺点，并探讨了一种结合区间算术与差分进化算法的混合改良方案。通过对各算法原理与 C++ 实现的客观拆解，本文对比了它们在渲染精度、计算效率与开发难度上的权衡，为相关可视化工具的算法选型提供参考。

        *关键词：* 科学可视化；函数绘图
      ]
    ]
  ]
]

#v(1.5em)

// --- 1. 引言（平等对待每种算法的教程风格） ---
= 1. 引言 <intro>

在科学计算、工程设计以及数字化教育蓬勃发展的今天，*函数绘图*（Function Plotting）已经成为科学可视化（Scientific Visualization）中不可或缺的基石。无论是供学生直观探索几何定理的教学软件（如 GeoGebra、Desmos），还是承载重工业工程计算与仿真分析的大型系统（如 MATLAB、Mathematica），如何将抽象的数学公式清晰、准确地呈现在屏幕上，是科学可视化软件最基础的能力。

然而，函数曲线的绘制并非一个简单的事情，受限于计算机世界本身是离散的（如有限的像素分辨率与离散的计算网格），而函数曲线是连续的。这种“离散与连续”的本质冲突，在实际图形渲染中引发了几个普遍而棘手的问题：

首先是*信息丢失与走样（Aliasing）*。当函数在局部发生急剧波动、或者曲线线段极其微小时，固定的网格采样率往往会漏掉这些转折特征，在屏幕上表现为曲线莫名断开、甚至形态严重失真；

其次是*不连续点与极值的处理*。在面对诸如 $1/x$ 等包含渐近线或跳跃间断点的公式时，朴素的像素连线算法极易在不连续的区间之间绘制出错误的“虚假连接线”，或者在奇点处因数值溢出而引发程序崩溃；

最后是*高保真度与交互效率的权衡*。在保证绘图窗口能实时平移和缩放的同时，如何确保在屏幕边缘始终呈现出丝滑、不发虚的连续线条，直接考验着底层绘图引擎的架构设计。

为了应对这些挑战，不同的可视化工具在开发过程中，分别在“开发复杂度”、“计算运行效率”与“拓扑严谨性”这三个维度上做出了不同的侧重与算法折衷。本教程将一视同仁地介绍三种具有代表性的算法路径：

首先，我们将探讨传统的*网格采样法*。它以极高的实现效率、较低的代码复杂度以及对现代 GPU 硬件的天然友好性，成为了常规绘图场景中应用最广的行业标准方案，但也需要在面对高频振荡等边界情况时承担走样的风险。

其次，我们将分析基于*区间算术（Interval Arithmetic）*的剖分算法。这种方法通过将传统单点计算扩展为区间计算，为解决漏画、断线问题提供了数学拓扑保障，但它也伴随着高计算开销以及低分辨率下边缘呈现锯齿感的技术折衷。

最后，我们将探讨一种将*区间算术与差分进化（Differential Evolution）相结合的混合寻优算法*。该方案尝试通过引入无导数辅助的随机寻优手段，在保留区间算法安全边界的同时，提升最终线条的视觉平滑度，但相应的，这也增加了系统实现的复杂度和并行化设计的门槛。

本篇文章将以客观、严谨的技术教程视角，逐一拆解这三种算法的数学原理与C++代码实现。我们的目的不是去证明某一种算法是“最完美的终极方案”，而是希望通过对它们运行机制的深度对比，帮助读者在开发自己的科学可视化工具时，能够根据具体的应用场景做出最合理的算法折衷与架构选择。


#v(1em)
#align(center)[
  #block(width: 95%)[
    #set text(size: 9.5pt)
    #table(
      columns: (1.2fr, 2.8fr),
      align: (left + horizon, left + horizon),
      // 经典的学术三线表或淡雅线条风格
      stroke: (x, y) => if y == 0 { (bottom: 1.5pt + black, top: 1.5pt + black) } else if y == 4 { (bottom: 1.5pt + black) } else { (bottom: 0.5pt + luma(200)) },
      fill: (x, y) => if y == 0 { luma(245) } else { none },

      table.header(
        [*典型复杂函数*], [*渲染与绘制的主要难点*]
      ),

      [$f(x) = sin(1/x)$], [
        *高频振荡：* 在原点 $x = 0$ 附近，函数的振荡频率趋于无穷大。常规的等间距采样由于无法满足采样定理，会在此区域丢失大量波形，产生严重的走样现象（表现为虚假的干涉条纹或散乱噪点）。
      ],

      [$f(x) = 1/x$], [
        *无穷奇点与渐近线：* 在自变量跨越 $x = 0$ 时，函数值从 $+oo$ 瞬间突变到 $-oo$。朴素的插值连线算法如果缺乏越界判定，往往会错误地绘制出一条穿过 $x = 0$、连接正负无穷的垂直虚假连线。
      ],

      [$f(x) = floor(x)$], [
        *跳跃间断点：* 函数值在每一个整数节点处发生离散阶跃。绘图引擎在处理此类突变边界时，若仅进行简单的点对点采样，会将原本不连续的阶梯边缘错误地绘制为倾斜的连续渐变线。
      ],

      [$y^2 - x^3 - x^2 = 0$], [
        *自相交与奇点：* 曲线在原点 $(0,0)$ 处发生自我相交。普通的局部逼近或追踪算法（如牛顿迭代、切线流算法）在此类多支路交汇点处极易遭遇导数未定义或搜索方向迷失，从而导致连线混乱或断裂。
      ]
    )
    #v(-0.5em)
    #align(center)[#text(8.5pt, fill: luma(100))[*表 1.1：* 经典函数曲线在离散计算环境下的渲染难点对照表]]
  ]
]
#v(1em)



#v(1em)
= 2. 显函数的离散采样法：回归高中数学的“描点法” <explicit-sampling>

当我们谈论如何在屏幕上绘制一条函数曲线时，最直观、最容易想到的算法，其实早在高中数学的课堂上就已经学过——那就是经典的*“描点法”*。

在手绘函数图像时，我们的操作步骤通常如下：
1. 在自变量 $x$ 的区间内，挑选几个有代表性的数值（例如 $-2, -1, 0, 1, 2$）；
2. 计算出对应的因变量 $y$ 的值；
3. 在坐标纸上把这些点 $(x, y)$ 描出来；
4. 用平滑的线条（或折线段）将这些相邻的点依次连接起来。

在计算机世界里，这一物理过程被转化为*“显函数的均匀离散采样算法”*。所谓显函数（Explicit Function），是指因变量 $y$ 可以用自变量 $x$ 的显式解析式直接表达的函数，即 $y = f(x)$。

== 2.1 算法执行逻辑：以 $y = x^2$ 为例

假设我们需要在坐标区间 $[-2, 2]$ 内绘制最简单的二次函数 $y = x^2$。计算机在执行离散采样时，会进行以下标准步骤：

1. *确定采样密度（步长）*：
   设定采样点总数 $N$。如果我们设 $N = 4$，那么采样步长 $Delta x$ 即可通过公式计算：
   $ Delta x = (x_("max") - x_("min")) / N = (2 - (-2)) / 4 = 1.0 $

2. *生成采样序列*：
   根据步长，自左向右依次计算每个采样点的自变量值：
   $ x_i = x_("min") + i dot.op Delta x quad (i = 0, 1, 2, 3, 4) $
   我们得到了一个离散的自变量数组：${-2.0, -1.0, 0.0, 1.0, 2.0}$。

3. *求解函数值与描点*：
   将 $x_i$ 代入公式 $y = x^2$，得到对应的因变量数组与坐标点：

#align(center)[
  #block(width: 80%)[
    #set text(size: 9pt)
    #table(
      columns: (1fr, 1fr, 1.2fr),
      align: (center, center, center),
      stroke: 0.5pt + luma(200),
      fill: (x, y) => if y == 0 { luma(245) } else { none },
      table.header([*采样序号 $i$*], [*计算关系 $y_i = x_i^2$*], [*生成的离散点坐标*]),
      [0], [$-2.0^2 = 4.0$], [$(-2.0, 4.0)$],
      [1], [$-1.0^2 = 1.0$], [$(-1.0, 1.0)$],
      [2], [$0.0^2 = 0.0$], [$(0.0, 0.0)$],
      [3], [$1.0^2 = 1.0$], [$(1.0, 1.0)$],
      [4], [$2.0^2 = 4.0$], [$(2.0, 4.0)$]
    )
  ]
]

4. *线性插值连线*：
   在屏幕渲染阶段，绘图引擎会使用最简单的折线段，将相邻的坐标点依次首尾相连：
   $ (-2.0, 4.0) arrow.r (-1.0, 1.0) arrow.r (0.0, 0.0) arrow.r (1.0, 1.0) arrow.r (2.0, 4.0) $

通过这种“分段线性逼近”的方式，当采样点数 $N$ 足够大时（例如 $N = 1000$），人眼在屏幕上就很难察觉出折线的棱角，从而看到一条平滑细腻的抛物线。



#v(1em)
== 2.3 基于世界单位步长的渐进式采样过程

为了更直观地观察“描点法”随采样步长 $h$（以世界单位，World Units 为度量）减小时的逼近过程，我们将采样区间 $[-2.0, 2.0]$ 离散化。该区间总宽度为 $4.0$ 世界单位。

以下九宫格图表展示了步长 $h$ 从 $4.0$ WU 逐步精细化至 $0.1$ WU 时，计算机在直角坐标系中直接进行“计算描点”与“插值连线”的数学收敛过程：

// 强制整个 3x3 九宫格作为一个不拆分的整体，避免跨页断开
#block(breakable: false)[
  #let plot-parabola(h-val, title) = {
    // 设定完全一致的物理宽高，确保网格呈现绝对正方形
    let w = 80pt
    let h = 80pt

    // 【完全对称且相等的视口范围】X轴：[-4.5, 4.5]，Y轴：[-4.5, 4.5]，数学跨度横纵均为 9.0
    let x-min = -4.5
    let x-max = 4.5
    let y-min = -4.5
    let y-max = 4.5

    // 建立离散坐标到像素的无损双向映射
    let map-x(x) = {
      (x - x-min) / (x-max - x-min) * w
    }
    let map-y(y) = {
      h - (y - y-min) / (y-max - y-min) * h
    }

    // 根据初始步长 h-val 动态计算点数 N
    let N = int(calc.round(4.0 / h-val)) + 1

    // 生成采样点集
    let pts = ()
    let s-min = -2.0
    let s-max = 2.0
    let step = h-val
    for i in range(N) {
      let x = s-min + i * step
      let y = x * x
      pts.push((x, y))
    }

    // 渲染单张微型图表卡片
    block(
      stroke: 0.5pt + luma(160), // 外圈轻微深灰色包裹
      radius: 4pt,
      fill: white,
      inset: (top: 8pt, bottom: 8pt, left: 10pt, right: 10pt),
      [
        #align(center)[#text(7.5pt, fill: luma(60))[#title]]
        #v(4pt)
        #align(center)[
          #box(
            width: w,
            height: h,
            stroke: none,
            fill: none,
            clip: true, // 【裁剪保障】任何子像素级的溢出都会被精准裁剪在正方形区域内
            [
              // 【核心避坑】使用 place(top + left) 包裹所有图元，消除代码换行产生的纵向偏移
              #place(top + left)[
                // 1. 【加深网格线】绘制对称的正方形网格虚线 (加深至极清晰的 luma(140))
                #for gx in (-4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0) {
                  place(line(
                    start: (map-x(gx), map-y(y-min)),
                    end: (map-x(gx), map-y(y-max)),
                    stroke: (paint: luma(140), dash: "dashed", thickness: 0.5pt)
                  ))
                }
                #for gy in (-4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0) {
                  place(line(
                    start: (map-x(x-min), map-y(gy)),
                    end: (map-x(x-max), map-y(gy)),
                    stroke: (paint: luma(140), dash: "dashed", thickness: 0.5pt)
                  ))
                }

                // 2. 【高对比度主轴】主直角坐标轴加深至 luma(30)（接近纯黑，突出主次关系）
                #place(line(start: (map-x(x-min), map-y(0)), end: (map-x(x-max), map-y(0)), stroke: 0.8pt + luma(30))) // X 轴
                #place(line(start: (map-x(0), map-y(y-min)), end: (map-x(0), map-y(y-max)), stroke: 0.8pt + luma(30))) // Y 轴

                // 3. 【引线】每个红色采样点到 X 轴 (y = 0) 的虚线投射，改为鲜艳的珊瑚橙 (#ff7043)
                #for pt in pts {
                  let px = map-x(pt.at(0))
                  let py = map-y(pt.at(1))
                  let py0 = map-y(0)
                  place(line(
                    start: (px, py),
                    end: (px, py0),
                    stroke: (paint: rgb("#ff7043"), dash: "dashed", thickness: 0.6pt)
                  ))
                }

                // 4. 绘制折线段插值
                #for i in range(N - 1) {
                  let p1 = pts.at(i)
                  let p2 = pts.at(i + 1)
                  let px1 = map-x(p1.at(0))
                  let py1 = map-y(p1.at(1))
                  let px2 = map-x(p2.at(0))
                  let py2 = map-y(p2.at(1))
                  place(line(start: (px1, py1), end: (px2, py2), stroke: 1.0pt + rgb("#1e88e5")))
                }

                // 5. 绘制采样红色圆点
                #for pt in pts {
                  let px = map-x(pt.at(0))
                  let py = map-y(pt.at(1))
                  place(dx: px - 1.2pt, dy: py - 1.2pt)[
                    #circle(radius: 1.2pt, fill: rgb("#e53935"))
                  ]
                }
              ]
            ]
          )
        ]
      ]
    )
  }

  // 渲染 3x3 矩阵排版
  #align(center)[
    #block(width: 100%)[
      #grid(
        columns: (1fr, 1fr, 1fr),
        gutter: 12pt,
        align: center,
        plot-parabola(4.0,  [step $h = 4.0$ (N = 2)]),
        plot-parabola(2.0,  [step $h = 2.0$ (N = 3)]),
        plot-parabola(1.0,  [step $h = 1.0$ (N = 5)]),

        plot-parabola(0.8,  [step $h = 0.8$ (N = 6)]),
        plot-parabola(0.5,  [step $h = 0.5$ (N = 9)]),
        plot-parabola(0.4,  [step $h = 0.4$ (N = 11)]),

        plot-parabola(0.25, [step $h = 0.25$ (N = 17)]),
        plot-parabola(0.2,  [step $h = 0.2$ (N = 21)]),
        plot-parabola(0.1,  [step $h = 0.1$ (N = 41)]),
      )
      #v(0.5em)
      #text(8.5pt, fill: luma(100))[*表 2.2：* 基于不同步长 $h$（单位：世界单位 WU）下对二次抛物线进行离散采样与引线投影的渐进收敛矩阵]
    ]
  ]
]
#v(1em)


#v(0.5em)
== 2.3.1 采样收敛规律与底层硬件协同机理 <sampling-analysis>

对上述九宫格矩阵（表 2.2）的分析可以发现一个直观的几何收敛规律：随着采样步长 $h$ 呈几何级数减小，分段折线段对抛物线 $y = x^2$ 的几何逼近误差呈平方级收敛。当步长 $h \le 0.2$（即采样点数 $N \ge 21$）时，折线间的转折棱角在人眼视觉层面上已基本不可察觉，呈现出高保真度的平滑曲线。

在现代计算机体系结构下，这种显式离散采样算法虽然数学原理简单，但在工程执行层面却具备*极其优秀的运行性能*。这主要得益于其底层连续的内存排布、简单的分支流，以及现代 CPU 的一系列硬件加速和编译器优化特性：

1. *硬件预取器（Hardware Prefetcher）的高效加载*：
   在该算法中，我们生成的离散坐标点被顺序存入连续的内存空间（如 C++ 的 `std::vector<Point2D>`）。在执行采样循环时，内存表现为严格的线性、步长固定的顺序寻址（Sequential Access）。CPU 内部的*硬件预取器*能极快地识别出这种 $O(1)$ 步进的数据访问模式，从而在 CPU 执行核心实际发出数据请求之前，提前将后续的内存行（Cache Line）载入至高速缓存（L1/L2 Cache）中，几乎消除了“缓存未命中（Cache Miss）”带来的总线停顿延迟。

2. *分支预测器（Branch Predictor）的零开销流水线*：
   由于整个采样循环逻辑极其纯粹，循环体内没有任何基于运行期不确定数据而产生的条件分支语句（如 `if-else` 条件判断）。现代 CPU 的*分支预测器*在执行此段指令流水线时，能达到近乎 100% 的预测准确率。指令流水线可以全速、不间断地向下推进，彻底避免了因分支预测失败而引发的“流水线清空（Pipeline Flush）”灾难，最大化了 CPU 核心中执行单元（ALU）的吞吐率。

3. *SIMD（单指令多数据流）向量化加速*：
   显式均匀采样的每一次计算 $y_i = f(x_i)$ 之间没有任何上下文和循环依赖（Loop-carried Dependency），属于典型的“易并行（Embarrassingly Parallel）”任务。这为 CPU 的 *SIMD（如 Intel AVX-512、ARM NEON）向量化*提供了完美的施展空间。
   借助编译器的自动向量化，CPU 可以将 8 个双精度浮点数（或 16 个单精度浮点数）同时加载进单个向量寄存器中，在同一个时钟周期内并发执行平方或加减乘除等代数变换。这使得点集的计算通量呈指数级上升。

4. *编译型语言标准数学库的底层实现性能*：
   当函数中包含超越函数（如 `sin`、`cos`、`exp`）时，诸如 C++、Rust、Fortran 等高性能编译型语言会直接链接到平台高度优化的数学库：
   - *算法实现*：这些标准库（如 `glibc` 的 `libm`、LLVM 的数学内置函数）在底层并不使用展开项极多且收敛缓慢的泰勒级数，而是采用基于汇编或寄存器级优化的* minimax 多项式逼近（如 Remez 算法）*或 CORDIC 算法，以极少的多项式项数在微观上达到机器精度的极致。
   - *向量化数学库 (Vector Math Library)*：在开启高级编译器优化（如 GCC/Clang 的 `-O3` 和 `-ffast-math`，或 Rust 的 `RUSTFLAGS="-C target-cpu=native"`）时，编译器能够将普通的标量数学函数调用，自动替换为高度向量化的数学函数库。例如，自动链接到 *Intel SVML（Short Vector Math Library）*或开源的 *SLEEF*。此时，即使是计算复杂的三角函数，CPU 也能通过 SIMD 寄存器在单周期内并发求解 4 到 8 个角度的 `sin` 值。
   - *语言间对比*：在编译优化全开的前提下，C++、Rust、Fortran 对这些底层数学库的调用开销几乎完全一致，均能逼近硬件理论带宽上限；而由于 Fortran 具有天然的“无指针别名（No Aliasing）”语义，编译器在对 Fortran 的采样循环进行 SIMD 自动向量化时的态度通常最为激进，而 C++ 和 Rust 则可以通过 `__restrict` 关键字或特定的编译器注解来达到同等极致的并行化效果。

5. *极其高效的数据管线流转（散点与线段带）*：
   因为离散采样生成的坐标点集在物理内存中是紧密排布且连续的，当需要将顶点数据传递至显卡时，我们可以直接利用一次 `memcpy`，将整块内存打包作为顶点缓冲区对象（VBO）传输至显存中。无论是作为单独的*散点（Scatter Points，对应图形 API 中的 `GL_POINTS`）*进行粒子化渲染，还是作为紧密相连的*线段带（Line Strip，对应 `GL_LINE_STRIP`）*进行线框绘制，底层图形驱动都不需要进行任何数据重组，达到了物理内存带宽的极限。

然而，显式均匀采样在面对诸如 $1/x$ 等包含无穷奇点、或者高频剧烈波动的复杂函数时，由于缺乏对自适应几何边界的感知，极易画出错误的跨界连接线。这促使我们必须继续探寻更具数学完备性的渲染方案。在下一章中，我们将探讨一种截然不同的思想——如何利用区间算术，构建“绝对安全”的几何边界探测网。


#v(0.5em)
== 2.3.2 固定采样步长的数学硬伤：高频函数与奈奎斯特混叠陷阱 <step-size-reflection>

在上一小节中，我们探讨了均匀离散采样如何完美契合现代计算机硬件流水线。然而，这种“在自变量 $x$ 轴上强行均匀分割”的朴素数学假设，在面对具有*高频（High-Frequency）*特征的连续函数时，会暴露出毁灭性的数学硬伤。

当函数在微小的区间内进行极其迅速的周期性振荡时，任何事先硬编码、一成不变的“初始步长 $h$”都会让绘图算法彻底失效。我们以高频正弦函数为例：
$ y = sin(999x) $
该函数的物理频率 $f$ 约为：
$ f = 999 / (2 pi) approx 159 "Hz" $
这意味着，自变量 $x$ 每增加 $1$ 个世界单位（WU），函数就会完整振荡大约 $159$ 次，其周期仅为 $T approx 0.0063$ WU。

对于这种极速振荡的函数，固定步长采样在信号学定理面前将显得无能为力。

==== 1. 信号学核心概念引入

为了分析固定步长的底层缺陷，我们需要引入信号处理（DSP）中的三个奠基性概念：

- *香农-奈奎斯特采样定理（Nyquist-Shannon Sampling Theorem）*：为了使离散的采样点集能够无失真地重建出连续的原始信号，采样频率 $f_s$（即每单位长度采集的点数）必须严格大于信号最高频率分量 $f_("max")$ 的两倍：
  $ f_s > 2 f_("max") $
- *奈奎斯特频率（Nyquist Frequency）*：定义为临界边界 $2 f_("max")$。一旦采样率低于这个边界，信息就会发生不可逆的丢失。
- *混叠（Aliasing）与“幻影信号（Ghost Signal）”*：如果我们的采样频率低于奈奎斯特频率（即 $f_s <= 2 f_("max")$），原本的高频信号就会发生“频谱折叠”，在离散点集连接后，会*伪装成一个完全错误的、波长极长的低频波形*。这个凭空产生的错误信号就被称为“幻影信号”或“混叠波形”。

对于 $y = sin(999x)$，其临界奈奎斯特频率要求采样率 $f_s > 318$ 次/单位。换算为采样步长 $h$：
$ h = 1 / f_s < 1 / 318 approx 0.00314 "WU" $

如果我们的初始步长 $h$ 稍大（例如设定为看似很小的 $h = 0.01$，此时 $f_s = 100$ 远低于临界值 $318$），采样定理就会被无情违背。算法在离散化连接后，画出来的根本不是频闪近 160 次的超高频波，而是一条极其平缓、波长极长的*虚假低频正弦波*。在信号学上，原本的高频信号已经彻底“混叠”为了幻影。

#v(0.5em)
==== 2.3.2.1 信号学混叠现象直观对照

为了最直观地呈现这一信号学硬伤，我们利用矢量引擎原生绘制了学术界经典的混叠对照图。

我们在一个大横框内，上下对称绘制了引入 $45 degree$ 相位右移的连续模拟信号（蓝色实线 $y = 2.0 sin(2.0 x + 0.25 pi)$，物理频率为 $f_0 approx 0.318$ Hz）：
- *方案 A（高采样率）*：采样步长 $h = 0.3$ WU，由于采样率高于奈奎斯特频率，红色的采样散点能够完美紧贴并复原蓝色高频信号。
- *方案 B（低采样率）*：采样步长 $h = 2.0$ WU，严重违反采样定理。采样的红色散点由于间隔过大，在连线后竟然*虚构出了一条完全错误的、红色的低频“幻影波”（红色虚线 $y = -2.0 sin((pi - 2.0) x - 0.25 pi)$）*。

// 【大横框包裹】占据整个 A4 纸宽度，完美对称且两端铺满，整体绝不跨页中断
#block(breakable: false)[
  #align(center)[
    #block(
      width: 440pt,
      stroke: 0.5pt + luma(160),
      radius: 6pt,
      fill: white,
      inset: (top: 10pt, bottom: 10pt, left: 15pt, right: 15pt),
      [
        #align(left)[#text(9pt, weight: "bold", fill: luma(50))[图 2.2：采样定理与混叠（Aliasing）现象直观物理对照]]
        #v(6pt)

        #let plot-aliasing(h-val, title) = {
          // 设定完全一致的物理宽高，确保网格呈现绝对正方形
          let w = 80pt
          let h = 80pt

          // 【完全对称且相等的视口范围】X轴：[-6.5, 6.5]，Y轴：[-6.5, 6.5]
          let x-min = -6.5
          let x-max = 6.5
          let y-min = -6.5
          let y-max = 6.5

          // 建立离散坐标到像素的无损双向映射
          let map-x(x) = {
            (x - x-min) / (x-max - x-min) * w
          }
          let map-y(y) = {
            h - (y - y-min) / (y-max - y-min) * h
          }

          // 根据初始步长 h-val 动态计算点数 N（曲线在 [-6.0, 6.0] 范围内生成，总跨度为 12.0 WU）
          let N = int(calc.round(12.0 / h-val)) + 1

          // 生成采样点集
          let pts = ()
          let s-min = -6.0
          let s-max = 6.0
          let step = h-val
          for i in range(N) {
            let x = s-min + i * step
            let y = 2.0 * calc.sin(2.0 * x + 0.25 * calc.pi) // 高频振荡信号
            pts.push((x, y))
          }

          // 渲染单张图表卡片
          block(
            width: w + 16pt,
            height: h + 30pt,
            stroke: none, // 彻底去除子图边框，统一由外围大横框包裹
            fill: none,
            inset: (top: 6pt, bottom: 6pt, left: 8pt, right: 8pt),
            [
              #align(center)[#text(7.5pt, fill: luma(60), weight: "bold")[#title]]
              #v(4pt)
              #align(center)[
                #box(
                  width: w,
                  height: h,
                  stroke: none,
                  fill: none,
                  clip: true, // 【裁剪保障】任何子像素级的溢出都会被精准裁剪在正方形区域内
                  [
                    #place(top + left)[
                      // 1. 绘制网格虚线 (加深至极清晰的 luma(170))
                      #for gx in (-6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0) {
                        place(line(
                          start: (map-x(gx), map-y(y-min)),
                          end: (map-x(gx), map-y(y-max)),
                          stroke: (paint: luma(170), dash: "dashed", thickness: 0.5pt)
                        ))
                      }
                      #for gy in (-6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0) {
                        place(line(
                          start: (map-x(x-min), map-y(gy)),
                          end: (map-x(x-max), map-y(gy)),
                          stroke: (paint: luma(170), dash: "dashed", thickness: 0.5pt)
                        ))
                      }

                      // 2. 主直角坐标轴 (主轴加深加粗至 0.8pt + luma(50))
                      #place(line(start: (map-x(x-min), map-y(0)), end: (map-x(x-max), map-y(0)), stroke: 0.8pt + luma(50))) // X 轴
                      #place(line(start: (map-x(0), map-y(y-min)), end: (map-x(0), map-y(y-max)), stroke: 0.8pt + luma(50))) // Y 轴

                      // 3. 离散引线，鲜艳的珊瑚橙色 (#ff7043)
                      #for pt in pts {
                        let px = map-x(pt.at(0))
                        let py = map-y(pt.at(1))
                        let py0 = map-y(0)
                        place(line(
                          start: (px, py),
                          end: (px, py0),
                          stroke: (paint: rgb("#ff7043"), dash: "dashed", thickness: 0.6pt)
                        ))
                      }

                      // 4. 原始高频正弦波 (皇家蓝色实线，如果是低采样率图，则稍微淡化以作对比背景)
                      #let true-pts = ()
                      #for i in range(151) {
                        let x = -6.0 + i * (12.0 / 150.0)
                        let y = 2.0 * calc.sin(2.0 * x + 0.25 * calc.pi)
                        true-pts.push((map-x(x), map-y(y)))
                      }
                      #let b-color = if h-val > 1.0 { rgb("#1e88e5").lighten(40%) } else { rgb("#1e88e5") }
                      #for i in range(150) {
                        let p1 = true-pts.at(i)
                        let p2 = true-pts.at(i + 1)
                        place(line(start: p1, end: p2, stroke: 1.0pt + b-color))
                      }

                      // 5. 【混叠图独有】绘制重构后的低频虚幻波 (红色虚线)
                      #if h-val > 1.0 {
                        let alias-pts = ()
                        for i in range(151) {
                          let x = -6.0 + i * (12.0 / 150.0)
                          let y = -2.0 * calc.sin((calc.pi - 2.0) * x - 0.25 * calc.pi) // 经严谨相位偏移换算后的低频波
                          alias-pts.push((map-x(x), map-y(y)))
                        }
                        for i in range(150) {
                          let p1 = alias-pts.at(i)
                          let p2 = alias-pts.at(i + 1)
                          place(line(start: p1, end: p2, stroke: (paint: rgb("#ff7043"), dash: "dashed", thickness: 1.2pt)))
                        }
                      }

                      // 6. 绘制采样红色圆点
                      #for pt in pts {
                        let px = map-x(pt.at(0))
                        let py = map-y(pt.at(1))
                        place(dx: px - 1.2pt, dy: py - 1.2pt)[
                          #circle(radius: 1.2pt, fill: rgb("#e53935"))
                        ]
                      }
                    ]
                  ]
                )
              ]
            ]
          )
        }

        // 横向并排两个方案，由于外层大 block 宽度是 440pt，里面两个图并排能完美对称呈现
        #grid(
          columns: (1fr, 1fr),
          gutter: 15pt,
          align: center,
          plot-aliasing(0.3, [方案 A：高采样率 (step $h = 0.3$) — 完美拟合]),
          plot-aliasing(2.0, [方案 B：低采样率 (step $h = 2.0$) — 严重混叠]),
        )

        #v(0.5em)
        #line(start: (0%, 0%), end: (100%, 0%), stroke: 0.3pt + luma(200))
        #v(0.5em)
        #align(left)[
          #text(8.2pt, fill: luma(100))[
            *表 2.2：* 奈奎斯特采样定理直观物理对照（引入 $phi_0 = 0.25 pi$ 相位右移）。方案 A 采用密集等距步长，采集的红色离散点（采样率 $f_s > 2 f_("max")$）能够无失真地复原出原始蓝色信号波形；方案 B 采用稀疏等距步长，采样的红色离散点因严重低于奈奎斯特临界频率，在离散化连接后虚构出一条完全错误的低频“虚假波形”（红色虚线），形象展示了离散绘图中的频谱混叠缺陷。
          ]
        ]
      ]
    )
  ]
]
#v(1em)


#v(0.5em)
== 2.3.3 离散采样的无底深渊：渐近线、奇点与算力几何级数爆炸 <asymptotes-reflection>

除了无法应对高频振荡外，固定步长采样在数学上面临的另一个致命死穴，是**不连续点与奇点（Singularity）**。

在绘制像 $y = 1/x$（反比例函数）或 $y = log(x)$（对数函数）这样包含垂直渐近线的几何曲线时，连续的数学空间会在零点附近暴露出难以逾越的无底深渊。任何试图通过“单纯提高全局采样密度”来强行绘制它们的离散手段，最终都会在物理世界和算力极限面前撞得粉碎。

==== 1. 反比例函数 $y = 1/x$：算力黑洞的线性缩放

在数学上，当自变量 $x$ 从正侧无限逼近 0 时，因变量 $y = 1/x$ 会以极快的速度冲向正无穷大（$+oo$）。

如果我们采用固定的步长 $h$ 进行采样，会遇到两个无法逾越的屏障：

- *虚假连接线的致命错误*：如果采样点没有正好落在 $x = 0$ 处，算法可能会采到 $x_1 = -h/2$（对应的 $y_1 = -2/h$）和 $x_2 = +h/2$（对应的 $y_2 = +2/h$）这两个点。在屏幕上连线时，绘图引擎会不加思考地用一根笔直的直线段横跨 $x=0$，强行连接起 $-2/h$ 和 $+2/h$。这在数学上是极其荒谬的——我们在两个原本无限分离、永不相交的数学分界线之间，画出了一条根本不存在的“虚假垂直连线”。
- *算力黑洞的爆发*：如果我们为了让曲线在纵向上“画得更深、更贴近渐近线”，假设我们希望把曲线在纵向上一直精细绘制到 $y_("max")$ 的高度（这在双精度浮点数能够表达的 $10^(308)$ 面前只是极其微小的一步）。
  由于 $y = 1/x$，当高度达到 $y_("max")$ 时，其横坐标对应的 $x$ 已经缩减到了极微观的：
  $ x_("min") = 1 / y_("max") $
  for 保证在区间 $[0, x_("min")]$ 内至少能落下一个采样点、从而不至于直接“横跨断崖”画出虚假连线，我们全局的均匀步长 $h$ 必须缩减到：
  $ h <= 1 / y_("max") $
  这意味着，为了画出区间 $[0, 1]$ 内的曲线，我们必须在横轴上均匀采样的总点数 $N$ 为：
  $ N = 1 / h >= y_("max") $

我们不妨做一个有趣的定量计算。假设我们要把曲线高度还原到 $y_("max") = 10^(25)$ 的高度：
- 我们需要生成至少 $N = 10^(25)$ 个点，并对每个点执行一次浮点数除法计算。
- 估算整个地球目前所有的超级计算机、云服务器、个人 PC、智能手机以及各种计算芯片合并在一起的*全球总算力上限，大约为 $10^(22)$ FLOPS*（每秒浮点运算次数）。
- 这意味着，如果我们要让*全人类所有的计算机停下手里的一切任务，协同并网、开足马力，仅仅为了去算 $y = 1/x$ 这一条一维曲线在 $[0, 1]$ 区间内的点*，需要消耗的时间为：
  $ T = 10^(25) / 10^(22) = 1000 "秒" (approx 16.7 "分钟") $
- 如果我们希望把曲线精细度再向上推 5 个数量级，达到 $y_("max") = 10^(30)$：
  同样让全球所有算力并网，不眠不休地全速计算，需要消耗的时间将飙升至：
  $ T = 10^(30) / 10^(22) = 10^8 "秒" approx 3.17 "年" $

这仅仅是一个一阶极点。这种随着几何精度线性增长，却瞬间吞噬全人类物理算力极限的现象，就是离散采样在奇点面前的无奈折衷。

#v(0.5em)
==== 2. 对数函数 $y = log(x)$：指数级爆炸的算力终结者

如果说反比例函数是一次“算力黑洞”，那么对数函数 $y = log(x)$ 在趋近零点时的表现，则是真正的“算力终结者”。

当 $x arrow.r 0^+$ 时，$y = log(x) arrow.r -oo$。在数学上，自变量与因变量的关系为：
$ x = e^y $

如果我们希望将对数曲线向下精细绘制到 $y = -y_("min")$（其中 $y_("min")$ > 0）的深度：
- 此时对应的横坐标极其微观，为 $x_("min") = e^(-y_("min"))$。
- 为了防止离散采样直接跨越断崖，均匀采样步长 $h$ 必须满足 $h <= e^(-y_("min"))$。
- 从而，要在区间 $[0, 1]$ 内完成采样，所需的总采样点数 $N$ 呈*指数级爆炸*：
  $ N = 1 / h >= e^(y_("min")) $

让我们同样做一个定量测算。假设我们仅仅希望把对数曲线在纵向上画到 $y = -100$ 这样一个在数学上极其平庸、微不足道的深度：
- 我们需要的采样点数将达到恐怖的：
  $ N >= e^(100) approx 2.688 times 10^(43) $
- 即使我们再次调动全地球所有的计算机，以 *$10^(22)$ FLOPS 的极限速度不间断地并网计算*，求解出这些坐标点所需消耗的物理时间将是：
  $ T = (2.688 times 10^(43)) / 10^(22) approx 2.688 times 10^(21) "秒" $
- 考虑到 1 年大约有 $3.15 times 10^7$ 秒，将该时间换算为年：
  $ T_("year") = (2.688 times 10^(21)) / (3.15 times 10^7) approx 8.5 times 10^(13) "年" $（即 85 万亿年）

*85 万亿年，这相当于我们已知宇宙年龄（约 138 亿年）的 6000 多倍。*

这便是固定步长采样在面对不连续空间和奇点时暴露出的底层宿命。这种算力开销并不是因为芯片不够快，对象也不是因为编译器不够聪明，而是因为“用均匀离散分割去描述连续超越空间”这一底层逻辑，在数学逻辑上存在着不可调和的根本性缺陷。



#v(0.5em)
== 2.3.2.2 确定性混沌与“绘图中的蝴蝶效应” <deterministic-chaos>

当函数的频率提升到极致时，固定步长的采样行为将彻底失去对几何形状的控制，坠入物理学上的**“确定性混沌（Deterministic Chaos）”**深渊。

我们以极高频的正弦函数为例：
$ y = sin(99999x) $
该函数的物理频率达到恐怖的 $f_0 arrow.r 15,915$ Hz。这意味着，自变量 $x$ 仅仅移动 $1.0$ 的距离，曲线就必须在屏幕上完整起伏 $15,915$ 次！此时，如果我们使用常规的固定步长 $h = 0.3$ 进行均匀采样，每个周期内的采样率实际上只有几万分之一。

在实际绘图时，这种极端欠采样会产生两个灾难性的后果：
- *几何信息彻底解体*：采样的散点由于在各周期中随机碰撞，导致相邻点之间的连线呈现出完全无序的、类似于“心电图”或物理噪声的粗糙锯齿波。真实的数学曲线是一片平滑的、由数万个波峰波谷组成的致密海洋，而屏幕上却展示了一条凌乱的、充满锯齿的单线。
- *绘图中的“蝴蝶效应”*：由于采样公式的高敏性，我们仅仅将步长 $h$ 微调万分之三（从 $h = 0.3$ 修改为 $h = 0.301$），在屏幕上绘制出来的噪声折线就会发生翻天覆地的改变。这意味着，**绘图引擎画出来的图形不仅是错误的，而且由于采样步长的微小漂移，它随时都在编造完全不同、毫无信用可言的“谎言”**。

// 【导入专为 Typst 0.14.0+ 自动兼容的最新版官方矢量库 CeTZ】
#import "@preview/cetz:0.5.2"

// 【大横框包裹】 3x1 矩阵横向并排，整体绝不跨页中断
#block(breakable: false)[
  #align(center)[
    #block(
      width: 440pt,
      stroke: 0.5pt + luma(160),
      radius: 6pt,
      fill: white,
      inset: (top: 10pt, bottom: 10pt, left: 15pt, right: 15pt),
      [
        #align(left)[#text(9pt, weight: "bold", fill: luma(50))[图 2.3：高频振荡 $y = 3 sin(99999 x)$ 在固定步长下的确定性混沌与高敏失真折线（基于 CeTZ 0.5.2 绘制）]]
        #v(6pt)

        #let plot_ultra_high_freq(h_val, title) = {
          // 统一 3x1 布局下单张卡片的尺寸
          let w = 82pt
          let h = 82pt

          block(
            width: w + 16pt,
            height: h + 30pt,
            stroke: none,
            fill: none,
            inset: (top: 6pt, bottom: 6pt, left: 8pt, right: 8pt),
            [
              #align(center)[#text(7.5pt, fill: luma(60), weight: "bold")[#title]]
              #v(4pt)
              #align(center)[
                // 设置画布单刻度长度，完美确保网格单元为绝对正方形
                #cetz.canvas(length: 3.2mm, {
                  import cetz.draw: *

                  // 1. 【核心修改：实线网格】CeTZ 原生正方形实线网格背景 (加深至极清晰的 luma(210))
                  grid((-4, -4), (4, 4), step: 1, stroke: 0.5pt + luma(210))

                  // 2. 【主坐标轴与 LaTeX 同款 stealth 箭头】
                  line((-4.2, 0), (4.2, 0), mark: (end: "stealth"), stroke: 0.8pt + luma(50)) // X 轴
                  line((0, -4.2), (0, 4.2), mark: (end: "stealth"), stroke: 0.8pt + luma(50)) // Y 轴

                  // 轴标签精确定位，无下划线命名冲突
                  content((3.8, -0.6), [$x$])
                  content((0.4, 3.9), [$y$])

                  // 根据初始步长 h_val 计算点数 N
                  let N = int(calc.round(4.0 / h_val)) + 1
                  let pts = ()
                  let s_min = -2.0
                  let step = h_val
                  for i in range(N) {
                    let x = s_min + i * step
                    let y = 3.0 * calc.sin(99999.0 * x) // 真实代入 99999x 进行计算
                    pts.push((x, y))

                    // 3. 绘制采样红色圆点
                    circle((x, y), radius: 1.2pt, fill: rgb("#e53935"), stroke: none)
                  }

                  // 4. 绘制折线插值段 (直接将点集数组解包成线，皇家蓝色实线)
                  line(..pts, stroke: 1.0pt + rgb("#1e88e5"))
                })
              ]
            ]
          )
        }

        // 1x3 横向并排，完美填满 440pt 宽度
        #grid(
          columns: (1fr, 1fr, 1fr),
          gutter: 10pt,
          align: center,
          plot_ultra_high_freq(0.3, [方案 A：step $h = 0.300$]),
          plot_ultra_high_freq(0.301, [方案 B：step $h = 0.301$]),
          plot_ultra_high_freq(0.305, [方案 C：step $h = 0.305$]),
        )

        #v(0.5em)
        #line(start: (0%, 0%), end: (100%, 0%), stroke: 0.3pt + luma(200))
        #v(0.5em)
        #align(left)[
          #text(8.2pt, fill: luma(100))[
            *表 2.3：* 高频振荡函数 $y = 3 sin(99999 x)$ 在固定步长下的确定性混沌效应。这三幅图由于采样率极其低下，表现为了纯粹的随机锯齿杂波。并且，仅仅将采样步长 $h$ 做出千分之三级别的极其微小的扰动（从 0.300 微调至 0.301 或 0.305），在屏幕上勾勒出的“虚假折线”就会发生翻天覆地的形变，彻底丧失了可视化的真实度。
          ]
        ]
      ]
    )
  ]
]
#v(1em)