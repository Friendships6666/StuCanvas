# StuCanvas


[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=flat-square&logo=gnu)](https://www.gnu.org/licenses/gpl-3.0)
[![WASM Build](https://img.shields.io/badge/WASM-Emscripten-green.svg)]()
[![Graphics](https://img.shields.io/badge/Graphics-WebGPU-orange.svg)]()

## 简介

StuCanvas, 现代化跨平台的几何绘图应用，支持 Web 及 Python 环境。其核心设计目标是解决复杂几何关系在处理过程中的计算延迟与渲染瓶颈。该框架结合了启发式路径优化技术与现代化图形 API，实现了大规模几何实体的动态显示。该项目现为 StuWiki 知识共享平台的默认可视化工具。


## 特性

*   **混合渲染算法**：混合 **区间算术 (Interval Arithmetic)** & **行进网格法 (Marching Squares)**，加速渲染&保持精度。
*   **特殊抗锯齿技术**：针对“点图元”设计的抗锯齿方案，高性能高精度。
*   **任意精度支持**：底层默认采用 `Double` 精度，并支持通过扩展实现 **任意精度浮点运算**。

## 依赖库 

StuCanvas 集成以下 C++ 库：

库 | 用途
-------|-----------------------------------------------------
[oneTBB](https://github.com/oneapi-src/oneTBB) | 并行计算，多线程计算。
[xsimd](https://github.com/xtensor-stack/xsimd) | SIMD向量化，计算加速。
[Boost.Multiprecision](https://github.com/boostorg/multiprecision) | 任意精度计算前端。
[GMP](https://gmplib.org/) | 任意精度计算。
[simdjson](https://github.com/simdjson/simdjson) | JSON解析。
[nlohmann/json](https://github.com/nlohmann/json) | JSON解析。

### 运行环境
由于利用了前沿的 Web 图形 API，运行环境需满足：

组件 | 要求
-----|-------------------------------
浏览器 | 支持 **WebGPU** , **Web Assembly**的 **Chromium**,**Safari**,**FireFox** 浏览器

## 效果

启发式函数渲染算法，精准处理各类复杂的几何关系与极端函数形态。

<table>
  <tr>
    <td align="center"><img src="./docs/images/1.jpg" width="300px"/><br/><sub>隐函数</sub></td>
    <td align="center"><img src="./docs/images/2.jpg" width="300px"/><br/><sub>隐函数</sub></td>
    <td align="center"><img src="./docs/images/3.jpg" width="300px"/><br/><sub>1000隐函数圆</sub></td>
    <td align="center"><img src="./docs/images/4.jpg" width="300px"/><br/><sub>隐函数</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="./docs/images/5.jpg" width="300px"/><br/><sub>直线</sub></td>
    <td align="center"><img src="./docs/images/6.jpg" width="300px"/><br/><sub>曲线细节</sub></td>
    <td align="center"><img src="./docs/images/7.jpg" width="300px"/><br/><sub>万花筒</sub></td>
    <td align="center"><img src="./docs/images/8.jpg" width="300px"/><br/><sub>曲线细节</sub></td>
  </tr>
</table>




## 应用与集成

### StuWiki
本项目作为 **StuWiki** 网站的官方底层图形驱动。Wiki 条目能够承载动态、交互式的几何模型，为用户提供直观的数学视觉化体验。


###  渲染引擎压力测试对比 (StuCanvas Benchmark v1.1)
**TODO**





