// --- 文件路径: include/graph/GeoSolver.h ---
#ifndef GEOSOLVER_H
#define GEOSOLVER_H

#include "GeoGraph.h"

#include <optional>
#include <vector>



double ExtractValue(const GeoNode& parent, RPNBinding::Property prop);


struct LineCoords {
    double x1, y1, x2, y2;
};


std::optional<LineCoords> ExtractLineCoords(const GeoNode& node, const std::vector<GeoNode>& pool);

// =========================================================
// 2. 几何求解器 (Solver Functions)
// =========================================================

// --- 基础数学与度量 ---
bool is_heuristic_solver_local(SolverFunc s);
void Solver_ScalarRPN(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Measure_Length(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Measure_Angle(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Measure_Area(GeoNode& self, const std::vector<GeoNode>& pool);

// --- 基础几何对象 ---
void Solver_StandardPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_CircleThreePoints(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_RatioPoint(GeoNode& self, const std::vector<GeoNode>& pool);

// --- 动态函数与曲线 ---
void Solver_DynamicSingleRPN(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_DynamicDualRPN(GeoNode& self, const std::vector<GeoNode>& pool);

// --- 几何构造与关系 ---
void Solver_PerpendicularFoot(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_ParallelPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_Tangent(GeoNode& self, const std::vector<GeoNode>& pool);

// --- 交点与吸附 (解析型 & 图解型) ---
void Solver_AnalyticalIntersection(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_AnalyticalConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_IntersectionPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool);

// --- 标签系统 (智能 UI) ---
void Solver_LabelAnchorPoint(GeoNode& self, const std::vector<GeoNode>& pool);
void Solver_TextLabel(GeoNode& self, const std::vector<GeoNode>& pool);

#endif // GEOSOLVER_H