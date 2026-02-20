//
// Created by hp on 2026/1/29.

// Created by hp on 2026/1/29.
//
#include "../include/graph/interact/preview/circle/circle_3points.h"
uint32_t InitCircle_3Points_Interact(GeometryGraph& graph) {
    // 1. 确定第一个点 ID (优先选择已有，否则创建)
    uint32_t p1_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(p1_id) || !GeoType::is_point(graph.get_node_by_id(p1_id).type)) {
        p1_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置选中状态
    graph.get_node_by_id(p1_id).state_mask |= IS_SELECTED;

    // 3. 初始化三点圆交互上下文
    graph.preview_registers.resize(3); // 三点定圆，预留三个寄存器
    graph.preview_registers[0] = p1_id;

    // 无论点是选取的还是新建的，都必须指向第二步
    graph.next_interact_func = InitCircle_3Points_2_Interact;

    return p1_id;
}


uint32_t InitCircle_3Points_2_Interact(GeometryGraph& graph) {
    // 1. 确定第二个点 ID (优先选择已有，若选中的不是点则创建)
    uint32_t p2_id = TrySelect_Interact(graph, true); // 3点圆通常需要高亮前两个点，故用多选

    if (!graph.is_alive(p2_id) || !GeoType::is_point(graph.get_node_by_id(p2_id).type)) {
        p2_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置选中状态
    graph.get_node_by_id(p2_id).state_mask |= IS_SELECTED;

    // 3. 配置三点圆的实时预览上下文
    graph.preview_registers[1] = p2_id;
    graph.preview_type         = GeoType::CIRCLE_3POINTS;
    graph.preview_func         = PreviewCircle_3Points_Intertact; // 开启实时预览
    graph.next_interact_func   = EndCircle_3Points_Interact;      // 指向最后一步

    return p2_id;
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
    // 1. 确定第三个点 ID (优先选择已有，若选中的不是点则创建)
    uint32_t p3_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(p3_id) || !GeoType::is_point(graph.get_node_by_id(p3_id).type)) {
        p3_id = CreatePoint_Interact(graph);
    }

    // 2. 调用工厂函数创建三点圆
    // 直接使用 p3_id 传参，逻辑更直观
    GeoFactory::CreateCircle_3Points(
        graph,
        graph.preview_registers[0],
        graph.preview_registers[1],
        p3_id,
        graph.preview_visual_config
    );

    // 3. 统一清理预览与交互状态
    CancelPreview_Intectact(graph);

    return 0;
}