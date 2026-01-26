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
    CompactBuffer(graph);

    const auto& view = graph.view;
    const auto& points = graph.final_points_buffer;
    
    // 2. 将鼠标屏幕位置映射到 int16 剪裁空间 (仅转换一次)
    Vec2i target_clip = view.ScreenToClip(screen_x, screen_y);
    
    // 3. 计算剪裁空间下的阈值平方 (将 5 像素阈值转为剪裁空间单位)
    // 根据 ViewState 定义: s2c_scale 决定了像素到 int16 的映射比例
    double clip_threshold = 5.0 * view.s2c_scale_x;
    int32_t threshold_sq = static_cast<int32_t>(clip_threshold * clip_threshold);

    // 4. 识别点击位置附近的节点 ID
    std::set<uint32_t> hit_ids;

    for (const auto& node : graph.node_pool) {
        // 只有活跃且有物理形状的节点参与判定
        if (!node.active || node.current_point_count == 0 || GeoType::is_scalar(node.type)) continue;

        bool node_hit = false;
        for (uint32_t i = 0; i < node.current_point_count; ++i) {
            const auto& pt = points[node.buffer_offset + i];
            
            // 跳过垃圾数据点
            if (pt.x == graph.view.MAGIC_CLIP_X) {
                continue;
            }
            // 💡 极致性能：纯整数空间判定
            // 使用 int32 存储差值及其平方，防止 int16 溢出
            int32_t dx = static_cast<int32_t>(pt.x) - static_cast<int32_t>(target_clip.x);
            int32_t dy = static_cast<int32_t>(pt.y) - static_cast<int32_t>(target_clip.y);
            
            // 欧式距离平方比较
            if (dx * dx + dy * dy <= threshold_sq) {
                node_hit = true;
                break;
            }
        }
        
        if (node_hit) {
            hit_ids.insert(node.id);
            // 找到两个 ID 即可判定为潜在交点意图，提前终止循环
            if (hit_ids.size() >= 2) break;
        }
    }

    // 5. 转换点击位置为世界坐标，生成公式字符串
    Vec2 world_pos = view.ScreenToWorld(screen_x, screen_y);
    std::string x_str = std::to_string(world_pos.x);
    std::string y_str = std::to_string(world_pos.y);

    // 6. 根据碰撞情况派发工厂函数
    if (hit_ids.empty()) {
        // 情况 A: 空旷区域 -> 创建自由点
        GeoFactory::AddFreePoint(graph, x_str, y_str);
    } 
    else if (hit_ids.size() == 1) {
        // 情况 B: 命中一个对象 -> 创建约束点
        uint32_t target_id = *hit_ids.begin();
        GeoFactory::AddConstrainedPoint(graph, target_id, x_str, y_str);
    }
    else {
        // 情况 C: 命中多个对象 -> 暂时吸附到第一个命中的对象上 (交点逻辑 TODO)
        uint32_t target_id = *hit_ids.begin();
        GeoFactory::AddConstrainedPoint(graph, target_id, x_str, y_str);
    }
}
