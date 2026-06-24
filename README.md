<div align="center">
  <h1>StuCanvas</h1>
  <font size="6"><b>官方网站</b></font><br>
  <a href="https://stucanvas.org/"><strong>stucanvas.org</strong></a>
</div>

> 目前 StuCanvas 正式版的测试与开发工作即将完毕。开发者已倾注了最大时间和精力，致力于为广大用户提供一个最强大的可视化工具。
> 
> 
[![Language: Rust](https://img.shields.io/badge/Language-Rust-orange.svg)](https://www.rust-lang.org/)
[![Language: C++23](https://img.shields.io/badge/Language-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![API: Vulkan](https://img.shields.io/badge/API-Vulkan-red.svg)](https://www.vulkan.org/)

**StuCanvas** 是一款支持多维可视化的渲染引擎，主要用于数学计算、几何推导、物理模拟展示以及科学传播。

项目在设计API上参考了 GeoGebra, Desmos, Algodoo, Manim, GrafEq 等优秀工具的功能特性，利用底层图形接口与系统级语言实现，提供多维交互与视频导出功能。

---

## 技术栈

* **图形 API**：基于 **Vulkan** 构建渲染管线，实现多队列渲染与硬件光追。
* **开发语言**：采用 **Rust** 与 **C++23**。
* **窗口与系统交互**：基于 **SDL3** 提供窗口管理与输入事件捕获。
* **文本排版后端**：**Typst** 编译器，支持数学公式与多语言文本的排版输出。

---

## 功能特性

### 1. 函数绘图
* **基础算法**：**Jeff Tupper 区间算术算法** 与**遗传算法**。
* **边界求解**：针对高频振荡函数、隐函数及奇异点等特殊数学场景进行边界求解与展示。

### 2. 几何约束求解
* **依赖管理**：采用声明式的几何构建逻辑，内部使用拓扑图论算法管理几何体之间的约束与依赖关系。
* **并行加速**：引入多核心 CPU 并行计算，进行几何关系的实时求解。
* **过程化建模** :支持拉伸、扫掠和布尔等参数化建模操作。

### 3. 多维图形支持
* **渲染维度**：支持 2D 与 3D 渲染，采用按需加载的设计。
* **SVG 支持**：支持较为全面的 SVG 规范子集解析与渲染。

### 4. 交互与状态控制
* **预览系统**：支持实时预览与暂停交互，用户在动画播放或模拟运行过程中可以介入交互并恢复。

### 5. 视频导出机制
* **时间轴跳转**：时间轴支持任意帧跳转，便于调试。
* **显存就地编码**：视频导出时，渲染帧数据直接通过驱动调用显存进行编码，减少 CPU 与 GPU 之间的数据拷贝，支持主流分辨率视频导出，硬件编码，最高支持8192x8192分辨率。

> 💡 **提示**
>
> 更多完整功能、详细参数与技术文档，请参阅 [StuCanvas 官方网站](https://stucanvas.org/)。
<hr>


<div align="center">
  <h2>💬 技术交流与社群</h2>
  <p>欢迎加入 QQ 交流群，计算机图形学&科学可视化，数值分析交流群：</p>
  <img src="docs/pictures/qq_group.jpg" width="280" alt="StuCanvas QQ Group" />
  <p><b>QQ 群号：960908849</b></p>
</div>