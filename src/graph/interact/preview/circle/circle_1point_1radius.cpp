//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_1point_1radius.h"
uint32_t InitCircle_1Point_1Radius_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        const auto& selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
            graph.preview_func = PreviewCircle_1Point_1Radius_Intertact;
            graph.preview_type = GeoType::CIRCLE_FULL_1POINT_1RADIUS;
            graph.next_interact_func = EndCircle_1Point_1Radius_Interact;
            graph.preview_registers[0] = selected_id;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_func = PreviewCircle_1Point_1Radius_Intertact;
        graph.preview_type = GeoType::CIRCLE_FULL_1POINT_1RADIUS;
        graph.next_interact_func = EndCircle_1Point_1Radius_Interact;
        graph.preview_registers[0] = new_point;
    }




    return graph.preview_registers[0];

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
    PlotCircle(&q, cx_view, cy_view, r_world, view);


    // 5. 提取结果到预览缓冲区
    // 注意：这里会覆盖上一帧的预览点
    q.try_pop(graph.preview_points);
}


uint32_t EndCircle_1Point_1Radius_Interact(GeometryGraph& graph) {

    GeoFactory::CreateCircle_1Point_1Radius(graph,graph.preview_registers[0],graph.preview_channels[0].original_infix,graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;


}