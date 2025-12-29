// --- 文件路径: src/graph/GeoFactory.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoFactory.h"

#include <algorithm>
#include <stdexcept>
#include <vector> // 确保 vector 被包含

namespace GeoFactory {

    // 辅助：连接父子关系，并检查循环依赖
    static void LinkAndRank(GeometryGraph& graph, uint32_t child_id, const std::vector<uint32_t>& parent_ids) {
        uint32_t max_parent_rank = 0;
        for (uint32_t pid : parent_ids) {
            // ★ 安全检查：ID 有效性
            if (pid >= graph.node_pool.size()) throw std::runtime_error("Invalid parent ID");

            // ★★★ 核心：循环依赖检测 ★★★
            if (graph.DetectCycle(child_id, pid)) {
                throw std::runtime_error("Circular dependency detected! Calculation graph is invalid.");
            }

            graph.node_pool[pid].children.push_back(child_id);
            max_parent_rank = std::max(max_parent_rank, graph.node_pool[pid].rank);
        }
        graph.node_pool[child_id].rank = parent_ids.empty() ? 0 : max_parent_rank + 1;
    }


    uint32_t CreatePoint(GeometryGraph& graph, GVar x, GVar y) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;

        Data_Point d{ x.value, y.value }; // 默认值
        std::vector<uint32_t> parents;

        // 处理 X 绑定
        if (x.is_ref) {
            parents.push_back(x.ref_id);
            d.bind_index_x = 0;
        }
        // 处理 Y 绑定 (复用 parent 或新增)
        if (y.is_ref) {
            // 简单处理：总是 push，不复用 parent 也没关系，GeoGraph 支持多 parent
            // 如果追求极致可以用 map 查重，这里直接 push 逻辑最简单
            parents.push_back(y.ref_id);
            d.bind_index_y = parents.size() - 1;
        }

        // 只要有绑定，就使用通用点求解器
        if (x.is_ref || y.is_ref) {
            node.solver = Solver_StandardPoint; // ★ 需要在 GeoSolver 中实现
        } else {
            node.solver = nullptr; // 纯静态点无需 Solver
        }

        node.data = d;
        node.parents = parents;

