// --- 文件路径: src/graph/GeoFactory.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotSegment.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotCircle.h"
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



    static void Render_Point_Delegate(
        GeoNode& self,
        const std::vector<GeoNode>& pool,
        const ViewState&, // 已经通过全局变量或 map 传入
        const NDCMap& map,
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q
    ) {
        // 1. 从节点中提取世界坐标 (double)
        // 此时 ExtractValue 会根据变体类型（Data_Point, Data_IntersectionPoint 等）自动提取
        double wx = ExtractValue(self, RPNBinding::POS_X, pool);
        double wy = ExtractValue(self, RPNBinding::POS_Y, pool);

        // 2. 构造显存点数据结构
        PointData pd{};

        // ★★★ 核心修复：使用 world_to_clip_store 进行投影 ★★★
        // 这个函数会处理：(World - Center) * Scale，并转为 float
        // 这样才能处理“大数吃小数”问题，并适配当前缩放等级
        world_to_clip_store(pd, wx, wy, map, self.id);

        // 3. 推送到渲染队列
        q.push({ self.id, {pd} });
    }

    static void Render_Line_Delegate(
        GeoNode& self,
        const std::vector<GeoNode>& pool,
        const ViewState& v,
        const NDCMap& m,
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q
    ) {
        // 情况 A：基于拓扑关系的线（依赖两个点 ID）
        if (std::holds_alternative<Data_Line>(self.data)) {
            const auto& d = std::get<Data_Line>(self.data);

            // ★★★ 核心修复：使用 ExtractValue 替代 std::get<Data_Point> ★★★
            // 这样无论 d.p1_id 指向的是普通点、交点还是解析交点，都能安全获取坐标
            double x1 = ExtractValue(pool[d.p1_id], RPNBinding::POS_X, pool);
            double y1 = ExtractValue(pool[d.p1_id], RPNBinding::POS_Y, pool);
            double x2 = ExtractValue(pool[d.p2_id], RPNBinding::POS_X, pool);
            double y2 = ExtractValue(pool[d.p2_id], RPNBinding::POS_Y, pool);

            process_two_point_line(
                &q,
                x1, y1, x2, y2,
                !d.is_infinite, // is_segment: 如果是无限直线则为 false
                self.id,
                v.world_origin,
                v.wppx, v.wppy,
                v.screen_width, v.screen_height,
                0, 0, // offset_x, offset_y (目前接口保留，内部已由 NDCMap 处理)
                m
            );
        }
        // 情况 B：基于计算结果的线（例如切线，直接存储了 double 坐标）
        else if (std::holds_alternative<Data_CalculatedLine>(self.data)) {
            const auto& d = std::get<Data_CalculatedLine>(self.data);

            process_two_point_line(
                &q,
                d.x1, d.y1, d.x2, d.y2,
                !d.is_infinite,
                self.id,
                v.world_origin,
                v.wppx, v.wppy,
                v.screen_width, v.screen_height,
                0, 0,
                m
            );
        }
    }

    static void Render_Circle_Delegate(GeoNode& self, const std::vector<GeoNode>& pool, const ViewState& v, const NDCMap& m, oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q) {
        const auto& d = std::get<Data_Circle>(self.data);
        process_circle_specialized(&q, d.cx, d.cy, d.radius, self.id, v.world_origin, v.wppx, v.wppy, v.screen_width, v.screen_height, m);
    }

    static void Render_Explicit_Delegate(GeoNode& self, const std::vector<GeoNode>& pool, const ViewState& v, const NDCMap& m, oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q) {
        const auto& d = std::get<Data_SingleRPN>(self.data);
        process_explicit_chunk(v.world_origin.x, v.world_origin.x + v.screen_width * v.wppx, d.tokens, &q, self.id, v.screen_width, m);
    }

    static void Render_Parametric_Delegate(GeoNode& self, const std::vector<GeoNode>& pool, const ViewState& v, const NDCMap& m, oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q) {
        const auto& d = std::get<Data_DualRPN>(self.data);
        process_parametric_chunk(d.tokens_x, d.tokens_y, d.t_min, d.t_max, &q, self.id, m);
    }

    static void Render_Implicit_Delegate(GeoNode& self, const std::vector<GeoNode>& pool, const ViewState& v, const NDCMap& m, oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q) {
        const auto& d = std::get<Data_SingleRPN>(self.data);
        process_implicit_adaptive(&q, v.world_origin, v.wppx, v.wppy, v.screen_width, v.screen_height, d.tokens, d.tokens, self.id, 0, 0, m);
    }
    uint32_t CreateScalar(GeometryGraph& graph, const RPNParam& expr) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Scalar;
        node.is_visible = false;

        Data_Scalar d;
        std::vector<uint32_t> parents;

        for (const auto& item : expr) {
            if (std::holds_alternative<RPNTokenType>(item)) {
                RPNTokenType type = std::get<RPNTokenType>(item);
                // 强制检查：标量 RPN 不允许包含坐标变量
                if (type == RPNTokenType::PUSH_X || type == RPNTokenType::PUSH_Y || type == RPNTokenType::PUSH_T) {
                    throw std::runtime_error("Scalar RPN cannot contain x, y, or t tokens.");
                }
                d.tokens.push_back({type, 0.0});
            } else if (std::holds_alternative<double>(item)) {
                d.tokens.push_back({RPNTokenType::PUSH_CONST, std::get<double>(item)});
            } else if (std::holds_alternative<Ref>(item)) {
                uint32_t ref_id = std::get<Ref>(item).id;
                d.tokens.push_back({RPNTokenType::PUSH_CONST, 0.0}); // 占位

                // 查找父节点索引
                uint32_t p_idx = 0; bool found = false;
                for(size_t i=0; i<parents.size(); ++i) if(parents[i]==ref_id){p_idx=(uint32_t)i;found=true;break;}
                if(!found){parents.push_back(ref_id); p_idx=(uint32_t)parents.size()-1;}

                d.bindings.push_back({(uint32_t)(d.tokens.size()-1), p_idx, RPNBinding::VALUE});
            }
        }

        node.data = std::move(d);
        node.parents = parents;
        node.solver = Solver_ScalarRPN;

        // 此处 LinkAndRank 内部会自动执行 DetectCycle
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        return id;
    }
    static void CompileMixedTokens(
        const std::vector<MixedToken>& src,
        AlignedVector<RPNToken>& out_tokens,
        std::vector<RPNBinding>& out_bindings,
        std::vector<uint32_t>& out_parents
    ) {
        for (const auto& item : src) {
            if (std::holds_alternative<RPNTokenType>(item)) {
                out_tokens.push_back({ std::get<RPNTokenType>(item), 0.0 });
            }
            else if (std::holds_alternative<double>(item)) {
                out_tokens.push_back({ RPNTokenType::PUSH_CONST, std::get<double>(item) });
            }
            else if (std::holds_alternative<Ref>(item)) {
                uint32_t ref_id = std::get<Ref>(item).id;
                out_tokens.push_back({ RPNTokenType::PUSH_CONST, 0.0 }); // 占位

                // 查找或添加父节点
                uint32_t p_idx = 0;
                bool found = false;
                for(size_t i=0; i<out_parents.size(); ++i) {
                    if(out_parents[i] == ref_id) { p_idx = (uint32_t)i; found = true; break; }
                }
                if(!found) {
                    out_parents.push_back(ref_id);
                    p_idx = (uint32_t)out_parents.size() - 1;
                }

                out_bindings.push_back({ (uint32_t)(out_tokens.size() - 1), p_idx, RPNBinding::VALUE });
            }
        }
    }

    // =========================================================
    // 创建参数方程：x=f(t), y=g(t)
    // =========================================================
    uint32_t CreateParametricFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& src_x,
        const std::vector<MixedToken>& src_y,
        double t_min, double t_max
    ) {

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Parametric;

        Data_DualRPN d;
        d.t_min = t_min;
        d.t_max = t_max;
        std::vector<uint32_t> parents;

        // 分别编译 X 和 Y 的 RPN
        CompileMixedTokens(src_x, d.tokens_x, d.bindings_x, parents);
        CompileMixedTokens(src_y, d.tokens_y, d.bindings_y, parents);

        node.data = std::move(d);
        node.parents = parents;
        node.solver = Solver_DynamicDualRPN; // 使用双 RPN 求解器

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        node.render_task = Render_Parametric_Delegate; // 绑定参数方程
        return id;
    }

    // =========================================================
    // 创建隐函数：f(x, y) = 0
    // =========================================================
    uint32_t CreateImplicitFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens
    ) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Implicit;

        Data_SingleRPN d;
        std::vector<uint32_t> parents;

        CompileMixedTokens(tokens, d.tokens, d.bindings, parents);

        node.data = std::move(d);
        node.parents = parents;
        node.solver = Solver_DynamicSingleRPN; // 与显函数逻辑一致

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        node.render_task = Render_Implicit_Delegate; // 绑定隐函数

        return id;
    }


    uint32_t CreatePoint(GeometryGraph& graph, const RPNParam& x_expr, const RPNParam& y_expr) {
        // 先创建两个标量节点
        uint32_t sx = CreateScalar(graph, x_expr);
        uint32_t sy = CreateScalar(graph, y_expr);

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;
        node.parents = { sx, sy };
        node.data = Data_Point{}; // 初始 0,0
        node.solver = Solver_StandardPoint;

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        node.render_task = Render_Point_Delegate; // 绑定点渲染
        return id;
    }



    // =========================================================
    // 圆创建 (支持动态半径)
    // =========================================================
    // --- 文件路径: src/graph/GeoFactory.cpp ---

    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, const RPNParam& radius_expr) {
        // 1. 先为半径创建一个标量节点
        uint32_t sr = CreateScalar(graph, radius_expr);

        // 2. 分配圆节点
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Circle;

        // 3. 建立依赖：parents[0] 是圆心点，parents[1] 是半径标量
        node.parents = { center_id, sr };

        // 4. 初始化数据（仅保留计算缓存）
        Data_Circle d{};
        // ★ 修复：删除了 d.center_id = center_id; 这一行
        node.data = d;
        node.solver = Solver_Circle;

        // 5. 建立拓扑链接并标记脏
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        node.render_task = Render_Circle_Delegate; // 绑定圆渲染

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
        node.render_task = Render_Explicit_Delegate; // 绑定显函数
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

        node.render_task = Render_Line_Delegate; // 绑定线渲染
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
        node.render_task = Render_Point_Delegate; // 绑定点渲染
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
        line_node.render_task = Render_Line_Delegate; // 垂线也是线

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
        line_node.render_task = Render_Line_Delegate; // 平行线也是线


        return line_id;
    }

    // --- 文件路径: src/graph/GeoFactory.cpp ---

    uint32_t CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, const RPNParam& x_expr, const RPNParam& y_expr) {
        // 1. 安全检查
        if (target_id >= graph.node_pool.size()) {
            throw std::runtime_error("ConstrainedPoint requires a valid target object.");
        }

        // 2. 像 CreatePoint 一样，先为初值坐标提升两个标量节点
        // 这样初值也可以是动态的，例如：Ref(len_AB)
        uint32_t sx = CreateScalar(graph, x_expr);
        uint32_t sy = CreateScalar(graph, y_expr);

        // 3. 分配节点
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;

        // 4. 建立依赖链：
        // parents[0] 是目标曲线/圆
        // parents[1] 是初始位置 X (Scalar)
        // parents[2] 是初始位置 Y (Scalar)
        node.parents = { target_id, sx, sy };

        // 5. 初始化数据缓存
        node.data = Data_Point{};
        node.solver = Solver_ConstrainedPoint;
        node.render_task = Render_Point_Delegate; // 绑定点渲染

        // 6. 拓扑链接与 JIT 注册
        // LinkAndRank 会自动执行针对这三个父节点的循环依赖检测
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

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
        node.render_task = Render_Line_Delegate; // 切线也是线，复用 Line 代理
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

    uint32_t CreateIntersectionPoint(
    GeometryGraph& graph,
    const RPNParam& x_init,
    const RPNParam& y_init,
    const std::vector<uint32_t>& target_ids
) {
        if (target_ids.size() < 2) throw std::runtime_error("Intersection requires at least 2 objects.");

        // 1. 类型检查：禁止点和标量参与
        for (uint32_t tid : target_ids) {
            auto type = graph.node_pool[tid].render_type;
            if (type == GeoNode::RenderType::Point || type == GeoNode::RenderType::Scalar || type == GeoNode::RenderType::None) {
                throw std::runtime_error("Only shape objects (Lines, Circles, Functions) can produce intersections.");
            }
        }

        // 2. 提升初始位置为标量节点
        uint32_t sx = CreateScalar(graph, x_init);
        uint32_t sy = CreateScalar(graph, y_init);

        // 3. 分配节点
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;
        node.render_task = Render_Point_Delegate;
        node.solver = Solver_IntersectionPoint; // ★ 绑定专用图解求解器

        // 4. 组装 parents: [Target0, Target1, ..., TargetN, SX, SY]
        node.parents = target_ids;
        node.parents.push_back(sx);
        node.parents.push_back(sy);

        Data_IntersectionPoint d;
        d.num_targets = (uint32_t)target_ids.size();
        node.data = d;

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        return id;
    }
    uint32_t CreateAnalyticalIntersection(
    GeometryGraph& graph,
    uint32_t id1, uint32_t id2,
    const RPNParam& x_guess,
    const RPNParam& y_guess
) {
        // 1. 严格类型校验
        auto t1 = graph.node_pool[id1].render_type;
        auto t2 = graph.node_pool[id2].render_type;

        bool is_line1 = (t1 == GeoNode::RenderType::Line);
        bool is_circle1 = (t1 == GeoNode::RenderType::Circle);
        bool is_line2 = (t2 == GeoNode::RenderType::Line);
        bool is_circle2 = (t2 == GeoNode::RenderType::Circle);

        if (!((is_line1 || is_circle1) && (is_line2 || is_circle2))) {
            throw std::runtime_error("AnalyticalIntersection only supports Line-Line, Line-Circle, or Circle-Circle.");
        }

        // 2. 提升初始猜测位置为标量节点
        // 只有在第一次 Solve 时用于确定分支符号
        uint32_t sx = CreateScalar(graph, x_guess);
        uint32_t sy = CreateScalar(graph, y_guess);

        // 3. 分配节点
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;

        // parents 布局: [Obj1, Obj2, GuessX, GuessY]
        node.parents = { id1, id2, sx, sy };
        node.solver = Solver_AnalyticalIntersection;
        node.render_task = Render_Point_Delegate;

        // 4. 初始化 Payload
        Data_AnalyticalIntersection d;
        d.branch_sign = 0; // 0 表示尚未锁定分支
        d.is_found = false;
        node.data = d;

        // 5. 建立链接并标记脏
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }
    uint32_t CreateAnalyticalConstrainedPoint(
    GeometryGraph& graph,
    uint32_t target_id,
    const RPNParam& x_guess,
    const RPNParam& y_guess
) {
        // 1. 将猜测坐标 RPN 提升为标量节点
        uint32_t sx = CreateScalar(graph, x_guess);
        uint32_t sy = CreateScalar(graph, y_guess);

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;

        // 2. 建立依赖关系
        node.parents = { target_id, sx, sy };

        Data_AnalyticalConstrainedPoint d;
        d.is_initialized = false; // 等待 Solver 第一次执行时锁定 t
        node.data = d;

        node.solver = Solver_AnalyticalConstrainedPoint;
        node.render_task = Render_Point_Delegate; // 复用通用点渲染

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }

}