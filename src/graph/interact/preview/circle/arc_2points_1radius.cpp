#include "../include/graph/interact/preview/circle/arc_2points_1radius.h"
uint32_t PreviewInitArc_2Points_1Radius_Intertact(GeometryGraph& graph) {
    // 1. 尝试选择现有对象
    uint32_t selected_id = TrySelect_Interact(graph, false);

    // 2. 如果没选到，则在点击位置创建一个新点
    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    // 3. 记录状态并指向下一个任务
    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers.resize(2, 0); // 确保空间足够
        graph.preview_registers[0] = selected_id;
        
        graph.next_interact_func = PreviewInitArc_2Points_1Radius_2_Intertact;
    }
    
    return selected_id;
}

uint32_t PreviewInitArc_2Points_1Radius_2_Intertact(GeometryGraph& graph) {
    uint32_t selected_id = TrySelect_Interact(graph, true); // 允许选择已选中的

    if (!graph.is_alive(selected_id) || !GeoType::is_point(graph.get_node_by_id(selected_id).type)) {
        selected_id = CreatePoint_Interact(graph);
    }

    if (graph.is_alive(selected_id)) {
        graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
        graph.preview_registers[1] = selected_id;

        // 核心：设置预览函数和最终执行函数
        graph.preview_type = GeoType::ARC_2POINTS_1RADIUS;
        graph.preview_func = PreviewArc_2Points_1Radius_Intertact;
        graph.next_interact_func = EndArc_2Points_1Radius_Intertact;
    }

    return selected_id;
}

void PreviewArc_2Points_1Radius_Intertact(GeometryGraph& graph) {
    uint32_t id1 = graph.preview_registers[0];
    uint32_t id2 = graph.preview_registers[1];

    if (!graph.is_alive(id1) || !graph.is_alive(id2)) return;

    const auto& p1 = graph.get_node_by_id(id1).result;
    const auto& p2 = graph.get_node_by_id(id2).result;
    const auto& view = graph.view;

    // 1. 编译并解算半径公式 (从预览通道获取)
    std::vector<uint32_t> dummy_parents;
    GeoFactory::CompileChannelInternal(graph, 0, 0, graph.preview_channels[0].original_infix, dummy_parents, true);

    if (graph.preview_status != GeoErrorStatus::VALID) {
        graph.preview_points.clear();
        return;
    }

    // 调用解算器逻辑（使用预览标志）
    double r_val = SolveChannel(GeometryGraph::NULL_NODE, 0, graph, true);

    // 2. 几何计算 (与 Solver 逻辑一致)
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double L_sq = dx * dx + dy * dy;
    double L = std::sqrt(L_sq);

    // 半径不足以跨越两点
    if (std::abs(r_val) < L / 2.0 || L < 1e-9) {
        graph.preview_points.clear();
        return;
    }

    double mx = (p1.x + p2.x) * 0.5;
    double my = (p1.y + p2.y) * 0.5;
    double abs_r = std::abs(r_val);
    double h = std::sqrt(abs_r * abs_r - L_sq / 4.0);

    // 垂直向量
    double ux = -dy / L;
    double uy = dx / L;

    // 根据半径正负号确定圆心位置
    double cx, cy;
    if (r_val > 0) {
        cx = mx + h * ux; cy = my + h * uy;
    } else {
        cx = mx - h * ux; cy = my - h * uy;
    }

    // 计算弧度范围
    double t_start = std::atan2(p1.y - cy, p1.x - cx);
    double t_end = std::atan2(p2.y - cy, p2.x - cx);

    // 3. 调用 Plotter 生成预览点
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;
    // 注意渲染使用相对视口坐标
    PlotCircle(&q, cx - view.offset_x, cy - view.offset_y, abs_r, view, t_start, t_end, false);

    // 提取结果到预览缓冲区
    graph.preview_points.clear();
    std::vector<PointData> temp_pts;
    while (q.try_pop(temp_pts)) {
        graph.preview_points.insert(graph.preview_points.end(), temp_pts.begin(), temp_pts.end());
    }
}


uint32_t EndArc_2Points_1Radius_Intertact(GeometryGraph& graph) {
    uint32_t id1 = graph.preview_registers[0];
    uint32_t id2 = graph.preview_registers[1];
    std::string radius_expr = graph.preview_channels[0].original_infix;

    if (graph.is_alive(id1) && graph.is_alive(id2) && !radius_expr.empty()) {
        // 调用工厂函数创建正式节点
        GeoFactory::CreateArc_2Points_1Radius(
            graph,
            id1,
            id2,
            radius_expr,
            graph.preview_visual_config
        );
    }

    // 清理预览状态，关闭交互模式
    CancelPreview_Intectact(graph);
    return 0;
}