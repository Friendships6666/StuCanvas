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
 * @brief 判断一个求解器是否为“图解型”（依赖屏幕采样 Buffer）
 */
inline bool is_heuristic_solver(SolverFunc solver) {
    return (solver == Solver_IntersectionPoint || solver == Solver_ConstrainedPoint);
}


/**
 * @brief 核心渲染调度入口
 */
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    std::vector<GeoNode>& node_pool,
    const std::vector<uint32_t>& draw_order,
    const std::vector<uint32_t>& dirty_node_ids,
    const ViewState& view,
    RenderUpdateMode mode
);
void commit_incremental_updates(GeometryGraph& graph, const ViewState& view, const std::vector<uint32_t>& draw_order);
void commit_viewport_update(GeometryGraph& graph, const ViewState& view, const std::vector<uint32_t>& draw_order);

#endif //PLOTCALL_H