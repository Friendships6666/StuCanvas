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
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    GeometryGraph& graph, // 注意：改为传 GeometryGraph 引用以便访问 buckets_all_heads
    const std::vector<uint32_t>& draw_order,
    const std::vector<uint32_t>& dirty_node_ids,
    const ViewState& view,
    RenderUpdateMode mode
) ;


#endif //PLOTCALL_H