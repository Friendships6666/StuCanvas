//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_2points.h"
uint32_t InitCircle_2Points_Interact(GeometryGraph& graph) {
    // 1. 获取中心点 ID (利用 EnsurePoint 逻辑：优先选择，否则创建)
    uint32_t center_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(center_id) || !GeoType::is_point(graph.get_node_by_id(center_id).type)) {
        center_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置选中状态
    graph.get_node_by_id(center_id).state_mask |= IS_SELECTED;

    // 3. 配置交互上下文
    graph.preview_registers.resize(2); // 两点定圆，预留两个寄存器位置
    graph.preview_registers[0] = center_id;

    graph.preview_type      = GeoType::CIRCLE_2POINTS;
    graph.preview_func      = PreviewCircle_2Points_Intertact;
    graph.next_interact_func = EndCircle_2Points_Interact;

    return center_id;
}


void PreviewCircle_2Points_Intertact(GeometryGraph& graph)
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

    // 圆心坐标（使用世界坐标，因为 Plotter 需要世界坐标）
    double cx_world = node.result.x;
    double cy_world = node.result.y;

    // 2. 确定圆周上的参考点 (Point on Circumference)
    uint32_t selected_id = TrySelect_Interact(graph,true);

    double p2_x_world{};
    double p2_y_world{};
    bool need_snap = true;

    // 优先吸附到已有对象点
    if (graph.is_alive(id) && selected_id != id) {
        if (graph.is_alive(selected_id)) {
            const auto& selected_node = graph.get_node_by_id(selected_id);
            if (GeoType::is_point(selected_node.type)) {
                p2_x_world = selected_node.result.x;
                p2_y_world = selected_node.result.y;
                need_snap = false;
            }
        }
    }

    // 其次吸附到网格或跟随鼠标
    if (need_snap) {
        Vec2 mouse_world = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped_world = SnapToGrid_Interact(graph, mouse_world);
        p2_x_world = snapped_world.x;
        p2_y_world = snapped_world.y;
    }

    // 3. 计算半径 (Radius)
    // 在世界坐标系下计算欧式距离
    double dx = p2_x_world - cx_world;
    double dy = p2_y_world - cy_world;
    double r_world = std::hypot(dx, dy);

    // 4. 运行 Plot 逻辑 (充当临时 Solver)

    tbb::concurrent_bounded_queue<std::vector<PointData>> q;

    // 直接调用你定义的特化圆绘制器
    PlotCircle(&q, cx_world - view.offset_x, cy_world - view.offset_y, r_world, view,0,0,true);


    // 5. 提取结果到预览缓冲区
    // 注意：这里会覆盖上一帧的预览点
    q.try_pop(graph.preview_points);
}


uint32_t EndCircle_2Points_Interact(GeometryGraph& graph) {
    // 1. 确定第二个点 ID (优先选择已有，否则创建)
    uint32_t point2_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(point2_id) || !GeoType::is_point(graph.get_node_by_id(point2_id).type)) {
        point2_id = CreatePoint_Interact(graph);
    }

    // 2. 调用工厂函数创建物理节点
    // 直接使用 point2_id，语义更清晰（此时不必非要写回 registers[1]）
    GeoFactory::CreateCircle_2Points(
        graph,
        graph.preview_registers[0],
        point2_id,
        graph.preview_visual_config
    );

    // 3. 统一清理预览状态
    CancelPreview_Intectact(graph);

    return 0;
}