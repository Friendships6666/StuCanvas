//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/line/previewRay.h"
void PreviewRay_Intertact(GeometryGraph& graph) {
    uint32_t id1 = graph.preview_registers[0];

    // 1. 基础校验
    if (!graph.is_alive(id1)) {
        graph.preview_points.clear();
        return;
    }
    const auto& node1 = graph.get_node_by_id(id1);
    if (!GeoType::is_point(node1.type) || node1.error_status != GeoErrorStatus::VALID) {
        graph.preview_points.clear();
        return;
    }

    // 2. 确定方向点的视口坐标 (View Space)
    const auto& view = graph.view;
    double p2x_v, p2y_v;

    // A. 尝试对象吸附
    uint32_t sel_id = TrySelect_Interact(graph, false);
    if (sel_id != 0 && sel_id != id1 && graph.is_alive(sel_id) &&
        GeoType::is_point(graph.get_node_by_id(sel_id).type))
    {
        const auto& node2 = graph.get_node_by_id(sel_id);
        p2x_v = node2.result.x_view;
        p2y_v = node2.result.y_view;
    }
    else
    {
        // B. 网格吸附/自由跟随
        Vec2 world_pos = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped   = SnapToGrid_Interact(graph, world_pos);
        p2x_v = snapped.x - view.offset_x;
        p2y_v = snapped.y - view.offset_y;
    }

    // 3. 执行射线渲染 (使用 LINE_RAY 枚举)
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;

    process_two_point_line(
        q,
        node1.result.x_view, node1.result.y_view,
        p2x_v, p2y_v,
        LinePlotType::RAY, // <--- 关键区别：射线类型
        view
    );

    // 4. 收割结果
    graph.preview_points.clear();
    q.try_pop(graph.preview_points);


}

uint32_t EndRay_Interact(GeometryGraph& graph) {
    // 1. 确定方向点 ID (优先选择已有，否则创建)
    uint32_t p2_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(p2_id) || !GeoType::is_point(graph.get_node_by_id(p2_id).type)) {
        p2_id = CreatePoint_Interact(graph);
    }

    // 2. 调用工厂函数创建物理射线
    GeoFactory::CreateRay(
        graph,
        graph.preview_registers[0],
        p2_id,
        graph.preview_visual_config
    );

    // 3. 清理状态
    CancelPreview_Intectact(graph);

    return 0;
}


uint32_t InitRay_Interact(GeometryGraph& graph) {
    // 1. 确定起点 ID (优先选择已有，若不符合条件则创建)
    uint32_t p1_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(p1_id) || !GeoType::is_point(graph.get_node_by_id(p1_id).type)) {
        p1_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置选中状态
    graph.get_node_by_id(p1_id).state_mask |= IS_SELECTED;

    // 3. 配置交互上下文 (射线需要两点：起点 + 方向点)
    graph.preview_registers.resize(2);
    graph.preview_registers[0] = p1_id;

    // 4. 挂载预览与下一步
    graph.preview_type       = GeoType::LINE_RAY;      // 标记为射线
    graph.preview_func       = PreviewRay_Intertact;   // 开启预览
    graph.next_interact_func = EndRay_Interact;        // 终结函数

    return p1_id;
}