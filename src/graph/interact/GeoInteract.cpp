#include "../include/graph/interact/GeoInteract.h"

#include "plot/plotCircle.h"

uint32_t CreatePoint_Interact(GeometryGraph& graph) {
    const auto& view = graph.view;
    const auto& points = graph.final_points_buffer;

    // 1. 获取网格吸附后的世界坐标
    Vec2 world_coords = view.ScreenToWorld(graph.mouse_position.x, graph.mouse_position.y);
    Vec2 world_snapped_coords = SnapToGrid_Interact(graph, world_coords);

    // 2. 将吸附后的坐标映射到 int16 剪裁空间
    Vec2i target_clip = view.WorldToClip(world_snapped_coords.x, world_snapped_coords.y);

    // 3. 计算剪裁空间下的阈值平方 (5 像素)
    // 使用 s2c_scale_y 作为一个近似的剪裁空间单位，并计算其平方
    double clip_threshold = 5.0 * view.s2c_scale_y;
    auto threshold_sq = static_cast<int64_t>(clip_threshold * clip_threshold);

    // 4. 识别点击位置附近的非点、非标量节点 ID
    std::vector<uint32_t> intersection_candidates_ids;

    for (const auto& node : graph.node_pool) {
        // 只有活跃且可绘制的非点、非标量节点才参与判定
        if (!graph.is_alive(node.id) || !(node.state_mask & IS_VISIBLE) ||
            node.current_point_count == 0 || GeoType::is_scalar(node.type) || GeoType::is_point(node.type)) {
            continue;
        }

        bool node_hit = false;
        // 遍历节点的采样点数据
        for (uint32_t i = 0; i < node.current_point_count; ++i) {
            const auto& pt = points[node.buffer_offset + i];

            // 纯整数空间距离平方判定
            int64_t dx = static_cast<int32_t>(pt.x) - static_cast<int32_t>(target_clip.x);
            int64_t dy = static_cast<int32_t>(pt.y) - static_cast<int32_t>(target_clip.y);

            if (dx * dx + dy * dy <= threshold_sq) {
                node_hit = true;
                break;
            }
        }

        if (node_hit) {
            intersection_candidates_ids.push_back(node.id);
        }
    }

    // 5. 准备锚点公式（用于 FreePoint/ConstrainedPoint 的初始位置，以及 Intersect 的猜测值）
    // 注意：这里使用鼠标点击前的原始世界坐标，因为吸附后的坐标更精确
    std::string x_str = std::to_string(world_snapped_coords.x);
    std::string y_str = std::to_string(world_snapped_coords.y);

    size_t hit_count = intersection_candidates_ids.size();

    // ==========================================================
    // 6. 核心决策：优先使用解析交点 (POINT_INTERSECT)
    // ==========================================================
    if (hit_count == 2) {
        uint32_t id1 = intersection_candidates_ids[0];
        uint32_t id2 = intersection_candidates_ids[1];

        const auto& n1 = graph.get_node_by_id(id1);
        const auto& n2 = graph.get_node_by_id(id2);

        // 判断组合是否为可解析的几何体（线或圆）
        bool is_parseable =
            (GeoType::is_line(n1.type) || GeoType::is_circle(n1.type)) &&
            (GeoType::is_line(n2.type) || GeoType::is_circle(n2.type));

        if (is_parseable) {
            // 情况 A: 命中两个可解析对象 (线线, 线圆, 圆圆) -> 创建解析交点
            return GeoFactory::CreateIntersection(graph, id1, id2, x_str, y_str, {});
        }
        // 如果是两个不可解析的对象（如两个隐函数），则降级到情况 B/C
    }

    // ==========================================================
    // 7. 降级决策
    // ==========================================================
    if (hit_count == 0) {
        // 情况 B: 命中 0 个对象 -> 创建自由点 (Free Point)
        return GeoFactory::CreateFreePoint(graph, x_str, y_str);
    }

    if (hit_count == 1) {
        // 情况 C: 命中 1 个对象 -> 创建约束点 (Constrained Point)
        return GeoFactory::CreateConstrainedPoint(graph, intersection_candidates_ids[0], x_str, y_str);
    }

    // 情况 D: 命中 3 个及以上，或命中 2 个不可解析对象 -> 创建图解交点
    // 图解交点适用于多对象求交，或处理不可解析的复杂曲线
    return GeoFactory::CreateGraphicalIntersection(graph, intersection_candidates_ids, x_str, y_str, {});
}


