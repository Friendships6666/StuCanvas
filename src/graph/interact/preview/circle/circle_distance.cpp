//
// Created by hp on 2026/1/30.
//
#include "../include/graph/interact/preview/circle/circle_distance.h"

uint32_t InitCircle_Distance_Interact(GeometryGraph& graph) {
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto& selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
            graph.next_interact_func = InitCircle_Distance_2_Interact;
            graph.preview_registers[0] = selected_id;
            return selected_id; // 成功选中一个点，返回其ID
        }
        if (GeoType::is_circle(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
            graph.preview_func = PreviewCircle_Distance_Intertact;
            graph.next_interact_func = EndCircle_Distance_Interact;
            graph.preview_registers[0] = selected_id;
            return selected_id; // 成功选中一个点，返回其ID
        }
        if (selected_node.type == GeoType::LINE_SEGMENT) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
            graph.preview_func = PreviewCircle_Distance_Intertact;
            graph.next_interact_func = EndCircle_Distance_Interact;
            graph.preview_registers[0] = selected_id;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
        graph.next_interact_func = InitCircle_Distance_2_Interact;
        graph.preview_registers[0] = selected_id;
    }




    return graph.preview_registers[0];
}
void PreviewCircle_Distance_Intertact(GeometryGraph& graph) {
    const auto& node_0 = graph.get_node_by_id(graph.preview_registers[0]);
    const auto& node_1 = graph.get_node_by_id(graph.preview_registers[1]);
    double radius = 0;
    if (GeoType::is_point(node_0.type) && GeoType::is_point(node_1.type)) {

        double x1 = node_0.result.x_view;
        double y1 = node_0.result.y_view;
        double x2 = node_1.result.x_view;
        double y2 = node_1.result.y_view;
        double dx = x2 - x1;
        double dy = y2 - y1;
        radius = dx*dx+dy*dy;

    }

    if (GeoType::is_circle(node_0.type)) {
        radius = node_1.result.cr;
    }

    if (node_0.type == GeoType::LINE_SEGMENT) {
        const auto& node_0_parents_0 = graph.get_node_by_id(node_0.parents[0]);
        const auto& node_0_parents_1 = graph.get_node_by_id(node_0.parents[1]);
        double x1 = node_0_parents_0.result.x_view;
        double y1 = node_0_parents_0.result.y_view;
        double x2 = node_0_parents_1.result.x_view;
        double y2 = node_0_parents_1.result.y_view;
        double dx = x2 - x1;
        double dy = y2 - y1;
        radius = dx*dx+dy*dy;
    }
    const auto& view = graph.view;
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;
    Vec2 mouse_pos = view.ScreenToWorldNoOffset(graph.mouse_position.x, graph.mouse_position.y);
    PlotCircle(&q,mouse_pos.x,mouse_pos.y,radius,view);
    q.try_pop(graph.preview_points);






}


uint32_t InitCircle_Distance_2_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  true); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_func = PreviewCircle_Distance_Intertact;
            graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
            graph.preview_registers[1] = selected_id;
            graph.next_interact_func = EndCircle_Distance_Interact;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_func = PreviewCircle_Distance_Intertact;
        graph.preview_type = GeoType::CIRCLE_FULL_DISTANCE;
        graph.preview_registers[1] = new_point;
        graph.next_interact_func = EndCircle_Distance_Interact;
    }




    return graph.preview_registers[1];
}

uint32_t EndCircle_Distance_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式


    if (graph.is_alive(selected_id)) {
        const auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            graph.preview_registers[2] = selected_id;
        }
    } else {
        auto new_point = CreatePoint_Interact(graph);
        graph.preview_registers[2] = new_point;
    }
    const auto& node_0 = graph.get_node_by_id(graph.preview_registers[0]);
    const auto& node_1 = graph.get_node_by_id(graph.preview_registers[1]);
    std::string radius;

    if (GeoType::is_circle(node_0.type)) {
        double r_world = node_0.result.cr;
        radius = std::to_string(r_world);
    }

    if (node_0.type == GeoType::LINE_SEGMENT) {
        uint32_t id1 = node_0.parents[0];
        uint32_t id2 = node_0.parents[1];
        std::string name1 = graph.get_node_by_id(id1).config.name;
        std::string name2 = graph.get_node_by_id(id2).config.name;
        radius = std::format("Length({},{})", name1, name2);
    }

    if (GeoType::is_point(node_0.type) && GeoType::is_point(node_1.type)) {



        std::string name1 = graph.get_node_by_id(node_0.id).config.name;
        std::string name2 = graph.get_node_by_id(node_1.id).config.name;
        radius = std::format("Length({},{})", name1, name2);


    }



    GeoFactory::CreateCircle_1Point_1Radius(graph,graph.preview_registers[2],radius,graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;




    // 3. 如果没有选中有效的点，则创建一个新的点
    // AddPoint_Interact 现在会返回新创建点的ID

}