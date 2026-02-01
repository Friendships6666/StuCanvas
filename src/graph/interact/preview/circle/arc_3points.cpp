#include "../include/graph/interact/preview/circle/arc_3points.h"



uint32_t PreviewInitArc_3Points_Interact(GeometryGraph& graph) {
    // 1. 尝试选择现有对象
    uint32_t selected_id = TrySelect_Interact(graph, false);

    // 2. 如果没选到，则在点击/吸附位置创建一个新点
    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers.resize(3, 0); 
        graph.preview_registers[0] = selected_id;
        
        // 指向下一步：选择起点
        graph.next_interact_func = PreviewInitArc_3Points_2_Interact;
    }
    return selected_id;
}


uint32_t PreviewInitArc_3Points_2_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph, true);

    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers[1] = selected_id;

        // 核心：设置预览状态
        graph.preview_type = GeoType::ARC_3POINTS;
        graph.preview_func = PreviewArc_3Points_Interact;     // 每帧执行绘图
        graph.next_interact_func = EndArc_3Points_Interact;  // 点击后终结
    }
    return selected_id;
}


void PreviewArc_3Points_Interact(GeometryGraph& graph) {
    uint32_t id_center = graph.preview_registers[0];
    uint32_t id_start = graph.preview_registers[1];

    if (!graph.is_alive(id_center) || !graph.is_alive(id_start)) return;

    const auto& p0 = graph.get_node_by_id(id_center).result;
    const auto& p1 = graph.get_node_by_id(id_start).result;
    const auto& view = graph.view;

    // --- 确定虚拟第三点 (方向点) 的位置 ---
    double x_dir, y_dir;

    // A. 尝试对象吸附 (查找鼠标附近的已有几何点)
    uint32_t hovered_id = TrySelect_Interact(graph, true);
    if (hovered_id != 0 && hovered_id != id_center && graph.is_alive(hovered_id)) {
        const auto& node_h = graph.get_node_by_id(hovered_id);
        if (GeoType::is_point(node_h.type)) {
            x_dir = node_h.result.x;
            y_dir = node_h.result.y;
            goto calc_arc; // 找到物理点，跳过后续吸附
        }
    }

    // B. 尝试网格吸附与鼠标跟随
    {
        Vec2 mouse_w = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped_w = SnapToGrid_Interact(graph, mouse_w);
        x_dir = snapped_w.x;
        y_dir = snapped_w.y;
    }

    calc_arc:
        // --- 几何计算 ---
        double dx1 = p1.x - p0.x;
    double dy1 = p1.y - p0.y;
    double r = std::hypot(dx1, dy1);

    double dx2 = x_dir - p0.x;
    double dy2 = y_dir - p0.y;

    if (r < 1e-7 || std::hypot(dx2, dy2) < 1e-7) {
        graph.preview_points.clear();
        return;
    }

    // 计算弧度范围 (基于 atan2 的逆时针逻辑)
    double t_start = std::atan2(dy1, dx1);
    double t_end = std::atan2(dy2, dx2);

    // --- 渲染预览点 ---
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;
    PlotCircle(&q, p0.x_view, p0.y_view, r, view, t_start, t_end, false);

    graph.preview_points.clear();
    std::vector<PointData> temp_pts;
    while (q.try_pop(temp_pts)) {
        graph.preview_points.insert(graph.preview_points.end(), temp_pts.begin(), temp_pts.end());
    }
}


uint32_t EndArc_3Points_Interact(GeometryGraph& graph) {
    uint32_t id_center = graph.preview_registers[0];
    uint32_t id_start = graph.preview_registers[1];

    // 确定第三个物理点
    uint32_t id_end = TrySelect_Interact(graph, false);

    // 如果点击处没有现有高亮显示的点，则在此处通过点击（含吸附）创建一个新点作为方向标
    if (!graph.is_alive(id_end) || !GeoType::is_point(graph.get_node_by_id(id_end).type)) {
        id_end = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(id_center) && graph.is_alive(id_start) && graph.is_alive(id_end)) {
        // 调用工厂创建 ARC_3POINTS
        GeoFactory::CreateArc_3Points(
            graph,
            id_center,
            id_start,
            id_end,
            graph.preview_visual_config
        );
    }

    // 清理预览和交互挂载
    CancelPreview_Intectact(graph);
    return 0;
}