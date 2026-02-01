//
// Created by hp on 2026/1/29.

// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_3points.h"
uint32_t InitCircle_3Points_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_registers[0] = selected_id;
            graph.next_interact_func = InitCircle_3Points_2_Interact;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_registers[0] = new_point;
    }




    return graph.preview_registers[0];

}


uint32_t InitCircle_3Points_2_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  true); // 多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto& selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_func = PreviewCircle_3Points_Intertact;
            graph.preview_type = GeoType::CIRCLE_3POINTS;
            graph.preview_registers[1] = selected_id;
            graph.next_interact_func = EndCircle_3Points_Interact;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_func = PreviewCircle_3Points_Intertact;
        graph.next_interact_func = EndCircle_3Points_Interact;
        graph.preview_type = GeoType::CIRCLE_2POINTS;
        graph.preview_registers[1] = new_point;
    }




    return graph.preview_registers[1];

}

void PreviewCircle_3Points_Intertact(GeometryGraph& graph) {
    // 1. 获取前两个已确定的点 ID
    uint32_t id1 = graph.preview_registers[0];
    uint32_t id2 = graph.preview_registers[1];

    // 安全检查：确保前两个点依然存活
    if (!graph.is_alive(id1) || !graph.is_alive(id2)) {
        graph.preview_points.clear();
        return;
    }

    const auto& p1 = graph.get_node_by_id(id1);
    const auto& p2 = graph.get_node_by_id(id2);
    const auto& view = graph.view;

    // 提取前两点的视口相对坐标
    double x1 = p1.result.x_view;
    double y1 = p1.result.y_view;
    double x2 = p2.result.x_view;
    double y2 = p2.result.y_view;

    // 2. 确定第三个点 (x3, y3) 的位置 —— 加入吸附逻辑
    double x3{}, y3{};
    bool need_grid_snap = true;

    // A. 尝试对象吸附：检查鼠标是否靠近已有的点
    uint32_t selected_id = TrySelect_Interact(graph, true);

    // 确保选中的不是前两个点本身
    if (selected_id != 0 && selected_id != id1 && selected_id != id2) {
        if (graph.is_alive(selected_id)) {
            const auto& p3 = graph.get_node_by_id(selected_id);
            if (GeoType::is_point(p3.type)) {
                x3 = p3.result.x_view;
                y3 = p3.result.y_view;
                need_grid_snap = false; // 成功吸附到对象，跳过网格吸附
            }
        }
    }

    // B. 尝试网格吸附：如果没吸附到对象，则检查网格
    if (need_grid_snap) {
        // 屏幕转世界
        Vec2 mouse_world = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        // 网格吸附
        Vec2 snapped_world = SnapToGrid_Interact(graph, mouse_world);
        // 转回视口相对坐标
        x3 = snapped_world.x - view.offset_x;
        y3 = snapped_world.y - view.offset_y;
    }

    // 3. 三点定圆数学解算 (View Space)
    double D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));

    // 共线判定：如果三点太接近一条直线，清空预览并返回
    if (std::abs(D) < 1e-9) {
        graph.preview_points.clear();
        return;
    }

    double s1 = x1 * x1 + y1 * y1;
    double s2 = x2 * x2 + y2 * y2;
    double s3 = x3 * x3 + y3 * y3;

    // 计算圆心相对坐标
    double cx_v = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / D;
    double cy_v = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / D;

    // 计算半径
    double r = std::hypot(cx_v - x1, cy_v - y1);

    // 4. 运行 Plotter 生成采样点

    tbb::concurrent_bounded_queue<std::vector<PointData>> q;



    PlotCircle(&q, cx_v, cy_v, r, view,0,0,true);

    // 5. 提取结果
    q.try_pop(graph.preview_points);



}


uint32_t EndCircle_3Points_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点
    if (graph.is_alive(selected_id)) {
        const auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            graph.preview_registers[2] = selected_id;
        }
    } else {
        auto new_point = CreatePoint_Interact(graph);
        graph.preview_registers[2] = new_point;
    }






    GeoFactory::CreateCircle_3Points(graph,graph.preview_registers[0],graph.preview_registers[1],graph.preview_registers[2],graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;


}