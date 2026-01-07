// --- 文件路径: src/graph/GeoFactory.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotSegment.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotCircle.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/graph/CommandManager.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace GeoFactory {
    // =========================================================
    // 0. 内部私有辅助逻辑
    // =========================================================
    namespace {
        // 样式修正
        void ValidateAndFixStyle(GeoNode::RenderType type, GeoNode::VisualConfig &cfg) {
            using RT = GeoNode::RenderType;
            if (type == RT::Point) {
                if (!ObjectStyle::IsPoint(cfg.style)) cfg.style = (uint32_t) ObjectStyle::Point::Free;
            } else {
                if (!ObjectStyle::IsLine(cfg.style)) cfg.style = (uint32_t) ObjectStyle::Line::Solid;
            }
        }

        // 核心 RPN 编译逻辑 (复用)
        void CompileMixedTokensInternal(
            const std::vector<MixedToken> &src,
            AlignedVector<RPNToken> &out_tokens,
            std::vector<RPNBinding> &out_bindings,
            std::vector<uint32_t> &out_parents
        ) {
            for (const auto &item: src) {
                if (std::holds_alternative<RPNTokenType>(item)) {
                    out_tokens.push_back({std::get<RPNTokenType>(item), 0.0});
                } else if (std::holds_alternative<double>(item)) {
                    out_tokens.push_back({RPNTokenType::PUSH_CONST, std::get<double>(item)});
                } else if (std::holds_alternative<Ref>(item)) {
                    uint32_t ref_id = std::get<Ref>(item).id;
                    out_tokens.push_back({RPNTokenType::PUSH_CONST, 0.0});

                    uint32_t p_idx = 0;
                    bool found = false;
                    for (size_t i = 0; i < out_parents.size(); ++i) {
                        if (out_parents[i] == ref_id) {
                            p_idx = (uint32_t) i;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        out_parents.push_back(ref_id);
                        p_idx = (uint32_t) out_parents.size() - 1;
                    }
                    out_bindings.push_back({(uint32_t) (out_tokens.size() - 1), p_idx, RPNBinding::VALUE});
                }
            }
        }

        // 自动挂载标签事务
        void AppendSmartLabelMutations(Transaction &tx, GeometryGraph &graph, uint32_t host_id,
                                       const std::string &host_name) {
            uint32_t sx_id = graph.allocate_node();
            uint32_t sy_id = graph.allocate_node();
            uint32_t anchor_id = graph.allocate_node();
            uint32_t label_id = graph.allocate_node();

            // 辅助标量 X
            GeoNode::VisualConfig sx_cfg;
            sx_cfg.name = host_name + "_sx";
            tx.mutations.push_back({Mutation::Type::ACTIVE, sx_id, false, true});
            tx.mutations.push_back({
                Mutation::Type::DATA, sx_id, std::monostate{}, Data_Scalar{0.0, ScalarType::Expression}
            });
            tx.mutations.push_back({Mutation::Type::STYLE, sx_id, GeoNode::VisualConfig{}, sx_cfg});
            tx.mutations.push_back({Mutation::Type::LINKS, sx_id, std::vector<uint32_t>{}, std::vector<uint32_t>{}});

            // 辅助标量 Y
            GeoNode::VisualConfig sy_cfg;
            sy_cfg.name = host_name + "_sy";
            tx.mutations.push_back({Mutation::Type::ACTIVE, sy_id, false, true});
            tx.mutations.push_back({
                Mutation::Type::DATA, sy_id, std::monostate{}, Data_Scalar{0.0, ScalarType::Expression}
            });
            tx.mutations.push_back({Mutation::Type::STYLE, sy_id, GeoNode::VisualConfig{}, sy_cfg});
            tx.mutations.push_back({Mutation::Type::LINKS, sy_id, std::vector<uint32_t>{}, std::vector<uint32_t>{}});

            // 锚点 (Helper)
            tx.mutations.push_back({Mutation::Type::ACTIVE, anchor_id, false, true});
            tx.mutations.push_back({Mutation::Type::DATA, anchor_id, std::monostate{}, Data_Point{0, 0}});
            // ★★★ 修复点：显式构造 vector ★★★
            tx.mutations.push_back({
                Mutation::Type::LINKS, anchor_id, std::vector<uint32_t>{}, std::vector<uint32_t>{host_id, sx_id, sy_id}
            });

            // 文字节点 (Text)
            GeoNode::VisualConfig label_cfg;
            label_cfg.name = host_name + "_label";
            tx.mutations.push_back({Mutation::Type::ACTIVE, label_id, false, true});
            tx.mutations.push_back({Mutation::Type::DATA, label_id, std::monostate{}, Data_TextLabel{0, 0}});
            // ★★★ 修复点：显式构造 vector ★★★
            tx.mutations.push_back({
                Mutation::Type::LINKS, label_id, std::vector<uint32_t>{}, std::vector<uint32_t>{anchor_id}
            });
            tx.mutations.push_back({Mutation::Type::STYLE, label_id, GeoNode::VisualConfig{}, label_cfg});
        }
    }

    // =========================================================
    // 1. 渲染代理 (Delegates)
    // =========================================================
    // 这些函数在渲染时被调用，不产生 Transaction
    void Render_Point_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                               oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        if (!self.result.is_valid) return;
        PointData pd{};
        world_to_clip_store(pd, self.result.x, self.result.y, m, self.id);
        q.push({self.id, {pd}});
    }

    void Render_Line_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                              oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        auto coords = ExtractLineCoords(self, pool);
        if (!coords) return;
        process_two_point_line(&q, coords->x1, coords->y1, coords->x2, coords->y2, true, self.id, v.world_origin,
                               v.wppx, v.wppy, v.screen_width, v.screen_height, 0, 0, m);
    }

    void Render_Circle_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                                oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        if (!self.result.is_valid) return;
        process_circle_specialized(&q, self.result.x, self.result.y, self.result.scalar, self.id, v.world_origin,
                                   v.wppx, v.wppy, v.screen_width, v.screen_height, m);
    }

    void Render_Explicit_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                                  oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        auto *d = std::get_if<Data_SingleRPN>(&self.data);
        if (d)
            process_explicit_chunk(v.world_origin.x, v.world_origin.x + v.screen_width * v.wppx, d->tokens, &q,
                                   self.id, v.screen_width, m);
    }

    void Render_Parametric_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v,
                                    const NDCMap &m, oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        auto *d = std::get_if<Data_DualRPN>(&self.data);
        if (d) process_parametric_chunk(d->tokens_x, d->tokens_y, d->t_min, d->t_max, &q, self.id, m);
    }

    void Render_Implicit_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                                  oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        auto *d = std::get_if<Data_SingleRPN>(&self.data);
        if (d)
            process_implicit_adaptive(&q, v.world_origin, v.wppx, v.wppy, v.screen_width, v.screen_height, d->tokens,
                                      d->tokens, self.id, 0, 0, m);
    }

    void Render_Text_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const ViewState &v, const NDCMap &m,
                              oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
        auto *data = std::get_if<Data_TextLabel>(&self.data);
        if (!data || !self.result.is_valid) return;
        uint32_t anchor_id = self.parents[0];
        uint32_t host_id = pool[anchor_id].parents[0];
        const auto &host_node = pool[host_id];
        if (!host_node.config.show_label) return;

        PointData pd{};
        world_to_clip_store(pd, self.result.x, self.result.y, m, self.id);
        pd.position.x += (host_node.config.label_offset_x / (float) v.screen_width) * 2.0f;
        pd.position.y -= (host_node.config.label_offset_y / (float) v.screen_height) * 2.0f;
        q.push({self.id, {pd}});
    }

    // =========================================================
    // 2. 事务化工厂函数实现
    // =========================================================

    Transaction CreateScalar(GeometryGraph &graph, const RPNParam &expr, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Scalar";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        Data_Scalar d_logic;
        std::vector<uint32_t> parents;
        CompileMixedTokensInternal(expr, d_logic.tokens, d_logic.bindings, parents);

        GeoNode::VisualConfig final_cfg = style;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, d_logic});
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, parents});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});
        return tx;
    }

    Transaction CreatePoint(GeometryGraph &graph, const RPNParam &x_e, const RPNParam &y_e,
                            const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Point";
        Transaction tx_x = CreateScalar(graph, x_e, {});
        Transaction tx_y = CreateScalar(graph, y_e, {});
        tx.mutations.insert(tx.mutations.end(), tx_x.mutations.begin(), tx_x.mutations.end());
        tx.mutations.insert(tx.mutations.end(), tx_y.mutations.begin(), tx_y.mutations.end());

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Free;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Point{0, 0}});
        tx.mutations.push_back({
            Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{tx_x.main_id, tx_y.main_id}
        });
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateLine(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite,
                           const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Line";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Line, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Line{p1_id, p2_id, is_infinite}});
        tx.mutations.push_back(
            {Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{p1_id, p2_id}});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateCircle(GeometryGraph &graph, uint32_t center_id, const RPNParam &radius_expr,
                             const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Circle";

        // 1. 创建半径标量事务，并合并其 Mutation
        Transaction tx_r = CreateScalar(graph, radius_expr, {});
        tx.mutations.insert(tx.mutations.end(), tx_r.mutations.begin(), tx_r.mutations.end());

        // 2. 为圆分配主节点 ID
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        // 3. 处理视觉配置与自动命名
        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Circle, final_cfg);
        if (final_cfg.name == "BasicObject") {
            final_cfg.name = graph.GenerateNextName();
        }

        // 4. 构建 Mutation 序列
        // 激活状态：从 false 变为 true
        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});

        // 逻辑数据：Data_Circle{cx, cy, radius} 初始为 0
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Circle{0.0, 0.0, 0.0}});

        // ★★★ 核心修复：显式构造 vector，解决 variant 匹配报错 ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{center_id, tx_r.main_id}
        });

        // 应用样式
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        // 5. 自动挂载标签（Label）链
        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);

        return tx;
    }

    Transaction CreateCircleThreePoints(GeometryGraph &graph, uint32_t p1, uint32_t p2, uint32_t p3,
                                        const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create 3P Circle";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Circle, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Circle{0, 0, 0}});

        // ★★★ 修复点：显式构造 vector，否则 variant 无法推导 ★★★
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{p1, p2, p3}});

        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateConstrainedPoint(GeometryGraph &graph, uint32_t target_id, const RPNParam &x_e,
                                       const RPNParam &y_e, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Constrained Point";
        Transaction tx_x = CreateScalar(graph, x_e, {});
        Transaction tx_y = CreateScalar(graph, y_e, {});
        tx.mutations.insert(tx.mutations.end(), tx_x.mutations.begin(), tx_x.mutations.end());
        tx.mutations.insert(tx.mutations.end(), tx_y.mutations.begin(), tx_y.mutations.end());

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Constraint;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Point{0, 0}});
        tx.mutations.push_back({
            Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{tx_x.main_id, tx_y.main_id}
        });
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateIntersectionPoint(GeometryGraph &graph, const RPNParam &x_e, const RPNParam &y_e,
                                        const std::vector<uint32_t> &targets, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Intersection";
        Transaction tx_x = CreateScalar(graph, x_e, {});
        Transaction tx_y = CreateScalar(graph, y_e, {});
        tx.mutations.insert(tx.mutations.end(), tx_x.mutations.begin(), tx_x.mutations.end());
        tx.mutations.insert(tx.mutations.end(), tx_y.mutations.begin(), tx_y.mutations.end());

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Intersection;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        Data_IntersectionPoint d;
        d.num_targets = (uint32_t) targets.size();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, d});
        std::vector<uint32_t> pids = targets;
        pids.push_back(tx_x.main_id);
        pids.push_back(tx_y.main_id);
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, pids});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateAnalyticalIntersection(GeometryGraph &graph, uint32_t id1, uint32_t id2, const RPNParam &x_g,
                                             const RPNParam &y_g, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Analytical Intersection";
        Transaction tx_x = CreateScalar(graph, x_g, {});
        Transaction tx_y = CreateScalar(graph, y_g, {});
        tx.mutations.insert(tx.mutations.end(), tx_x.mutations.begin(), tx_x.mutations.end());
        tx.mutations.insert(tx.mutations.end(), tx_y.mutations.begin(), tx_y.mutations.end());

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Intersection;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        Data_AnalyticalIntersection d;
        d.branch_sign = 0;
        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, d});
        tx.mutations.push_back({
            Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{tx_x.main_id, tx_y.main_id}
        });
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateAnalyticalConstrainedPoint(GeometryGraph &graph, uint32_t target_id, const RPNParam &x_g,
                                                 const RPNParam &y_g, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Analytical Constrained";

        // 1. 创建辅助标量
        Transaction tx_x = CreateScalar(graph, x_g, {});
        Transaction tx_y = CreateScalar(graph, y_g, {});
        tx.mutations.insert(tx.mutations.end(), tx_x.mutations.begin(), tx_x.mutations.end());
        tx.mutations.insert(tx.mutations.end(), tx_y.mutations.begin(), tx_y.mutations.end());

        // 2. 创建主节点
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Constraint;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        Data_AnalyticalConstrainedPoint d;
        d.is_initialized = false;

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, d});

        // ★★★ 修复点：显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS, id, std::vector<uint32_t>{},
            std::vector<uint32_t>{target_id, tx_x.main_id, tx_y.main_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        // 3. 自动挂载 Label
        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateRatioPoint(GeometryGraph &graph, uint32_t p1, uint32_t p2, const RPNParam &ratio,
                                 const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Ratio Point";

        // 1. 创建比例标量
        Transaction tx_r = CreateScalar(graph, ratio, {});
        tx.mutations.insert(tx.mutations.end(), tx_r.mutations.begin(), tx_r.mutations.end());

        // 2. 创建主节点
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        final_cfg.style = (uint32_t) ObjectStyle::Point::Intersection; // 或 Point::Free，视需求而定
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_RatioPoint{}});

        // ★★★ 修复点：显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS, id, std::vector<uint32_t>{}, std::vector<uint32_t>{p1, p2, tx_r.main_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        // 3. 自动挂载 Label
        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    // =========================================================
    // 补全：垂线、平行线、切线、中点
    // =========================================================

    Transaction CreatePerpendicular(GeometryGraph &graph, uint32_t segment_id, uint32_t point_id, bool is_infinite,
                                    const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Perpendicular";

        // 1. 创建垂足节点 (Foot Point)
        // 垂足是一个依赖于(线, 点)的几何点
        uint32_t foot_id = graph.allocate_node();

        // 垂足的默认样式 (通常作为辅助点，可以用 Intersection 样式)
        GeoNode::VisualConfig foot_cfg;
        foot_cfg.style = (uint32_t) ObjectStyle::Point::Intersection;
        foot_cfg.name = graph.GenerateNextName(); // 给垂足也起个名

        tx.mutations.push_back({Mutation::Type::ACTIVE, foot_id, false, true});
        // 注意：垂足的数据类型是 Data_Point，由 Solver_PerpendicularFoot 计算
        tx.mutations.push_back({Mutation::Type::DATA, foot_id, std::monostate{}, Data_Point{0, 0}});

        // ★★★ 显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            foot_id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{segment_id, point_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, foot_id, GeoNode::VisualConfig{}, foot_cfg});

        // 自动挂载垂足的 Label (可选，但建议挂载以保持一致性)
        AppendSmartLabelMutations(tx, graph, foot_id, foot_cfg.name);


        // 2. 创建垂线节点 (Line)
        // 垂线连接 外部点(point_id) 和 垂足(foot_id)
        uint32_t line_id = graph.allocate_node();
        tx.main_id = line_id;

        GeoNode::VisualConfig line_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Line, line_cfg);
        if (line_cfg.name == "BasicObject") line_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, line_id, false, true});
        tx.mutations.push_back({
            Mutation::Type::DATA, line_id, std::monostate{}, Data_Line{point_id, foot_id, is_infinite}
        });

        // ★★★ 显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            line_id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{point_id, foot_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, line_id, GeoNode::VisualConfig{}, line_cfg});

        // 挂载垂线的 Label
        AppendSmartLabelMutations(tx, graph, line_id, line_cfg.name);

        return tx;
    }

    Transaction CreateParallel(GeometryGraph &graph, uint32_t segment_id, uint32_t point_id,
                               const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Parallel";

        // 1. 创建辅助参考点 (Helper Point)
        // 这个点用于确定方向，通常对用户不可见
        uint32_t helper_id = graph.allocate_node();

        GeoNode::VisualConfig helper_cfg;
        helper_cfg.name = "helper_" + std::to_string(helper_id);
        helper_cfg.opacity = 0.0f; // ★ 设置全透明以隐藏辅助点
        helper_cfg.style = (uint32_t) ObjectStyle::Point::Free;

        tx.mutations.push_back({Mutation::Type::ACTIVE, helper_id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, helper_id, std::monostate{}, Data_Point{0, 0}});

        // ★★★ 显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            helper_id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{segment_id, point_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, helper_id, GeoNode::VisualConfig{}, helper_cfg});
        // 辅助点不需要 Label

        // 2. 创建平行线 (Line)
        // 平行线连接 外部点(point_id) 和 辅助点(helper_id)
        uint32_t line_id = graph.allocate_node();
        tx.main_id = line_id;

        GeoNode::VisualConfig line_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Line, line_cfg);
        if (line_cfg.name == "BasicObject") line_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, line_id, false, true});
        // 平行线默认为无限长 (true)
        tx.mutations.push_back({Mutation::Type::DATA, line_id, std::monostate{}, Data_Line{point_id, helper_id, true}});

        // ★★★ 显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            line_id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{point_id, helper_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, line_id, GeoNode::VisualConfig{}, line_cfg});

        AppendSmartLabelMutations(tx, graph, line_id, line_cfg.name);

        return tx;
    }

    Transaction CreateTangent(GeometryGraph &graph, uint32_t constrained_point_id, const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Tangent";

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Line, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});

        // 切线使用的是 Data_CalculatedLine，初始全0
        // is_infinite = true
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_CalculatedLine{0, 0, 0, 0, true}});

        // ★★★ 显式构造 vector ★★★
        // 依赖于那个在曲线上的约束点
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{constrained_point_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateMidpoint(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id,
                               const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Midpoint";

        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        GeoNode::VisualConfig final_cfg = style;
        // 中点通常属于 Intersection 或 Constraint 类型，或者 Free 类型(作为几何点)
        // 这里使用 Intersection 样式作为默认，表示它是构造出来的
        final_cfg.style = (uint32_t) ObjectStyle::Point::Intersection;
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, Data_Point{0, 0}});

        // ★★★ 显式构造 vector ★★★
        tx.mutations.push_back({
            Mutation::Type::LINKS,
            id,
            std::vector<uint32_t>{},
            std::vector<uint32_t>{p1_id, p2_id}
        });

        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateParametricFunction(GeometryGraph &graph, const std::vector<MixedToken> &src_x,
                                         const std::vector<MixedToken> &src_y, double t_min, double t_max,
                                         const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Parametric";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        Data_DualRPN d;
        d.t_min = t_min;
        d.t_max = t_max;
        std::vector<uint32_t> parents;
        CompileMixedTokensInternal(src_x, d.tokens_x, d.bindings_x, parents);
        CompileMixedTokensInternal(src_y, d.tokens_y, d.bindings_y, parents);

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Parametric, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, std::move(d)});
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, parents});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateExplicitFunction(GeometryGraph &graph, const std::vector<MixedToken> &tokens,
                                       const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Explicit";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        Data_SingleRPN d;
        std::vector<uint32_t> parents;
        CompileMixedTokensInternal(tokens, d.tokens, d.bindings, parents);

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Explicit, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, std::move(d)});
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, parents});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    Transaction CreateImplicitFunction(GeometryGraph &graph, const std::vector<MixedToken> &tokens,
                                       const GeoNode::VisualConfig &style) {
        Transaction tx;
        tx.description = "Create Implicit";
        uint32_t id = graph.allocate_node();
        tx.main_id = id;

        Data_SingleRPN d;
        std::vector<uint32_t> parents;
        CompileMixedTokensInternal(tokens, d.tokens, d.bindings, parents);

        GeoNode::VisualConfig final_cfg = style;
        ValidateAndFixStyle(GeoNode::RenderType::Implicit, final_cfg);
        if (final_cfg.name == "BasicObject") final_cfg.name = graph.GenerateNextName();

        tx.mutations.push_back({Mutation::Type::ACTIVE, id, false, true});
        tx.mutations.push_back({Mutation::Type::DATA, id, std::monostate{}, std::move(d)});
        tx.mutations.push_back({Mutation::Type::LINKS, id, std::vector<uint32_t>{}, parents});
        tx.mutations.push_back({Mutation::Type::STYLE, id, GeoNode::VisualConfig{}, final_cfg});

        AppendSmartLabelMutations(tx, graph, id, final_cfg.name);
        return tx;
    }

    // =========================================================
    // 3. 更新事务 (Update Transactions)
    // =========================================================

    Transaction UpdateFreePoint_Tx(GeometryGraph &graph, uint32_t id, const RPNParam &x_e, const RPNParam &y_e) {
        Transaction tx;
        tx.description = "Move Point";
        if (id >= graph.node_pool.size()) return tx;
        const auto &node = graph.node_pool[id];
        if (node.parents.size() < 2) return tx;

        // 1. 生成 X 标量的全套更新
        uint32_t sx_id = node.parents[0];
        Data_Scalar dx;
        std::vector<uint32_t> px;
        CompileMixedTokensInternal(x_e, dx.tokens, dx.bindings, px);
        tx.mutations.push_back({Mutation::Type::DATA, sx_id, graph.node_pool[sx_id].data, dx});
        tx.mutations.push_back({Mutation::Type::LINKS, sx_id, graph.node_pool[sx_id].parents, px});

        // 2. 生成 Y 标量的全套更新
        uint32_t sy_id = node.parents[1];
        Data_Scalar dy;
        std::vector<uint32_t> py;
        CompileMixedTokensInternal(y_e, dy.tokens, dy.bindings, py);
        tx.mutations.push_back({Mutation::Type::DATA, sy_id, graph.node_pool[sy_id].data, dy});
        tx.mutations.push_back({Mutation::Type::LINKS, sy_id, graph.node_pool[sy_id].parents, py});

        return tx;
    }

    Transaction DeleteObject_Tx(GeometryGraph &graph, uint32_t id) {
        Transaction tx;
        tx.description = "Delete Object";

        // 递归收集
        std::vector<uint32_t> chain;
        std::function<void(uint32_t)> collect = [&](uint32_t cid) {
            chain.push_back(cid);
            // 注意：这里读取 graph 数据，但 Transaction 生成时 graph 尚未被修改
            // 所以读取的是删除前的拓扑，是安全的
            for (uint32_t child: graph.node_pool[cid].children) collect(child);
        };
        collect(id);

        std::reverse(chain.begin(), chain.end());
        for (uint32_t cid: chain) {
            if (graph.node_pool[cid].active) {
                tx.mutations.push_back({Mutation::Type::ACTIVE, cid, true, false});
            }
        }
        return tx;
    }

    Transaction UpdateStyle_Tx(GeometryGraph &graph, uint32_t id, const GeoNode::VisualConfig &new_style) {
        Transaction tx;
        tx.description = "Update Style";
        if (id < graph.node_pool.size()) {
            tx.mutations.push_back({Mutation::Type::STYLE, id, graph.node_pool[id].config, new_style});
        }
        return tx;
    }

    Transaction UpdateLabelPosition_Tx(GeometryGraph &graph, uint32_t label_id, double mouse_wx, double mouse_wy,
                                       const ViewState &view) {
        Transaction tx;
        tx.description = "Move Label";
        if (label_id >= graph.node_pool.size()) return tx;
        const auto &label_node = graph.node_pool[label_id];

        // 溯源：Label -> Anchor -> Host
        if (label_node.parents.empty()) return tx;
        uint32_t anchor_id = label_node.parents[0];
        const auto &anchor_node = graph.node_pool[anchor_id];
        if (anchor_node.parents.empty()) return tx;
        uint32_t host_id = anchor_node.parents[0];
        const auto &host_node = graph.node_pool[host_id];

        // 计算逻辑
        NDCMap m = BuildNDCMap(view);
        auto w2p = [&](double wx, double wy) {
            float nx = (wx - m.center_x) * m.scale_x;
            float ny = -(wy - m.center_y) * m.scale_y;
            return Vec2{(nx * 0.5f + 0.5f) * view.screen_width, (ny * -0.5f + 0.5f) * view.screen_height};
        };

        Vec2 a_px = w2p(anchor_node.result.x, anchor_node.result.y);
        Vec2 m_px = w2p(mouse_wx, mouse_wy);

        GeoNode::VisualConfig new_cfg = host_node.config;
        new_cfg.label_offset_x = m_px.x - a_px.x;
        new_cfg.label_offset_y = m_px.y - a_px.y;

        tx.mutations.push_back({Mutation::Type::STYLE, host_id, host_node.config, new_cfg});
        return tx;
    }
} // namespace GeoFactory
