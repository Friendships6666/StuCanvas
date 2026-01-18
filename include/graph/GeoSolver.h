// --- 文件路径: include/graph/GeoSolver.h ---
#ifndef GEOSOLVER_H
#define GEOSOLVER_H

#include "GeoGraph.h"
#include "../../pch.h"
void Solver_ScalarRPN(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
void Solver_StandardLine(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
void Solver_StandardPoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
#endif // GEOSOLVER_H