//
// Created by hp on 2026/1/30.
//
#include "../include/graph/interact/preview/circle/circle_distance.h"

uint32_t InitCircle_Distance_Interact(GeometryGraph& graph) {
    uint32_t id = TrySelect_Interact(graph, false);

    // 1. 核心规则：如果没有选中有效对象，或者选中的对象不是我们想要的，则强制创建/视为点
    bool is_dist_provider = graph.is_alive(id) &&
                            (GeoType::is_circle(graph.get_node_by_id(id).type) ||
                             graph.get_node_by_id(id).type == GeoType::LINE_SEGMENT);

    if (!graph.is_alive(id) || (!is_dist_provider && !GeoType::is_point(graph.get_node_by_id(id).type))) {
        id = CreatePoint_Interact(graph);
    }

    // 2. 统一公共状态配置
    auto& node = graph.get_node_by_id(id);
    node.state_mask |= IS_SELECTED;

    graph.preview_registers.resize(3);
    graph.preview_registers[0] = id;
    graph.preview_type = GeoType::CIRCLE_DISTANCE;

    // 3. 语义路径分配：根据对象属性决定交互深度
    if (is_dist_provider) {
        // 路径 A：对象自带长度（圆半径/线段长），直接预览放置位置
        graph.preview_func      = PreviewCircle_Distance_Intertact;
        graph.next_interact_func = EndCircle_Distance_Interact;
    } else {
        // 路径 B：选中的是点或新创的点，跳转到第二步选第二个点以确定距离
        graph.next_interact_func = InitCircle_Distance_2_Interact;
    }

    return id;
}
void PreviewCircle_Distance_Intertact(GeometryGraph& graph) {
    // 1. 获取核心参考对象
    const auto& node0 = graph.get_node_by_id(graph.preview_registers[0]);
    double radius = 0.0;

    // 2. 多模态半径解算 (Strategy-based radius derivation)
    if (GeoType::is_circle(node0.type)) {
        // 模式 A: 直接提取圆半径
        radius = node0.result.cr;
    }
    else if (node0.type == GeoType::LINE_SEGMENT) {
        // 模式 B: 计算线段长度作为半径
        const auto& p1 = graph.get_node_by_id(node0.parents[0]).result;
        const auto& p2 = graph.get_node_by_id(node0.parents[1]).result;
        radius = std::hypot(p1.x_view - p2.x_view, p1.y_view - p2.y_view);
    }
    else if (GeoType::is_point(node0.type)) {
        // 模式 C: 点对点距离 (需要检查第二个寄存器)
        const auto& node1 = graph.get_node_by_id(graph.preview_registers[1]);
        if (graph.is_alive(node1.id) && GeoType::is_point(node1.type)) {
            radius = std::hypot(node0.result.x_view - node1.result.x_view,
                                node0.result.y_view - node1.result.y_view);
        }
    }

    // 安全检查：半径过小或无效时不进行渲染
    if (radius < 1e-7 || !std::isfinite(radius)) {
        graph.preview_points.clear();
        return;
    }

    // 3. 渲染预览圆 (跟随鼠标)
    const auto& view = graph.view;
    tbb::concurrent_bounded_queue<std::vector<PointData>> q;

    // 将屏幕鼠标位置转为视口相对坐标 (View Space)
    Vec2 mouse_v = view.ScreenToWorldNoOffset(graph.mouse_position.x, graph.mouse_position.y);

    // 执行绘图
    PlotCircle(&q, mouse_v.x, mouse_v.y, radius, view, 0, 0, true);

    // 4. 结果收割
    graph.preview_points.clear();

    q.try_pop(graph.preview_points);

}


uint32_t InitCircle_Distance_2_Interact(GeometryGraph& graph) {
    // 1. 确定第二个参考点 ID (优先选择已有，若选中的不是点则在点击处创建)
    uint32_t p2_id = TrySelect_Interact(graph, true);

    if (!graph.is_alive(p2_id) || !GeoType::is_point(graph.get_node_by_id(p2_id).type)) {
        p2_id = CreatePoint_Interact(graph);
    }

    // 2. 统一配置选中状态与交互寄存器
    graph.get_node_by_id(p2_id).state_mask |= IS_SELECTED;
    graph.preview_registers[1] = p2_id;

    // 3. 挂载预览函数并指向终点阶段
    // 此时距离已经可以计算（p1来自上一步，p2来自本步），开始跟随鼠标预览圆心
    graph.preview_type      = GeoType::CIRCLE_DISTANCE;
    graph.preview_func      = PreviewCircle_Distance_Intertact;
    graph.next_interact_func = EndCircle_Distance_Interact;

    return p2_id;
}

uint32_t EndCircle_Distance_Interact(GeometryGraph& graph) {
    // 1. 确定最终圆心的点 ID (优先选择已有，若选中的不是点则创建)
    uint32_t center_id = TrySelect_Interact(graph, false);

    if (!graph.is_alive(center_id) || !GeoType::is_point(graph.get_node_by_id(center_id).type)) {
        center_id = CreatePoint_Interact(graph);
    }

    // 2. 根据参考对象生成半径公式 (Radius Formula Generation)
    const auto& node0 = graph.get_node_by_id(graph.preview_registers[0]);
    std::string radius_expr;

    if (GeoType::is_circle(node0.type)) {
        // 模式 A: 引用现有圆的半径数值
        radius_expr = std::to_string(node0.result.cr);
    }
    else if (node0.type == GeoType::LINE_SEGMENT) {
        // 模式 B: 引用线段长度函数
        const std::string& name1 = graph.get_node_by_id(node0.parents[0]).config.name;
        const std::string& name2 = graph.get_node_by_id(node0.parents[1]).config.name;
        radius_expr = std::format("Length({},{})", name1, name2);
    }
    else if (GeoType::is_point(node0.type)) {
        // 模式 C: 引用两个点之间的距离函数
        const auto& node1 = graph.get_node_by_id(graph.preview_registers[1]);
        if (graph.is_alive(node1.id)) {
            radius_expr = std::format("Length({},{})", node0.config.name, node1.config.name);
        }
    }

    // 3. 健壮性检查：如果公式生成失败，不创建对象
    if (!radius_expr.empty()) {
        GeoFactory::CreateCircle_1Point_1Radius(
            graph,
            center_id,
            radius_expr,
            graph.preview_visual_config
        );
    }

    // 4. 统一清理交互状态
    CancelPreview_Intectact(graph);

    return 0;
}