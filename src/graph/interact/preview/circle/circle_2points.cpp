//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_2points.h"
uint32_t InitCircle_2Points_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto& selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_func = PreviewCircle_2Points_Intertact;
            graph.preview_type = GeoType::CIRCLE_FULL_2POINTS;
            graph.preview_registers[0] = selected_id;
            graph.next_interact_func = EndCircle_2Points_Interact;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_func = PreviewCircle_2Points_Intertact;
        graph.preview_type = GeoType::CIRCLE_FULL_2POINTS;
        graph.next_interact_func = EndCircle_2Points_Interact;
        graph.preview_registers[0] = new_point;
    }




    return graph.preview_registers[0];

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
    uint32_t selected_id = TrySelect_Interact(graph,false);

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
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        const auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            graph.preview_registers[1] = selected_id;
        }
    } else {
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);

        graph.preview_registers[1] = new_point;
    }


    // 3. 如果没有选中有效的点，则创建一个新的点

    GeoFactory::CreateCircle_2Points(graph,graph.preview_registers[0],graph.preview_registers[1],graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;


}