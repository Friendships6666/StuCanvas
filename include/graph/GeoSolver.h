// --- 文件路径: include/graph/GeoSolver.h ---
#ifndef GEOSOLVER_H
#define GEOSOLVER_H

#include "GeoGraph.h"
double ExtractValue(const GeoNode& parent, RPNBinding::Property prop, const std::vector<GeoNode>& pool);
// =========================================================
// 求解器函数声明
// =========================================================
void Solver_ScalarRPN(GeoNode& self, const std::vector<GeoNode>& pool);
// 基础几何求解器
void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_StandardPoint(GeoNode& self, const std::vector<GeoNode>& pool);
// 度量求解器 (生成 Data_Scalar)
void Solver_Measure_Length(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Measure_Angle(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Measure_Area(GeoNode& self, const std::vector<GeoNode>& pool);

// 动态 RPN 热更新求解器
void Solver_DynamicSingleRPN(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_DynamicDualRPN(GeoNode& self, const std::vector<GeoNode>& pool);

void Solver_PerpendicularFoot(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_ParallelPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Tangent(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_IntersectionPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_AnalyticalIntersection(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_AnalyticalConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_RatioPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_CircleThreePoints(GeoNode& self, const std::vector<GeoNode>& pool);
#endif // GEOSOLVER_H