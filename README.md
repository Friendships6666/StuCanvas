# StuCanvas

[![Language: Rust](https://img.shields.io/badge/Language-Rust-orange.svg)](https://www.rust-lang.org/)
[![Language: C++23](https://img.shields.io/badge/Language-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![API: Vulkan](https://img.shields.io/badge/API-Vulkan-red.svg)](https://www.vulkan.org/)

**StuCanvas** 是一款面向现代教学与科学传播的高性能、实时交互式多维可视化引擎。

StuCanvas 在设计上借鉴了 **GeoGebra, Desmos, Algodoo, Manim, GrafEq** 等科学可视化工具的优秀理念，并在架构设计、渲染性能和多维交互方面进行了重构与创新。引擎利用现代底层图形 API 与系统级语言，旨在为数学计算、几何推导、物理模拟以及教学视频制作提供极低延迟的交互与高保真的视频导出解决方案。

---

## 🛠 核心技术栈

* **图形 API**：基于 **Vulkan** 构建现代化渲染管线，实现低开销、高并行的多线程渲染。
* **开发语言**：采用 **Rust** 与 **C++23** 混合编程，兼顾内存安全、开发效率与极致的运行时性能。
* **窗口与系统交互**：基于 **SDL3** 强大的底层接口，提供跨平台的窗口管理与输入捕获。
* **文本与排版后端**：深度集成 **Typst** 编译器，实现印刷级数学公式与多语言文本的高质量排版。

---

## ✨ 核心特性

### 1. 高精度函数绘图
* **独家算法支持**：结合了 **Jeff Tupper 区间算术算法** 与**遗传算法**优化。
* **高鲁棒性绘制**：针对高频振荡函数、隐函数及奇异点等传统数学软件容易失真的场景，StuCanvas 经历了长期的算法调优，实现了兼顾渲染精度与抗锯齿质量的函数边界求解。

### 2. 高性能几何构建
* **图论依存求解**：借鉴了声明式的几何构建思路，但内部使用更复杂的**高性能拓扑图论算法**来解析和管理几何体之间的约束与依赖关系。
* **多核并行加速**：引入多核心 CPU 并行计算，将几何关系的实时求解延迟降至极低，即使在处理大规模依赖链时依然流畅。

### 3. 多维物理与图形模拟
* **“Pay-for-what-you-use” 架构**：支持 2D 与 3D 渲染。相比同类工具，StuCanvas 采用了“不为不需要的功能买单”的零开销抽象架构，节约内存。
* **更全面的 SVG 支持**：相比 Manim，StuCanvas 支持更完善的 SVG 规范子集，能够更加准确地还原矢量素材。

### 4. 实时交互与状态控制
* **双模预览系统**：借助 SDL3 底层 API，实现了“实时预览”与“离线渲染”无缝结合的机制。用户在动画播放或模拟运行过程中可以**随时暂停、介入交互或恢复播放**，不会丢失中间状态。

### 5. 非线性编辑（NLE）级导出架构
* **$O(1)$ 任意帧跳转**：视频导出和时间轴设计参考了非线性编辑软件（NLE）的底层逻辑，支持在任意帧数进行 $O(1)$ 时间复杂度的瞬间跳转，极大地方便了动画的调试与预览。
* **显存就地编码（In-place VRAM Encoding）**：视频导出时，渲染帧数据直接通过驱动调用显存就地进行编码。数据无需在 CPU 与 GPU 之间进行低效拷贝，系统开销极低。最高支持 **8192 × 8192** 超高分辨率的主流格式视频导出。

---


## 🛠️ Dependencies & Powered By (第三方依赖列表)

为了实现高性能图形渲染、物理模拟与着色器动态编译，**StuCanvas** 引入并深度集成了以下业界优秀的开源技术与硬件加速接口：

|                                      徽标 (Logo)                                      | 依赖项 (Dependency) | 许可协议 (License) | 说明与核心用途 (Description) |
|:-----------------------------------------------------------------------------------:| :--- | :--- | :--- |
|       <img src="./docs/logos/Vulkan_RGB_Dec16.svg" height="26" alt="Vulkan">        | **Vulkan SDK** | Apache 2.0 | 现代低开销、跨平台的图形与计算 API，用于构建高性能多线程渲染管线。 |
|     <img src="./docs/logos/nvidia-logo-vert-wht.png" height="26" alt="NVIDIA">      | **NVIDIA Driver API** | Proprietary | 结合 CUDA 技术，实现极低开销的显存就地编码（In-place VRAM Encoding）以及视频导出硬件加速。 <br>*(注：浅色主题可替换为 `nvidia-logo-vert-blk.png`)* |
|        <img src="./docs/logos/Intel-logo-nobox.png" height="20" alt="Intel">        | **Intel CPU Tech & TBB** | Apache 2.0 / MIT | 深度结合多核心 CPU 并行计算，大幅度优化复杂的图论几何求解与依赖树构建。 |
|           <img src="./docs/logos/slang-logo.svg" height="22" alt="Slang">           | **Slang Shading Language** | Apache 2.0 | 现代化的着色器语言与编译服务，提供灵活的跨平台（Vulkan/SPIR-V）着色器生成与动态管理。 |
| <img src="./docs/logos/Eigen_Silly_Professor_135x135.png" height="32" alt="Eigen3"> | **Eigen3** | MPL2 | 紧凑、高效的 C++ 线性代数库，用于底层的矩阵变换、几何坐标求解与代数计算。 |
|          <img src="./docs/logos/sdl-original.svg" height="20" alt="SDL3">           | **SDL3** | zlib | 跨平台底层开发库，提供健壮的窗口管理、事件捕获及多媒体上下文环境。 |


### 🏷️ Trademarks & Disclaimers (商标与免责声明)

*   **Intel** 以及 **Intel Inside** 均为 Intel Corporation 及其子公司的注册商标。
*   **NVIDIA**、**GeForce** 均为 NVIDIA Corporation 的注册商标。
*   **Vulkan** 以及 Vulkan 徽标是 Khronos Group Inc. 的注册商标。
*   本软件中所引用的各品牌 Logo 及注册商标仅用于标示系统底层的技术依赖与兼容性，其知识产权与版权均归属各对应商标持有人所有。
---
