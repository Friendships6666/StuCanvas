#include "../include/graph/interact/preview/line/line_vertical.h"




// =========================================================
// 第一步：初始化（识别用户选了什么）
// =========================================================
uint32_t InitVerticalLine_Interact(GeometryGraph& graph) {
    uint32_t sel_id = TrySelect_Interact(graph, false);
    graph.preview_registers.assign(2, 0); // 清空并分配 2 个槽位

    if (graph.is_alive(sel_id) && GeoType::is_line(graph.get_node_by_id(sel_id).type)) {
        // --- 途径 A：先选了线 ---
        graph.get_node_by_id(sel_id).state_mask |= IS_SELECTED;
        graph.preview_registers[1] = sel_id; // 存入线槽

        graph.preview_type = GeoType::LINE_VERTICAL;
        graph.preview_func = PreviewVerticalLine_Interact; // 开启预览
        graph.next_interact_func = EndVerticalLine_Interact; // 下一次点击创建点并结束
        return sel_id;
    }
    // --- 途径 B：选了点，或者在空白处点击（创建点） ---
    uint32_t p_id = sel_id;
    if (!graph.is_alive(p_id) || !GeoType::is_point(graph.get_node_by_id(p_id).type)) {
        p_id = CreatePoint_Interact(graph);
    }

    graph.get_node_by_id(p_id).state_mask |= IS_SELECTED;
    graph.preview_registers[0] = p_id; // 存入点槽

    graph.preview_func = nullptr; // 途径 B 无预览
    graph.next_interact_func = InitVerticalLine_PathB_Step2_Interact; // 下一次点击去找线
    return p_id;
}

// =========================================================
// 预览逻辑（仅用于途径 A：鼠标作为点 P）
// =========================================================
void PreviewVerticalLine_Interact(GeometryGraph& graph) {
    uint32_t line_id = graph.preview_registers[1]; // 获取已选中的目标线
    if (!graph.is_alive(line_id)) return;

    const auto& l_res = graph.get_node_by_id(line_id).result;
    const auto& view = graph.view;

    // --- 1. 确定参考点 P 的坐标 (View Space) ---
    double px, py;
    bool found_point = false;

    // A. 尝试拾取已有的点对象 (容差通常由 TrySelect_Interact 内部处理)
    uint32_t hovered_id = TrySelect_Interact(graph, false);
    if (graph.is_alive(hovered_id)) {
        const auto& node = graph.get_node_by_id(hovered_id);
        if (GeoType::is_point(node.type)) {
            // 命中点对象，使用该点的精确位置
            px = node.result.x_view;
            py = node.result.y_view;
            found_point = true;
        }
    }

    // B. 如果没有选中点，则使用鼠标位置并进行网格吸附
    if (!found_point) {
        Vec2 mouse_w = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
        Vec2 snapped_p = SnapToGrid_Interact(graph, mouse_w);
        px = snapped_p.x - view.offset_x;
        py = snapped_p.y - view.offset_y;
    }

    // --- 2. 几何投影计算垂足 H ---
    double x1 = l_res.x1_view; double y1 = l_res.y1_view;
    double x2 = l_res.x2_view; double y2 = l_res.y2_view;
    double dx = x2 - x1; double dy = y2 - y1;
    double len_sq = dx * dx + dy * dy;

    if (len_sq < 1e-10) {
        graph.preview_points.clear();
        return;
    }

    // 计算投影参数 t = (PA · AB) / |AB|^2
    double t = ((px - x1) * dx + (py - y1) * dy) / len_sq;
    double hx = x1 + t * dx;
    double hy = y1 + t * dy;

    // --- 3. 处理点 P 在直线上的退化情况 ---
    // 如果 P 点和垂足 H 重合，利用目标线的法向量 (-dy, dx) 确定垂线方向
    if (std::abs(px - hx) < 1e-6 && std::abs(py - hy) < 1e-6) {
        hx = px - dy;
        hy = py + dx;
    }

    // --- 4. 生成预览采样点 ---
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;

    // 使用 LINE 类型进行无限延伸绘制
    process_two_point_line(
        q,
        px, py,    // 点 P
        hx, hy,    // 垂足 H (作为方向参考)
        LinePlotType::LINE,
        view
    );

    // 5. 结果交换
    graph.preview_points.clear();
    q.try_pop(graph.preview_points);
}

// =========================================================
// 途径 B 的第二步：等待用户选线
// =========================================================
uint32_t InitVerticalLine_PathB_Step2_Interact(GeometryGraph& graph) {
    uint32_t sel_id = TrySelect_Interact(graph, false);

    // 判定：必须选到线
    if (!graph.is_alive(sel_id) || !GeoType::is_line(graph.get_node_by_id(sel_id).type)) {
        // 用户没点准，直接返回，不清理指针，允许再次尝试
        return 0;
    }

    // 选到了线
    uint32_t p_id = graph.preview_registers[0];
    GeoFactory::CreateVerticalLine(graph, p_id, sel_id, graph.preview_visual_config);

    CancelPreview_Intectact(graph); // 正常结束
    return sel_id;
}

// =========================================================
// 途径 A 的结束：为鼠标位置确定一个点
// =========================================================
uint32_t EndVerticalLine_Interact(GeometryGraph& graph) {
    // 确定点 ID (优先吸附已有，否则创建)
    uint32_t p_id = TrySelect_Interact(graph, false);
    if (!graph.is_alive(p_id) || !GeoType::is_point(graph.get_node_by_id(p_id).type)) {
        p_id = CreatePoint_Interact(graph);
    }

    uint32_t l_id = graph.preview_registers[1];
    if (graph.is_alive(p_id) && graph.is_alive(l_id)) {
        GeoFactory::CreateVerticalLine(graph, p_id, l_id, graph.preview_visual_config);
    }

    CancelPreview_Intectact(graph);
    return 0;
}