        LinkAndRank(graph, id, node.parents);
        if (node.solver) graph.TouchNode(id);
        return id;
    }

    uint32_t CreateFreePoint(GeometryGraph& graph, double x, double y) {
        return CreatePoint(graph, GVar(x), GVar(y));
    }

    // =========================================================
    // 圆创建 (支持动态半径)
    // =========================================================
    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, GVar radius) {
        if (center_id >= graph.node_pool.size()) throw std::runtime_error("Invalid center");

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Circle;

        Data_Circle d;
        d.center_id = center_id;
        d.cx = 0; d.cy = 0; // 稍后 Solver 会填
        d.radius = radius.value;

        std::vector<uint32_t> parents = { center_id };

        if (radius.is_ref) {
            parents.push_back(radius.ref_id);
            d.bind_index_radius = 1; // parents[1] 是半径来源
        }

        node.data = d;
        node.parents = parents;
        node.solver = Solver_Circle;

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        return id;
    }

    // =========================================================
    // 显式函数创建 (Token 编译)
    // =========================================================
    uint32_t CreateExplicitFunction(GeometryGraph& graph, const std::vector<MixedToken>& tokens) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Explicit;

        Data_SingleRPN d;
        std::vector<uint32_t> parents;

        for (const auto& item : tokens) {
            // Case 1: 操作符 (RPNTokenType)
            if (std::holds_alternative<RPNTokenType>(item)) {
                d.tokens.push_back({ std::get<RPNTokenType>(item), 0.0 });
            }
            // Case 2: 常数 (double)
            else if (std::holds_alternative<double>(item)) {
                d.tokens.push_back({ RPNTokenType::PUSH_CONST, std::get<double>(item) });
            }
            // Case 3: 变量引用 (Ref) -> 编译为 PUSH_CONST + Binding
            else if (std::holds_alternative<Ref>(item)) {
                uint32_t ref_id = std::get<Ref>(item).id;

                // 占位符
                d.tokens.push_back({ RPNTokenType::PUSH_CONST, 0.0 });

                // 添加到依赖列表
                parents.push_back(ref_id);

                // 记录绑定
                d.bindings.push_back({
                    (uint32_t)(d.tokens.size() - 1), // token index
                    (uint32_t)(parents.size() - 1),  // parent index
                    RPNBinding::VALUE
                });
            }
        }

        node.data = std::move(d);
        node.parents = parents;
        node.solver = Solver_DynamicSingleRPN;

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        return id;
    }

    uint32_t CreateLine(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite) {
        // 类型检查
        if (p1_id >= graph.node_pool.size() || p2_id >= graph.node_pool.size() ||
            graph.node_pool[p1_id].render_type != GeoNode::RenderType::Point ||
            graph.node_pool[p2_id].render_type != GeoNode::RenderType::Point) {
            throw std::runtime_error("Line/Segment must depend on two valid points.");
        }

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Line;
        node.parents = { p1_id, p2_id };
        node.data = Data_Line{ p1_id, p2_id, is_infinite };
        node.solver = nullptr; // 直线在渲染阶段提取点坐标，不需要 Solver 逻辑

        LinkAndRank(graph, id, node.parents);
        // 不需要 TouchNode，因为 Line 本身没有 Solver
        return id;
    }

    uint32_t CreateMidpoint(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id) {
        // 类型检查
        if (p1_id >= graph.node_pool.size() || p2_id >= graph.node_pool.size() ||
            graph.node_pool[p1_id].render_type != GeoNode::RenderType::Point ||
            graph.node_pool[p2_id].render_type != GeoNode::RenderType::Point) {
            throw std::runtime_error("Midpoint must depend on two valid points.");
        }

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Point;
        node.parents = { p1_id, p2_id };
        node.data = Data_Point{ 0, 0 }; // 初始占位，待算
        node.solver = Solver_Midpoint;

        LinkAndRank(graph, id, node.parents);
        // ★ 统一修改：不再抢跑，依赖 JIT
        // node.solver(node, graph.node_pool);
        graph.TouchNode(id); // 注册计算
        return id;
    }



    uint32_t CreateFunction(
        GeometryGraph& graph,
        GeoNode::RenderType r_type,
        const AlignedVector<RPNToken>& tokens,
        const std::vector<RPNBinding>& bindings,
        const std::vector<uint32_t>& parent_ids
    ) {
        // 类型检查：确保父节点是 Point 或 Scalar
        for(uint32_t pid : parent_ids) {
            if (pid >= graph.node_pool.size()) throw std::runtime_error("Invalid parent ID for function.");
            auto p_type = graph.node_pool[pid].render_type;
            if (p_type != GeoNode::RenderType::Point && p_type != GeoNode::RenderType::Scalar) {
                 throw std::runtime_error("Function can only depend on Points or Scalars.");
            }
        }

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

        graph.TouchNode(id); // 注册计算
        return id;
    }

    uint32_t CreatePerpendicular(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id, bool is_infinite) {
        // 类型检查
        if (segment_id >= graph.node_pool.size() || point_id >= graph.node_pool.size() ||
            graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line ||
            graph.node_pool[point_id].render_type != GeoNode::RenderType::Point) {
            throw std::runtime_error("Perpendicular requires a Line and a Point as dependencies.");
        }

        uint32_t foot_id = graph.allocate_node();
        {
            GeoNode& foot_node = graph.node_pool[foot_id];
            foot_node.render_type = GeoNode::RenderType::Point;
            foot_node.parents = { segment_id, point_id };
            foot_node.solver = Solver_PerpendicularFoot;
            foot_node.data = Data_Point{ 0, 0 }; // 初始占位

            // 计算 Rank
            uint32_t max_r = std::max(graph.node_pool[segment_id].rank, graph.node_pool[point_id].rank);
            foot_node.rank = max_r + 1;

            // 建立依赖关系
            graph.node_pool[segment_id].children.push_back(foot_id);
            graph.node_pool[point_id].children.push_back(foot_id);


            graph.TouchNode(foot_id); // 注册计算
        }

        // 创建垂线 (本身不计算，只是定义关系)
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        // 安全获取 rank (垂足的 rank)
        uint32_t foot_rank = graph.node_pool[foot_id].rank;

        line_node.render_type = GeoNode::RenderType::Line;
        line_node.parents = { point_id, foot_id }; // 依赖外部点和垂足
        line_node.data = Data_Line{ point_id, foot_id, is_infinite };
        line_node.solver = nullptr; // Line 渲染时直接取端点坐标
        line_node.rank = foot_rank + 1;

        graph.node_pool[point_id].children.push_back(line_id);
        graph.node_pool[foot_id].children.push_back(line_id);

        return line_id;
    }

    uint32_t CreateParallel(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id) {
        // 类型检查
        if (segment_id >= graph.node_pool.size() || point_id >= graph.node_pool.size() ||
            graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line ||
            graph.node_pool[point_id].render_type != GeoNode::RenderType::Point) {
             throw std::runtime_error("Parallel requires a Line and a Point.");
        }

        uint32_t helper_id = graph.allocate_node();
        {
            GeoNode& helper_node = graph.node_pool[helper_id];
            helper_node.render_type = GeoNode::RenderType::Point;
            helper_node.parents = { segment_id, point_id };
            helper_node.solver = Solver_ParallelPoint;
            helper_node.data = Data_Point{ 0, 0 }; // 初始占位
            helper_node.is_visible = false; // 辅助点不渲染

            // 计算 Rank
            uint32_t max_r = std::max(graph.node_pool[segment_id].rank, graph.node_pool[point_id].rank);
            helper_node.rank = max_r + 1;

            // 建立依赖
            graph.node_pool[segment_id].children.push_back(helper_id);
            graph.node_pool[point_id].children.push_back(helper_id);

            // ★ 统一修改：不再抢跑
            // helper_node.solver(helper_node, graph.node_pool);
            graph.TouchNode(helper_id); // 注册计算
        }

        // 创建平行线 (定义关系，不计算)
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        // 安全获取 rank
        uint32_t helper_rank = graph.node_pool[helper_id].rank;

        line_node.render_type = GeoNode::RenderType::Line;
        line_node.parents = { point_id, helper_id }; // 依赖通过点和辅助点
        line_node.data = Data_Line{ point_id, helper_id, true }; // true=无限长
        line_node.solver = nullptr; // Line 本身不计算
        line_node.rank = helper_rank + 1;

        graph.node_pool[point_id].children.push_back(line_id);
        graph.node_pool[helper_id].children.push_back(line_id);

        return line_id;
    }

    uint32_t CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, double initial_x, double initial_y) {
        if (target_id >= graph.node_pool.size()) throw std::runtime_error("ConstrainedPoint requires a valid target object.");

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Point;
        node.parents = { target_id }; // 依赖目标对象
        node.data = Data_Point{ initial_x, initial_y }; // 存储当前坐标
        node.solver = Solver_ConstrainedPoint;

        LinkAndRank(graph, id, node.parents);

        // ★ 统一修改：不再抢跑，依赖 JIT
        // node.solver(node, graph.node_pool); // 之前为了立刻看到效果加的，但要统一就删掉
        graph.TouchNode(id); // 注册计算
        return id;
    }

    uint32_t CreateTangent(GeometryGraph& graph, uint32_t constrained_point_id) {
        if (constrained_point_id >= graph.node_pool.size() ||
            graph.node_pool[constrained_point_id].render_type != GeoNode::RenderType::Point) {
             throw std::runtime_error("Tangent requires a Point as dependency.");
        }

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Line; // 渲染类型是 Line
        node.parents = { constrained_point_id };      // 依赖那个约束点

        // 初始数据 (占位)
        node.data = Data_CalculatedLine{0,0,0,0, true}; // 默认为无限长直线
        node.solver = Solver_Tangent; // 绑定专用求解器

        LinkAndRank(graph, id, node.parents);

        // ★ 统一修改：不再抢跑
        // node.solver(node, graph.node_pool);
        graph.TouchNode(id); // 注册计算
        return id;
    }
    uint32_t CreateMeasureLength(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id) {
        // 类型检查
        if (p1_id >= graph.node_pool.size() || p2_id >= graph.node_pool.size()) {
            throw std::runtime_error("Invalid points for measurement.");
        }

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        node.render_type = GeoNode::RenderType::Scalar; // 类型是标量
        node.parents = { p1_id, p2_id };

        node.data = Data_Scalar{ 0.0, ScalarType::Length }; // 初始值为 0
        node.solver = Solver_Measure_Length; // 绑定测量求解器

        LinkAndRank(graph, id, node.parents);

        // 注册到 JIT 队列
        graph.TouchNode(id);

        return id;
    }

}