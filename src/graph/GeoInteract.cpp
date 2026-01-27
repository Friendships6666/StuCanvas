#include "../../include/graph/GeoInteract.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include <vector>
#include <set>
#include <string>
#include <cmath>

void AddPoint_Interact(GeometryGraph& graph, double screen_x, double screen_y) {
    // 1. 立即执行 GC，确保缓冲区一致性
    // CompactBuffer(graph); // 假设 CompactBuffer 在 GeoGraph 内部或其他地方被调用

    const auto& view = graph.view;
    const auto& points = graph.final_points_buffer;
    
    // 2. 将鼠标屏幕位置映射到 int16 剪裁空间 (仅转换一次)
    Vec2i target_clip = view.ScreenToClip(screen_x, screen_y);
    
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
            // 为了效率，如果找到足够多的交点候选（例如，至少2个），可以提前退出
            // if (intersection_candidates_ids.size() >= 2) break; 
        }
    }

    // 5. 转换点击位置为世界坐标，生成公式字符串
    Vec2 world_pos = view.ScreenToWorld(screen_x, screen_y);
    std::string x_str = std::to_string(world_pos.x);
    std::string y_str = std::to_string(world_pos.y);


    if (intersection_candidates_ids.empty()) {
        // 情况 A: 空旷区域 -> 创建自由点
        GeoFactory::AddFreePoint(graph, x_str, y_str);
    } 
    else if (intersection_candidates_ids.size() == 1) {
        // 情况 B: 命中一个非点、非标量对象 -> 创建约束点
        uint32_t target_id = intersection_candidates_ids[0];
        GeoFactory::AddConstrainedPoint(graph, target_id, x_str, y_str);
    }
    else {
        // 情况 C: 命中多个非点、非标量对象 -> 创建图解交点
        // x_str 和 y_str 作为锚点公式，指示初始交点位置
        GeoFactory::AddGraphicalIntersection(graph, intersection_candidates_ids, x_str, y_str);
    }
}
