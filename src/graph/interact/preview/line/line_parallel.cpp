#include "../include/graph/interact/preview/line/line_parallel.h"

// 内部步骤声明

// =========================================================
// 初始化：识别途径 A 或 B
// =========================================================
uint32_t InitParallelLine_Interact(GeometryGraph& graph) {
    uint32_t sel_id = TrySelect_Interact(graph, false);
    graph.preview_registers.assign(2, 0); 

    if (graph.is_alive(sel_id) && GeoType::is_line(graph.get_node_by_id(sel_id).type)) {
        // --- 途径 A：先选了线 ---
        graph.get_node_by_id(sel_id).state_mask |= IS_SELECTED;
        graph.preview_registers[1] = sel_id; // 线存入 index 1
        
        graph.preview_type = GeoType::LINE_PARALLEL;
        graph.preview_func = PreviewParallelLine_Intertact; 
        graph.next_interact_func = EndParallelLine_Interact; 
        return sel_id;
    }
    // --- 途径 B：先选点（或空白处创建点） ---
    uint32_t p_id = sel_id;
    if (!graph.is_alive(p_id) || !GeoType::is_point(graph.get_node_by_id(p_id).type)) {
        p_id = CreatePoint_Interact(graph);
    }
        
    graph.get_node_by_id(p_id).state_mask |= IS_SELECTED;
    graph.preview_registers[0] = p_id; // 点存入 index 0
        
    graph.preview_func = nullptr;
    graph.next_interact_func = InitParallelLine_PathB_Step2_Interact;
    return p_id;
}

// =========================================================
// 预览逻辑 (途径 A)
// =========================================================
void PreviewParallelLine_Intertact(GeometryGraph& graph) {
    uint32_t line_id = graph.preview_registers[1];
    if (!graph.is_alive(line_id)) return;

    const auto& l_res = graph.get_node_by_id(line_id).result;
    const auto& view = graph.view;

    // 1. 确定参考点 P 的坐标 (优先级：拾取点 > 网格吸附)
    double px = 0, py = 0;
    bool found_point = false;

    uint32_t hovered_id = TrySelect_Interact(graph, false);
    if (graph.is_alive(hovered_id) && GeoType::is_point(graph.get_node_by_id(hovered_id).type)) {
        px = graph.get_node_by_id(hovered_id).result.x_view;
        py = graph.get_node_by_id(hovered_id).result.y_view;
        found_point = true;
    }

    if (!found_point) {
        Vec2 mouse_w = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped_p = SnapToGrid_Interact(graph, mouse_w);
        px = snapped_p.x - view.offset_x;
        py = snapped_p.y - view.offset_y;
    }

    // 2. 几何计算 (方向向量 dx, dy)
    double dx = l_res.x2_view - l_res.x1_view;
    double dy = l_res.y2_view - l_res.y1_view;

    if (std::abs(dx) < 1e-10 && std::abs(dy) < 1e-10) return;

    // 3. 平行线另一个定义点 P2 = P + (dx, dy)
    double p2x = px + dx;
    double p2y = py + dy;

    // 4. 渲染
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;
    process_two_point_line(q, px, py, p2x, p2y, LinePlotType::LINE, view);

    graph.preview_points.clear();
    q.try_pop(graph.preview_points);
}

// =========================================================
// 途径 B 第二步：选择目标线
// =========================================================
uint32_t InitParallelLine_PathB_Step2_Interact(GeometryGraph& graph) {
    uint32_t sel_id = TrySelect_Interact(graph, false);

    // 判定：必须选到线，否则不处理，保持当前函数指针等待下次点击
    if (!graph.is_alive(sel_id) || !GeoType::is_line(graph.get_node_by_id(sel_id).type)) {
        return 0; 
    }

    uint32_t p_id = graph.preview_registers[0];
    GeoFactory::CreateParallelLine(graph, p_id, sel_id, graph.preview_visual_config);
    
    CancelPreview_Intectact(graph); 
    return sel_id;
}

// =========================================================
// 途径 A 结束：确定点位置
// =========================================================
uint32_t EndParallelLine_Interact(GeometryGraph& graph) {
    uint32_t p_id = TrySelect_Interact(graph, false);
    if (!graph.is_alive(p_id) || !GeoType::is_point(graph.get_node_by_id(p_id).type)) {
        p_id = CreatePoint_Interact(graph);
    }

    uint32_t l_id = graph.preview_registers[1];
    if (graph.is_alive(p_id) && graph.is_alive(l_id)) {
        GeoFactory::CreateParallelLine(graph, p_id, l_id, graph.preview_visual_config);
    }

    CancelPreview_Intectact(graph);
    return 0;
}