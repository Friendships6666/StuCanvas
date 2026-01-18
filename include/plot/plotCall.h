// --- 文件路径: include/plot/plotCall.h ---
#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"
#include "../graph/GeoGraph.h"
#include "../graph/GeoSolver.h"
#include <vector>
#include <tuple>







/**
 * @brief 核心渲染调度入口
 */
void calculate_points_core(
    std::vector<PointData>& out_points,
    std::vector<FunctionRange>& out_ranges,
    GeometryGraph& graph
);


#endif //PLOTCALL_H