uint32_t TrySelect_Interact(GeometryGraph& graph, bool is_multi_select) {
    // 1. 如果不是多选模式，清除所有节点的选中状态
    if (!is_multi_select) {
        for (auto& node : graph.node_pool) {
            if (graph.is_alive(node.id)) {
                node.state_mask &= ~IS_SELECTED;
            }
        }
    }

    const auto& view = graph.view;
    const auto& points_buffer = graph.final_points_buffer;

    // 2. 将鼠标屏幕位置映射到 int16 剪裁空间
    Vec2i target_clip = view.ScreenToClip(graph.mouse_position.x, graph.mouse_position.y);

    uint32_t nearest_id = 0;
    int64_t min_dist_sq = std::numeric_limits<int64_t>::max();

    // 3. 遍历所有活跃节点，寻找最近的符合条件的节点
    for (auto& node : graph.node_pool) {
        if (!graph.is_alive(node.id) || !(node.state_mask & IS_VISIBLE)) continue;

        int32_t current_threshold_sq = 0;
        if (GeoType::is_point(node.type)) {
            // 点对象：10 像素容忍度
            double clip_threshold = 10.0 * view.s2c_scale_y;
            current_threshold_sq = static_cast<int32_t>(clip_threshold * clip_threshold);

            // 对于点对象，直接使用其自身的中心点坐标
            Vec2i node_clip_pos = view.WorldToClip(node.result.x, node.result.y);
            int64_t dx = static_cast<int64_t>(node_clip_pos.x) - target_clip.x;
            int64_t dy = static_cast<int64_t>(node_clip_pos.y) - target_clip.y;
            int64_t d2 = dx * dx + dy * dy;

            if (d2 <= current_threshold_sq && d2 < min_dist_sq) {
                min_dist_sq = d2;
                nearest_id = node.id;
            }

        } else if (!GeoType::is_scalar(node.type)) {
            // 非点、非标量对象：5 像素容忍度
            double clip_threshold = 5.0 * view.s2c_scale_y;
            current_threshold_sq = static_cast<int32_t>(clip_threshold * clip_threshold);

            // 遍历节点的渲染点数据
            uint32_t start_idx = node.buffer_offset;
            uint32_t end_idx = start_idx + node.current_point_count;
            for (uint32_t i = start_idx; i < end_idx; ++i) {
                const auto& pt = points_buffer[i];
                if (pt.x == graph.view.MAGIC_CLIP_X) continue; // 跳过无效点

                int64_t dx = static_cast<int64_t>(pt.x) - target_clip.x;
                int64_t dy = static_cast<int64_t>(pt.y) - target_clip.y;
                int64_t d2 = dx * dx + dy * dy;

                if (d2 <= current_threshold_sq && d2 < min_dist_sq) {
                    min_dist_sq = d2;
                    nearest_id = node.id;
                }
            }
        }
    }

    // 4. 更新选中状态并返回ID

    return nearest_id;
}







/**
 * @brief 仅吸附主网格(Major Grid)交点
 * 直接复用 CalculateGridStep 获取主网格步长
 */
Vec2 SnapToGrid_Interact(const GeometryGraph& graph, Vec2 world_coord) {
    const auto& view = graph.view;

    // 1. 获取主网格步长 (Major Step)
    // 此时 snap_step 严格等于渲染层中的 major_step
    double snap_step = CalculateGridStep(view.wpp);

    // 2. 计算最近的 Major 坐标倍数
    double snapped_x = std::round(world_coord.x / snap_step) * snap_step;
    double snapped_y = std::round(world_coord.y / snap_step) * snap_step;

    // 3. 计算吸附阈值（10 屏幕像素）
    double threshold = 10.0 * view.wpp;
    double dx = world_coord.x - snapped_x;
    double dy = world_coord.y - snapped_y;

    // 4. 执行吸附判定
    // 使用距离平方判定，效率更高
    if ((dx * dx + dy * dy) <= (threshold * threshold)) {
        return { snapped_x, snapped_y };
    }

    // 移除 else 冗余：若未进入 if 块，自然返回原始坐标
    return world_coord;
}







void CancelPreview_Intectact(GeometryGraph& graph) {
    graph.preview_func = nullptr;
    graph.next_interact_func = nullptr;
    graph.preview_type = GeoType::UNKNOWN;
    graph.preview_registers.clear();
    graph.preview_points.clear();
    for (auto& node : graph.node_pool) {
        node.state_mask &= ~IS_SELECTED;
    }
    graph.preview_status = GeoErrorStatus::VALID;
    for (auto& ch : graph.preview_channels) {
        ch.clear();
    }
}


void UpdateMousePos_Interact(GeometryGraph& graph,double x,double y) {
    graph.mouse_position = {x,y};
}



void UpdatePreviewFormula_Interact(GeometryGraph& graph,std::string& r,int32_t idx) {
    graph.preview_channels[idx].original_infix = r;
}

