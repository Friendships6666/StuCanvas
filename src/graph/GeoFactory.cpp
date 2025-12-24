// --- 文件路径: src/graph/GeoFactory.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoFactory.h"

#include <algorithm>
#include <stdexcept>



namespace GeoFactory {

    // 辅助函数：连接父子关系并计算 Rank
    static void LinkAndRank(GeometryGraph& graph, uint32_t child_id, const std::vector<uint32_t>& parent_ids) {
        uint32_t max_parent_rank = 0;
        for (uint32_t pid : parent_ids) {
            // 建立反向索引：告诉父亲你有个孩子
            graph.node_pool[pid].children.push_back(child_id);
            // 计算辈分
            max_parent_rank = std::max(max_parent_rank, graph.node_pool[pid].rank);
        }
        // 如果有爹，我的 Rank = 爹里最大的 Rank + 1
        graph.node_pool[child_id].rank = parent_ids.empty() ? 0 : max_parent_rank + 1;
    }

    uint32_t CreateFreePoint(GeometryGraph& graph, double x, double y) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Point;
        node.data = Data_Point{ x, y };
        node.rank = 0; // 自由点永远是 Rank 0
        node.solver = nullptr; // 自由点不需要计算

        return id;
    }

    uint32_t CreateLine(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite) {
        // 类型检查
        if (graph.node_pool[p1_id].render_type != GeoNode::RenderType::Point ||
            graph.node_pool[p2_id].render_type != GeoNode::RenderType::Point) {
            throw std::runtime_error("Line/Segment must depend on two points.");
        }

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Line;
        node.parents = { p1_id, p2_id };
        node.data = Data_Line{ p1_id, p2_id, is_infinite };
        node.solver = nullptr; // 直线在渲染阶段提取点坐标，不需要 Solver 逻辑

        LinkAndRank(graph, id, node.parents);
        return id;
    }

    uint32_t CreateMidpoint(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Point;
        node.parents = { p1_id, p2_id };
        node.data = Data_Point{ 0, 0 }; // 初始占位，待算
        node.solver = Solver_Midpoint;

        LinkAndRank(graph, id, node.parents);
        // 立即执行一次计算，让中点位置正确
        node.solver(node, graph.node_pool);
        return id;
    }

    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, double radius) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Circle;
        node.parents = { center_id };
        node.data = Data_Circle{ center_id, radius };
        node.solver = Solver_Circle;

        LinkAndRank(graph, id, node.parents);
        node.solver(node, graph.node_pool);
        return id;
    }

    uint32_t CreateFunction(
        GeometryGraph& graph,
        GeoNode::RenderType r_type,
        const AlignedVector<RPNToken>& tokens,
        const std::vector<RPNBinding>& bindings,
        const std::vector<uint32_t>& parent_ids
    ) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = r_type;
        node.parents = parent_ids;

        Data_SingleRPN d;
        d.tokens = tokens;
        d.bindings = bindings;
        node.data = std::move(d);
        node.solver = Solver_DynamicSingleRPN;

        LinkAndRank(graph, id, node.parents);
        node.solver(node, graph.node_pool);
        return id;
    }

} // namespace GeoFactory