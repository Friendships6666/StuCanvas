#include "../include/graph/interact/preview/circle/arc_3points_circumarc.h"

uint32_t PreviewInitArc_3Points_Circumarc_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers.resize(3, 0); 
        graph.preview_registers[0] = selected_id;
        
        // 进入第二步
        graph.next_interact_func = PreviewInitArc_3Points_Circumarc_2_Interact;
    }
    return selected_id;
}


uint32_t PreviewInitArc_3Points_Circumarc_2_Interact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph, true);

    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers[1] = selected_id;

        // 启动预览逻辑
        graph.preview_type = GeoType::ARC_3POINTS_CIRCUMARC;
        graph.preview_func = PreviewArc_3Points_Circumarc_Interact;
        graph.next_interact_func = EndArc_3Points_Circumarc_Interact;
    }
    return selected_id;
}


void PreviewArc_3Points_Circumarc_Interact(GeometryGraph& graph) {
    uint32_t id1 = graph.preview_registers[0];
    uint32_t id2 = graph.preview_registers[1];

    if (!graph.is_alive(id1) || !graph.is_alive(id2)) return;

    const auto& p1 = graph.get_node_by_id(id1).result;
    const auto& p2 = graph.get_node_by_id(id2).result;
    const auto& view = graph.view;

    // --- 确定虚拟第三点 P3 (鼠标位置) ---
    double x3, y3;
    uint32_t hovered_id = TrySelect_Interact(graph, true);

    if (hovered_id != 0 && graph.is_alive(hovered_id) &&
        GeoType::is_point(graph.get_node_by_id(hovered_id).type)) {
        x3 = graph.get_node_by_id(hovered_id).result.x;
        y3 = graph.get_node_by_id(hovered_id).result.y;
    } else {
        Vec2 mouse_w = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped_w = SnapToGrid_Interact(graph, mouse_w);
        x3 = snapped_w.x;
        y3 = snapped_w.y;
    }

    // --- 计算外接圆几何 (基于 View Space 保证精度) ---
    double x1_v = p1.x_view, y1_v = p1.y_view;
    double x2_v = p2.x_view, y2_v = p2.y_view;
    double x3_v = x3 - view.offset_x, y3_v = y3 - view.offset_y;

    double D = 2 * (x1_v * (y2_v - y3_v) + x2_v * (y3_v - y1_v) + x3_v * (y1_v - y2_v));

    if (std::abs(D) < 1e-9) {
        graph.preview_points.clear();
        return; // 共线无法成圆
    }

    double s1 = x1_v * x1_v + y1_v * y1_v;
    double s2 = x2_v * x2_v + y2_v * y2_v;
    double s3 = x3_v * x3_v + y3_v * y3_v;

    double cx_v = (s1 * (y2_v - y3_v) + s2 * (y3_v - y1_v) + s3 * (y1_v - y2_v)) / D;
    double cy_v = (s1 * (x3_v - x2_v) + s2 * (x1_v - x3_v) + s3 * (x2_v - x1_v)) / D;
    double r = std::hypot(cx_v - x1_v, cy_v - y1_v);

    // 计算三点极角
    double t1 = std::atan2(y1_v - cy_v, x1_v - cx_v);
    double t2 = std::atan2(y2_v - cy_v, x2_v - cx_v);
    double t3 = std::atan2(y3_v - cy_v, x3_v - cx_v);

    // --- 判定 CCW 扫描区间 (确保 P2 在 P1->P3 的路径上) ---
    auto is_ccw_between = [](double a, double start, double end) {
        const double TWO_PI = 6.283185307179586;
        auto norm = [&](double ang) {
            double res = std::fmod(ang, TWO_PI);
            if (res < 0) res += TWO_PI;
            return res;
        };
        a = norm(a); start = norm(start); end = norm(end);
        if (start <= end) return a >= start && a <= end;
        return a >= start || a <= end;
    };

    double final_t_start, final_t_end;
    if (is_ccw_between(t2, t1, t3)) {
        final_t_start = t1; final_t_end = t3;
    } else {
        final_t_start = t3; final_t_end = t1;
    }

    // --- 渲染 ---
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;
    PlotCircle(&q, cx_v, cy_v, r, view, final_t_start, final_t_end, false);

    graph.preview_points.clear();

    q.try_pop(graph.preview_points);

}

uint32_t EndArc_3Points_Circumarc_Interact(GeometryGraph& graph) {
    uint32_t id1 = graph.preview_registers[0];
    uint32_t id2 = graph.preview_registers[1];

    // 获取/创建第三个物理点
    uint32_t id3 = TrySelect_Interact(graph, false);
    if (!graph.is_alive(id3) || !GeoType::is_point(graph.get_node_by_id(id3).type)) {
        id3 = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(id1) && graph.is_alive(id2) && graph.is_alive(id3)) {
        GeoFactory::CreateArc_3Points_Circumarc(
            graph,
            id1,
            id2,
            id3,
            graph.preview_visual_config
        );
    }

    CancelPreview_Intectact(graph);
    return 0;
}