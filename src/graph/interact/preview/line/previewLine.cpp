//
// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/line/previewLine.h"
void PreviewLine_Intertact(GeometryGraph& graph)
{
    auto id = graph.preview_registers[0];

    if (!graph.is_alive(id) ) {
        graph.preview_points.clear();
        return;
    }
    auto& node = graph.get_node_by_id(id);
    if (GeoType::is_point(node.type) && node.error_status == GeoErrorStatus::VALID) {
        const auto& view = graph.view;

        uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式
        double point2_x{};
        double point2_y{};
        bool need_snap = true;





        if (graph.is_alive(id) && selected_id != id) {
            if (graph.is_alive(selected_id)) {
                const auto& selected_node = graph.get_node_by_id(selected_id);
                if (GeoType::is_point(selected_node.type)) {
                    double temp_point2_x = selected_node.result.x_view;
                    double temp_point2_y = selected_node.result.y_view;
                    point2_x = temp_point2_x;
                    point2_y = temp_point2_y;
                    need_snap = false;
                }
            }
        }

        if (need_snap) {
            Vec2 WorldMousePostion = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);

            Vec2 SnappedMousePostion = SnapToGrid_Interact(graph,WorldMousePostion);
            point2_x = SnappedMousePostion.x - view.offset_x;
            point2_y = SnappedMousePostion.y - view.offset_y;
        }




        double point_x = node.result.x_view;
        double point_y = node.result.y_view;
        tbb::concurrent_bounded_queue<std::vector<PointData>> q;
        process_two_point_line(q, point_x, point_y,
                       point2_x, point2_y,
                       LinePlotType::LINE, view);


        q.try_pop(graph.preview_points);




    }
}

uint32_t EndLine_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式


    if (graph.is_alive(selected_id)) {
        const auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            graph.preview_registers[1] = selected_id;
        }
    } else {
        auto new_point = CreatePoint_Interact(graph);

        graph.preview_registers[1] = new_point;
    }


    // 3. 如果没有选中有效的点，则创建一个新的点
    // AddPoint_Interact 现在会返回新创建点的ID

    GeoFactory::CreateLine(graph,graph.preview_registers[0],graph.preview_registers[1],graph.preview_visual_config);
    CancelPreview_Intectact(graph);
    return 0;


}


uint32_t InitLine_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点

    if (graph.is_alive(selected_id)) {
        auto &selected_node = graph.get_node_by_id(selected_id);
        if (GeoType::is_point(selected_node.type)) {
            selected_node.state_mask |= IS_SELECTED;
            graph.preview_func = PreviewLine_Intertact;
            graph.preview_type = GeoType::LINE_STRAIGHT;
            graph.preview_registers[0] = selected_id;
            graph.next_interact_func = EndLine_Interact;
            return selected_id; // 成功选中一个点，返回其ID
        }
    } else {
        // 3. 如果没有选中有效的点，则创建一个新的点
        // AddPoint_Interact 现在会返回新创建点的ID
        auto new_point = CreatePoint_Interact(graph);
        graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
        graph.preview_func = PreviewLine_Intertact;
        graph.preview_type = GeoType::LINE_STRAIGHT;
        graph.preview_registers[0] = new_point;
        graph.next_interact_func = EndLine_Interact;
    }




    return graph.preview_registers[0];

}