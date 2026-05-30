# StuPlot

**StuPlot** 是基于 C++23 构建的现代化、高性能且高精度的后端绘图引擎，专门用于可靠地绘制二维/三维隐函数曲线及参数方程曲面。StuPlot 能够从数学本质上保证图形拓扑的正确性，并能以工业级的稳定性自动处理复杂的大多数数学奇点。

---

## 核心特性

*   **仅头文件模板库 (Header-only)**：核心逻辑零外部依赖，只需将 `include/stuplot` 文件夹拷贝到项目中即可直接使用。
*   **高鲁棒性**：隐函数绘图能够自动识别并处理绝大多情况的“可去间断点”（如 $0/0$ 型）和“无穷间断点”（如 $1/0$ 型），确保图像平滑且无虚假连线,支持处理高频率函数。
*   **任意精度数字计算支持**：支持 `boost::multiprecision` (MPFR)。

---
## 主要功能
* $F(x,y)=0$ 二维隐函数绘图支持
* $F(x,y,z)=0$ 三维隐函数绘图支持
* $x=x(t),y=y(t)$ 二维参数曲线绘图支持
* $x=x(u,v),y=y(u,v),z=z(u,v)$ 三维参数曲面绘图支持
---
## 实现原理

参考论文：

1.  *Jeff Tupper. "Reliable Two-Dimensional Graphing Methods for Mathematical Formulae with Two Free Variables." SIGGRAPH 2001, University of Toronto.*
2.  *Storn, R. & Price, K. "Differential Evolution – A Simple and Efficient Heuristic for Global Optimization over Continuous Spaces." Journal of Global Optimization, 1997.*
---

## 安装与集成

由于本项目是 **Header-only** 库，您可以选择以下任一方式进行集成：

### 方式 1：直接拷贝
将 `include/stuplot` 文件夹直接复制到您的项目目录中，并在代码中包含：
```cpp
#include <stuplot/stuplot.hpp>
```

### 方式 2：CMake 子项目 (推荐)
在您的 `CMakeLists.txt` 中添加：
```cmake
add_subdirectory(path/to/StuPlot)
target_link_libraries(your_app PRIVATE StuPlot::stuplot)
```

---

## 快速入门示例

绘制隐函数 $y - \frac{\sin(x)}{x} = 0$：

```cpp
#include <stuplot/stuplot.hpp>
#include <vector>

int main() {
    // 定义隐函数方程
    // 泛型Lambda
    auto func = [](const auto& x, const auto& y) {
        using std::sin; //使用ADL
        //无需编写NaN处理逻辑
        return y - sin(x) / x;
    };
    
    //设置保存容器
    std::vector<StuPlot::Point2D<double>> points;

    // 配置描述符
    StuPlot::IntervalPlot2DDescriptor desc(func, &points);
    desc.x_min = -10.0; desc.x_max = 10.0;
    desc.y_min = -2.0;  desc.y_max = 2.0;
    desc.use_de_refinement = true; // 开启间断点自动精修(可选)
    //desc.de_population_size = 80; //设置差异演化算法种群规模(可选)
    //desc.....

    // 执行绘图任务
    StuPlot::plot_interval_2D(desc);

    return 0;
}
```
---
## 函数支持
| 分类 | 支持的函数                                                                       |
| :--- |:----------------------------------------------------------------------------|
| **基础运算与工具** | `abs`, `lerp`, `hypot`                                                      |
| **幂与开方** | `sqrt`, `cbrt`, `pow`                                                       |
| **指数与对数** | `exp`, `exp2`, `expm1`, `log`, `log10`, `log2`, `log1p`                     |
| **三角函数** | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`                        |
| **双曲函数** | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`                           |
| **特殊函数** | `erf`, `erfc`, `tgamma`, `lgamma`                                           |
| **取整与余数** | `floor`, `ceil`, `trunc`, `round`, `rint`, `nearbyint`, `fmod`, `remainder` |
---

## 开源协议

本项目采用 **MIT License**。

Copyright (c) 2026 **Friendships666**.

