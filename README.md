# StuCanvas

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.4-red?style=flat-square&logo=vulkan)](https://www.vulkan.org/)

**StuCanvas** 是一款专为现代化开发环境量身打造的跨平台、高性能、交互式实时与离线科学可视化综合计算引擎。

---

## 🌌 项目愿景与设计哲学

StuCanvas 的诞生深受 **GeoGebra**、**Desmos**、**Manim** 和 **Algodoo** 等科学可视化与物理模拟先驱软件的启发。然而，许多传统工具由于历史架构瓶颈，在面对复杂几何拓扑自愈、极端尺度下的数学奇点绘制，以及实时高吞吐物理交互时，往往难以同时兼顾性能与精度。

我们致力于从底层重构这一计算与渲染范式。通过**自研的强鲁棒性区间算术引擎**保障绝对严谨的数学拓扑追踪，利用**并行化编译的 DAG 有向无环图**实现极速复杂的拓扑关系自愈，并完全基于 **Vulkan 现代图形 API** 释放底层硬件的极致算力。StuCanvas 旨在探索集“高精度数学绘图、超低延迟几何求解、实时科学动画与多物理场仿真”于一体的下一代高性能综合计算图谱。

---

## ⚖️ 开源协议

本项目基于 **[MIT License](LICENSE)** 协议开源，鼓励自由学习、使用与二次开发。