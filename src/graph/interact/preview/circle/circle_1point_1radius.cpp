//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_1point_1radius.h"
uint32_t InitCircle_1Point_1Radius_Interact(GeometryGraph& graph) {
    // 1. 获取中心点 ID：尝试选择，若选中的不是点或未选中，则创建新点
    uint32_t center_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(center_id) || !GeoType::is_point(graph.get_node_by_id(center_id).type)) {
        center_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置交互状态
    auto& node = graph.get_node_by_id(center_id);
    node.state_mask |= IS_SELECTED;

    // 3. 挂载预览与交互逻辑
    graph.preview_registers.resize(1); // 确保空间，通常建议在交互开始时 reset
    graph.preview_registers[0] = center_id;

    graph.preview_type = GeoType::CIRCLE_1POINT_1RADIUS;
    graph.preview_func = PreviewCircle_1Point_1Radius_Intertact;
    graph.next_interact_func = EndCircle_1Point_1Radius_Interact;

    return center_id;
}


void PreviewCircle_1Point_1Radius_Intertact(GeometryGraph& graph)
{
    // 1. 获取圆心点 (Center Point)
    auto id = graph.preview_registers[0];
    if (!graph.is_alive(id) ) {
        graph.preview_points.clear();
        return;
    }

    auto& node = graph.get_node_by_id(id);
    if (!GeoType::is_point(node.type) || node.error_status != GeoErrorStatus::VALID) return;

    const auto& view = graph.view;

    double cx_view = node.result.cx_view;
    double cy_view = node.result.cy_view;
    std::vector<uint32_t> parents;
    GeoFactory::CompileChannelInternal(graph,0,0,graph.preview_channels[0].original_infix,parents,false);
    if (graph.preview_status != GeoErrorStatus::VALID) return;
    double r_world = SolveChannel(GeometryGraph::NULL_NODE,0,graph,true);





    tbb::concurrent_bounded_queue<std::vector<PointData>> q;

    // 直接调用你定义的特化圆绘制器
    PlotCircle(&q, cx_view, cy_view, r_world, view,0,0,true);


    // 5. 提取结果到预览缓冲区
    // 注意：这里会覆盖上一帧的预览点
    q.try_pop(graph.preview_points);
}


uint32_t EndCircle_1Point_1Radius_Interact(GeometryGraph& graph) {

    GeoFactory::CreateCircle_1Point_1Radius(graph,graph.preview_registers[0],graph.preview_channels[0].original_infix,graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;


}