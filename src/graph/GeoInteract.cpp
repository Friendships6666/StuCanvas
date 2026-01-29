#include "../../include/graph/GeoInteract.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/grids/grids.h"
#include "../../include/plot/plotSegment.h"
#include <vector>
#include <set>
#include <string>
#include <cmath>

uint32_t AddPoint_Interact(GeometryGraph& graph) {


    const auto& view = graph.view;
    const auto& points = graph.final_points_buffer;
    
    // 2. 将鼠标屏幕位置映射到 int16 剪裁空间 (仅转换一次)
    Vec2i target_clip = view.ScreenToClip(graph.mouse_position.x,graph.mouse_position.y);
    
    // 3. 计算剪裁空间下的阈值平方 (将 5 像素阈值转为剪裁空间单位)
    // 根据 ViewState 定义: s2c_scale 决定了像素到 int16 的映射比例
    // 这里使用 s2c_scale_y 作为一个近似，因为它可能是非均匀缩放的
    double clip_threshold = 5.0 * view.s2c_scale_y; 
    int64_t threshold_sq = static_cast<int64_t>(clip_threshold * clip_threshold);

    // 4. 识别点击位置附近的非点、非标量节点 ID 作为交点候选
    std::vector<uint32_t> intersection_candidates_ids;

    for (const auto& node : graph.node_pool) {
        // 只有活跃且有物理形状的非点、非标量节点才参与判定
        if (!graph.is_alive(node.id) || !(node.state_mask & IS_VISIBLE) || node.current_point_count == 0 || GeoType::is_scalar(node.type) || GeoType::is_point(node.type)) {
            continue;
        }

        bool node_hit = false;
        for (uint32_t i = 0; i < node.current_point_count; ++i) {
            const auto& pt = points[node.buffer_offset + i];
            

            // 💡 极致性能：纯整数空间判定
            // 使用 int32 存储差值及其平方，防止 int16 溢出
            int64_t dx = static_cast<int32_t>(pt.x) - static_cast<int32_t>(target_clip.x);
            int64_t dy = static_cast<int32_t>(pt.y) - static_cast<int32_t>(target_clip.y);
            
            // 欧式距离平方比较
            if (dx * dx + dy * dy <= threshold_sq) {
                node_hit = true;
                break;
            }
        }
        
        if (node_hit) {
            intersection_candidates_ids.push_back(node.id);

        }
    }

    // 5. 转换点击位置为世界坐标，生成公式字符串
    Vec2 world_pos = view.ScreenToWorld(graph.mouse_position.x,graph.mouse_position.y);
    std::string x_str = std::to_string(world_pos.x);
    std::string y_str = std::to_string(world_pos.y);


    if (intersection_candidates_ids.empty()) {
        return GeoFactory::AddFreePoint(graph, x_str, y_str);
    }

    if (intersection_candidates_ids.size() == 1) {
        return GeoFactory::AddConstrainedPoint(graph, intersection_candidates_ids[0], x_str, y_str);
    }

    // 情况 C: 命中多个对象
    return GeoFactory::AddGraphicalIntersection(graph, intersection_candidates_ids, x_str, y_str);
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

uint32_t InitSegment_Interact(GeometryGraph& graph) {
    // 1. 尝试选择已有的点
    // 假设 TrySelect_Interact 会处理 IS_SELECTED 掩码的设置
    uint32_t selected_id = TrySelect_Interact(graph,  false); // 非多选模式



    // 2. 检查选中的节点是否是一个点
    if (selected_id != 0) { // 假设 0 是 NULL_ID
        if (graph.is_alive(selected_id)) {
            const auto& selected_node = graph.get_node_by_id(selected_id);
            if (GeoType::is_point(selected_node.type)) {
                graph.get_node_by_id(selected_id).state_mask |= IS_SELECTED;
                graph.preview_func = PreviewSegment_Intertact;
                graph.preview_type = GeoType::LINE_SEGMENT;
                graph.preview_registers[0] = selected_id;
                return selected_id; // 成功选中一个点，返回其ID
            }
        }
    }

    // 3. 如果没有选中有效的点，则创建一个新的点
    // AddPoint_Interact 现在会返回新创建点的ID
    auto new_point = AddPoint_Interact(graph);
    graph.get_node_by_id(new_point).state_mask |= IS_SELECTED;
    graph.preview_func = PreviewSegment_Intertact;
    graph.preview_type = GeoType::LINE_SEGMENT;
    graph.preview_registers[0] = selected_id;

    return new_point;

}

/**
 * @brief 仅吸附主网格(Major Grid)交点
 * 直接复用 CalculateGridStep 获取主网格步长
 */
Vec2 SnapToGrid_Interact(GeometryGraph& graph, Vec2 world_coord) {
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


void PreviewSegment_Intertact(GeometryGraph& graph)
{
    auto id = graph.preview_registers[0];
    auto& node = graph.get_node_by_id(id);
    if (GeoType::is_point(node.type) && node.error_status == GeoErrorStatus::VALID) {
        const auto& view = graph.view;
        Vec2 mouse_pos = view.ScreenToWorld(graph.mouse_position.x,graph.mouse_position.y);
        Vec2 mouse_pos_snapped = SnapToGrid_Interact(graph, mouse_pos);
        auto mouse_pos_snapped_no_offset_x = mouse_pos_snapped.x - view.offset_x;
        auto mouse_pos_snapped_no_offset_y = mouse_pos_snapped.y - view.offset_y;
        double point_x = node.result.x_view;
        double point_y = node.result.y_view;
        tbb::concurrent_bounded_queue<std::vector<PointData>> q;
        process_two_point_line(q, point_x, point_y,
                       mouse_pos_snapped_no_offset_x, mouse_pos_snapped_no_offset_y,
                       true, view);


        q.try_pop(graph.preview_points);




    }
}


void CancelPreview_Intectact(GeometryGraph& graph) {
    graph.preview_func = nullptr;
    graph.preview_type = GeoType::UNKNOWN;
    graph.preview_registers.clear();
    graph.preview_points.clear();
}


void UpdateMousePos_Interact(GeometryGraph& graph,double x,double y) {
    graph.mouse_position = {x,y};
}