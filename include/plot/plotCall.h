// --- 文件路径: include/plot/plotCall.h ---
#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"
#include "../graph/GeoGraph.h"
#include <vector>
#include <tuple>

// 视图状态定义
struct ViewState {
    double screen_width;
    double screen_height;
    double offset_x;
    double offset_y;
    double zoom;
    Vec2 world_origin;
    double wppx;
    double wppy;
};

// 并发计算结果载体
struct FunctionResult {
    unsigned int function_index;
    std::vector<PointData> points;
};

/**
 * @brief 核心渲染调度入口
 */
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    std::vector<GeoNode>& node_pool,
    const std::vector<uint32_t>& draw_order,      // 逻辑渲染顺序 (画家算法)
    const std::vector<uint32_t>& dirty_node_ids,  // 局部更新时的脏节点
    const ViewState& view,
    bool is_global_update                         // true=全重算(重置内存), false=增量(追加)
);

#endif //PLOTCALL_H