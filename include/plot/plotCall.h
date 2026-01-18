// --- 文件路径: include/plot/plotCall.h ---
#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"
#include "../graph/GeoGraph.h"
#include "../graph/GeoSolver.h"
#include <vector>
#include <tuple>


enum class RenderUpdateMode {
    Incremental, // 局部增量（拖拽、创建）
    Viewport     // 视图变化（缩放、平移）
};




/**
 * @brief 核心渲染调度入口
 */
void calculate_points_core(
    std::vector<PointData>& out_points,      // 物理点 Buffer
    std::vector<FunctionRange>& out_ranges,  // 范围索引 Buffer (给 JS 绘图)
    GeometryGraph& graph,                    // 包含 ViewState, LUT, Pool 的核心对象
    RenderUpdateMode mode
);


#endif //PLOTCALL_H