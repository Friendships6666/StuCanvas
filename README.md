# StuCanvas

现代化 Web 端跨平台实时函数绘制与几何图形构建应用。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![WASM Build](https://img.shields.io/badge/WASM-Emscripten-green.svg)]()
[![Graphics](https://img.shields.io/badge/Graphics-WebGPU-orange.svg)]()

## 简介

**StuCanvas** 专为 Web 平台设计的现代化、高性能几何绘图应用。旨在解决浏览器端处理大规模动态几何关系时的性能瓶颈，利用 C++ 和 现代化图形API 。


## 特性

*   **混合渲染算法**：混合 **区间算术 (Interval Arithmetic)** & **行进网格法 (Marching Squares)**，加速渲染&保持精度。
*   **特殊抗锯齿技术**：针对“点图元”设计的抗锯齿方案，高性能高精度。
*   **任意精度支持**：底层默认采用 `Double` 精度，并支持通过扩展实现 **任意精度浮点运算**，彻底消除微观缩放下的浮点漂移。

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

复合函数算法算法，精准处理各类复杂的几何关系与极端函数形态。

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

### 实时交互演示

CPU | U9 275hHx

<table>
  <tr>
    <td align="center">
      <video src="./docs/videos/1.mp4" width="300px" controls>您的浏览器不支持视频播放</video><br/>
      <sub>StuCanvas实时绘制100个复杂隐函数</sub>
    </td>
    <td align="center">
      <video src="./docs/videos/2.mp4" width="300px" controls>您的浏览器不支持视频播放</video><br/>
      <sub>Desmos实时绘制50个y^3=sin(10x)</sub>
    </td>
  </tr>
</table>


## 应用与集成

### StuWiki
本项目作为 **StuWiki** 网站的官方底层图形驱动。Wiki 条目能够承载动态、交互式的几何模型，为用户提供直观的数学视觉化体验。


| 测试公式 (LaTeX Formula) | 测试目的 / 压力点 | 预期表现 & 鲁棒性要求 |
| :--- | :--- | :--- |
| $y^3 = \sin(x + 999999999)$ | 高精偏移量验证 | 验证运算时精度（$f64$ vs $f32$）。$f32$ 会出现严重锯齿。 |
| $y^3 = x^{1000000}$ | 大幂次溢出/断裂测试 | 观察函数图像在极高幂次下是否发生断裂或消失。 |
| $y^3 = \sin(\frac{1}{x})$ | $x=0$ 处的 NaN 逻辑 | 观察 $x=0$ 位置是否存在错误的垂直长直线。 |
| $y^3 = \tan(\frac{1}{x})$ | 采样逻辑验证 | 观察中心极高频区域是否填满，验证采样策略。 |
| $y^3 = \sin(99999x)$ | 摩尔纹与点采样测试 | 验证高频震荡下是否产生视觉假象或采样缺失。 |
| $\sin(x^2 + y^2) = 0.1$ | 复杂隐函数结构 | 观察同心圆是否完整、是否存在断裂。 |
| $y = 3^x \sin(x)$ | 数值鲁棒性 ($x > 800$) | 验证在大数值指数运算下的渲染稳定性。 |
| $y = \ln(\cos(x) + \sin(y))$ | 孤立离散点采样 | 验证对隐函数细节区域的捕捉能力。 |
| $y^3 = \ln(x)$ | 负半轴渐近线渲染 | 观察图像是否渲染到了屏幕最底端的趋近点。 |
| $y^3 = \tan(x)$ | 奇点与 NaN 处理 | 观察渐近线位置是否产生了不该存在的垂直连线。 |
| $y^3 = \frac{\ln(x)}{x-1}$ | 可去间断点 (Removable Singularity) | 验证区间算术对间断点处的平滑处理能力。 |
| $x^{\frac{2}{3}} + y^{\frac{2}{3}} = 1$ | 幂函数负数域解析 | 测试对 `pow` 函数的处理，图像应出现在全部 4 个象限。 |
| $x^2 + 2x + 1 = 0$ | 临界厚度测试 | 图像应为一条位于 $x=-1$ 的极细垂直线。 |
| $(y+x+1)^2 (y+1-x) = 0$ | 公式解析逻辑检查 | 验证公式解析器是否能正确分离直线系图像。 |
| $(\frac{1}{\cos t}, \tan t)$ | 参数方程渐近线 | 测试参数方程在趋近无穷大时的断开处理。 |



## 许可证

本项目采用 MIT 许可证。