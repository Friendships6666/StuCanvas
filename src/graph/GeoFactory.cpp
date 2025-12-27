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

        // ★★★ 修正点：必须初始化中间的 cx, cy ★★★
        // 假设结构体顺序是 center_id, cx, cy, radius
        node.data = Data_Circle{ center_id, 0.0, 0.0, radius };

        node.solver = Solver_Circle;

        LinkAndRank(graph, id, node.parents);

        // 这一步 Solver_Circle 会立即读取 center_id 的坐标
        // 并把正确的 cx, cy 填入 node.data
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
    uint32_t CreatePerpendicular(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id, bool is_infinite) {
        // 类型检查
        if (graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line) {
            throw std::runtime_error("垂线的依赖对象必须是线段/直线");
        }

        // --- 步骤 1: 创建垂足点 (Foot Point) ---
        uint32_t foot_id = graph.allocate_node();
        GeoNode& foot_node = graph.node_pool[foot_id];

        foot_node.render_type = GeoNode::RenderType::Point;
        foot_node.parents = { segment_id, point_id };
        foot_node.solver = Solver_PerpendicularFoot;
        foot_node.data = Data_Point{ 0, 0 }; // 初始占位

        // 手动处理依赖绑定和 Rank
        uint32_t max_r = std::max(graph.node_pool[segment_id].rank, graph.node_pool[point_id].rank);
        foot_node.rank = max_r + 1;
        graph.node_pool[segment_id].children.push_back(foot_id);
        graph.node_pool[point_id].children.push_back(foot_id);

        // 执行初始计算，确定位置
        foot_node.solver(foot_node, graph.node_pool);

        // --- 步骤 2: 创建最终的垂线对象 (Line) ---
        // 这个对象连接原点和垂足
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        line_node.render_type = GeoNode::RenderType::Line;
        line_node.parents = { point_id, foot_id };
        line_node.data = Data_Line{ point_id, foot_id, is_infinite };
        line_node.solver = nullptr; // 直线本身不需要 solver，渲染时取点坐标即可
        line_node.rank = foot_node.rank + 1;

        // 建立反向索引
        graph.node_pool[point_id].children.push_back(line_id);
        graph.node_pool[foot_id].children.push_back(line_id);

        return line_id;
    }


    void Solver_ParallelPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
        if (self.parents.size() < 2) return;

        // 1. 获取参考线段 (Parent 0) 和 通过点 (Parent 1)
        const auto& segmentNode = pool[self.parents[0]];
        const auto& throughPointNode = pool[self.parents[1]];

        const auto& segData = std::get<Data_Line>(segmentNode.data);

        // 获取线段的两个端点坐标 A, B
        const auto& pA = std::get<Data_Point>(pool[segData.p1_id].data);
        const auto& pB = std::get<Data_Point>(pool[segData.p2_id].data);
        // 获取通过点 P 的坐标
        const auto& pP = std::get<Data_Point>(throughPointNode.data);

        // 2. 计算平行向量 v = B - A
        double vx = pB.x - pA.x;
        double vy = pB.y - pA.y;

        // 3. 计算新参考点 P' = P + v
        Data_Point p_prime;
        p_prime.x = pP.x + vx;
        p_prime.y = pP.y + vy;

        self.data = p_prime;
    }


    uint32_t CreateParallel(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id) {
        // 1. 类型检查
        if (graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line) {
            throw std::runtime_error("平行线的参考对象必须是线段或直线");
        }

        // --- 步骤 1: 创建平移参考点 (Parallel Helper Point) ---
        uint32_t helper_id = graph.allocate_node();
        GeoNode& helper_node = graph.node_pool[helper_id];

        helper_node.render_type = GeoNode::RenderType::Point;
        helper_node.parents = { segment_id, point_id };
        helper_node.solver = Solver_ParallelPoint;
        helper_node.data = Data_Point{ 0, 0 };
        helper_node.is_visible = false; // 辅助点通常不需要渲染

        // 计算 Rank
        uint32_t max_r = std::max(graph.node_pool[segment_id].rank, graph.node_pool[point_id].rank);
        helper_node.rank = max_r + 1;

        // 绑定依赖
        graph.node_pool[segment_id].children.push_back(helper_id);
        graph.node_pool[point_id].children.push_back(helper_id);

        // 初始计算位置
        helper_node.solver(helper_node, graph.node_pool);

        // --- 步骤 2: 创建最终的平行直线 ---
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        line_node.render_type = GeoNode::RenderType::Line;
        line_node.parents = { point_id, helper_id };
        line_node.data = Data_Line{ point_id, helper_id, true }; // 默认为无限直线
        line_node.solver = nullptr; // 渲染层直接取两点坐标
        line_node.rank = helper_node.rank + 1;

        // 绑定依赖
        graph.node_pool[point_id].children.push_back(line_id);
        graph.node_pool[helper_id].children.push_back(line_id);

        return line_id;
    }

} // namespace GeoFactory