// --- 文件路径: include/graph/GeoSolver.h ---
#ifndef GEOSOLVER_H
#define GEOSOLVER_H

#include "GeoGraph.h"
#include "../../pch.h"
#include "../include/functions/lerp.h"
/**
 * @brief 所有几何解算器的统一接口声明
 *
 * 架构说明：
 * 1. 签名简化：不再传递 pool, lut, view 等多个参数，统一通过 GeometryGraph 实例访问上下文。
 * 2. 状态驱动：Solver 运行前由调度器检查父节点状态，Solver 内部只负责纯数学逻辑。
 * 3. 数学自检：所有 Solver 内部需进行 isfinite 校验，并更新 self.status。
 */

// RPN 标量求解器 (补丁填充 + 数值计算)
void Solver_ScalarRPN(GeoNode& self, GeometryGraph& graph);

// 标准点求解器 (由两个标量父节点合成 X, Y)
void Solver_StandardPoint(GeoNode& self, GeometryGraph& graph);

// 线段/直线求解器 (由两个点父节点快照坐标)
void Solver_StandardLine(GeoNode& self, GeometryGraph& graph);

// 中点求解器
void Solver_Midpoint(GeoNode& self, GeometryGraph& graph);
void Solver_Circle_1Point_1Radius(GeoNode& self, GeometryGraph& graph);
void Solver_Circle_2Points(GeoNode& self, GeometryGraph& graph);
void Solver_Circle_3Points(GeoNode& self, GeometryGraph& graph);
double SolveChannel(GeoNode& self, int idx,GeometryGraph& graph,bool is_preview);
// 约束点求解器 (执行裁剪空间投影与吸附算法)
void Solver_ConstrainedPoint(GeoNode& self, GeometryGraph& graph);
inline const ComputedResult& get_parent_res(const GeometryGraph& graph, uint32_t pid) {
    return graph.get_node_by_id(pid).result;
}

// 图解交点求解器
void Solver_GraphicalIntersectionPoint(GeoNode& self, GeometryGraph& graph);
void Solver_Intersection(GeoNode& self, GeometryGraph& graph);
void Solver_Arc_2Points_1Radius(GeoNode& self, GeometryGraph& graph);
void Solver_Arc_3Points(GeoNode& self, GeometryGraph& graph);
void Solver_Arc_3Points_Circumarc(GeoNode& self, GeometryGraph& graph);
void Solver_ConstrainedPoint_Analytic(GeoNode& self, GeometryGraph& graph);
void Solver_VerticalLine(GeoNode& self, GeometryGraph& graph);
void Solver_ParallelLine(GeoNode& self, GeometryGraph& graph);
#endif // GEOSOLVER_H