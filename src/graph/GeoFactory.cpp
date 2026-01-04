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

#include "../../include/graph/GeoGraph.h"

namespace GeoFactory {
    static void PostProcessNodeLabel(GeometryGraph& graph, uint32_t host_id) {
        auto& host_node = graph.node_pool[host_id];

        // 只有点、线、圆、函数等需要标签，Scalar 和 None 不需要
        using RT = GeoNode::RenderType;
        if (host_node.render_type == RT::None || host_node.render_type == RT::Scalar || host_node.render_type == RT::Text) {
            return;
        }

        // 调用之前定义的 CreateSmartTextLabel
        // 它内部会创建：1个特化锚点(Helper_ConstrainedPoint) + 1个文字节点(Text)
        CreateSmartTextLabel(graph, host_id);
    }

    // 位于 GeoFactory.cpp 内部辅助逻辑
    static void InitializeNodeConfig(GeometryGraph& graph, GeoNode& node, const GeoNode::VisualConfig& input_config) {
        // 首先拷贝配置
        node.config = input_config;

        // --- 新增：样式合法性检查与自动纠错 ---
        using RT = GeoNode::RenderType;
        uint32_t current_style = node.config.style;

        if (node.render_type == RT::Point) {
            // 规则：点类型必须应用 0x2xxx 区间的样式
            if (!GeoNode::ObjectStyle::IsPoint(current_style)) {
                // 纠错：如果是点对象却传了线样式（或无效值），强制重置为默认圆形点
                node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Free);

                // 可选：添加调试日志，帮助开发者定位 JS 传参问题
                // std::cout << "[Warning] Object ID " << node.id << " is a Point but received Line style. Resetting to Point::Circle." << std::endl;
            }
        }
        else if (node.render_type == RT::Line || node.render_type == RT::Circle ||
                 node.render_type == RT::Explicit || node.render_type == RT::Parametric ||
                 node.render_type == RT::Implicit) {
            // 规则：所有属于“线/轨迹”类的对象必须应用 0x1xxx 区间的样式
            if (!GeoNode::ObjectStyle::IsLine(current_style)) {
                // 纠错：强制重置为实线
                node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Line::Solid);
            }
                 }
        // --- 检查结束 ---

        // 1. 自动命名 (a, b, c...)
        if (node.config.name == "BasicObject") {
            node.config.name = graph.GenerateNextName();
        }

        // 2. 自动创建并挂载 Label 孩子节点
        PostProcessNodeLabel(graph, node.id);
    }
    // 位于 GeoFactory.cpp 内部辅助
    static void Internal_UpdateScalarRPN(GeometryGraph& graph, uint32_t scalar_id, const RPNParam& expr) {
        GeoNode& node = graph.node_pool[scalar_id];
        auto* d = std::get_if<Data_Scalar>(&node.data);
        if (!d) return;

        // 1. 清空旧的 RPN 数据
        d->tokens.clear();
        d->bindings.clear();

        // 2. 重新编译 Token 和 依赖
        std::vector<uint32_t> new_parents;
        for (const auto& item : expr) {
            if (std::holds_alternative<RPNTokenType>(item)) {
                d->tokens.push_back({std::get<RPNTokenType>(item), 0.0});
            } else if (std::holds_alternative<double>(item)) {
                d->tokens.push_back({RPNTokenType::PUSH_CONST, std::get<double>(item)});
            } else if (std::holds_alternative<Ref>(item)) {
                uint32_t ref_id = std::get<Ref>(item).id;
                d->tokens.push_back({RPNTokenType::PUSH_CONST, 0.0}); // 占位

                uint32_t p_idx = 0; bool found = false;
                for(size_t i=0; i<new_parents.size(); ++i) if(new_parents[i]==ref_id){p_idx=(uint32_t)i;found=true;break;}
                if(!found){new_parents.push_back(ref_id); p_idx=(uint32_t)new_parents.size()-1;}

                d->bindings.push_back({(uint32_t)(d->tokens.size()-1), p_idx, RPNBinding::VALUE});
            }
        }

        // 3. 更新拓扑（如果依赖项变了）
        node.parents = new_parents;
        // 注意：在正式版本中，这里需要根据 new_parents 重新调用 LinkAndRank
        // 但在简单测试中，假设依赖的点对象没变，仅 Touch 即可。
        graph.TouchNode(scalar_id);
    }



    // 位于 GeoFactory.cpp 内部

    /**
     * @brief 核心拓扑管理函数：负责建立/重构父子关系，并确保 Rank 绝对正确
     * @param graph 几何图引用
     * @param child_id 需要建立/更新依赖的节点 ID
     * @param new_parent_ids 该节点的新父节点集合 (全量覆盖)
     */
    static void LinkAndRank(GeometryGraph& graph, uint32_t child_id, const std::vector<uint32_t>& new_parent_ids) {
        auto& child_node = graph.node_pool[child_id];

        // --- 第一步：切断旧链接 (清理旧生父) ---
        // 遍历当前已有的 parents，告诉他们：“我不再是你们的孩子了”
        for (uint32_t old_pid : child_node.parents) {
            auto& old_p_children = graph.node_pool[old_pid].children;
            old_p_children.erase(
                std::remove(old_p_children.begin(), old_p_children.end(), child_id),
                old_p_children.end()
            );
        }

        // --- 第二步：建立新链接与安全检查 ---
        child_node.parents = new_parent_ids; // 覆盖旧父 ID 列表

        // 重新判定节点属性（是否依赖 Buffer 的传播）
        // 假设你有之前讨论的 is_heuristic 判断
        child_node.is_heuristic = is_heuristic_solver(child_node.solver);
        child_node.is_buffer_dependent = false; // 初始重置，稍后在循环中传播

        for (uint32_t pid : new_parent_ids) {
            // 1. 安全检查：防止回环
            if (graph.DetectCycle(child_id, pid)) {
                throw std::runtime_error("拓扑冲突：检测到循环依赖！");
            }

            // 2. 建立反向链接
            graph.node_pool[pid].children.push_back(child_id);

            // 3. 属性传播：只要有一个父亲依赖 Buffer，孩子就依赖 Buffer
            if (graph.node_pool[pid].is_heuristic || graph.node_pool[pid].is_buffer_dependent) {
                child_node.is_buffer_dependent = true;
            }
        }

        // --- 第三步：核心 Rank 递归传播 ---
        // 这个递归函数确保了“牵一发而动全身”，下游所有节点的 Rank 都会自动对齐
        graph.UpdateRankRecursive(child_id);
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
    static void Render_Text_Delegate(
    GeoNode& self,
    const std::vector<GeoNode>& pool,
    const ViewState& v,
    const NDCMap& m,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q
) {
        auto* data = std::get_if<Data_TextLabel>(&self.data);
        if (!data) return;

        // 找到宿主节点（用于提取 Name 和 Config）
        // 逻辑：TextNode -> AnchorNode -> HostNode
        uint32_t anchor_id = self.parents[0];
        if (anchor_id >= pool.size()) return;
        const auto& anchor_node = pool[anchor_id];

        if (anchor_node.parents.empty()) return;
        uint32_t host_id = anchor_node.parents[0];
        const auto& host_node = pool[host_id];

        // 如果宿主关闭了标签，不产生任何点
        if (!host_node.config.show_label) return;

        // 1. 将锚点投影到 CLIP 空间
        PointData pd{};
        world_to_clip_store(pd, data->world_x, data->world_y, m, self.id);

        // 2. 应用配置中的像素偏移 (转换到 NDC 空间)
        const auto& cfg = host_node.config;
        pd.position.x += (cfg.label_offset_x / (float)v.screen_width) * 2.0f;
        pd.position.y -= (cfg.label_offset_y / (float)v.screen_height) * 2.0f; // Y 轴反转

        // 3. 提交给渲染队列 (JS 端会根据 id 获取宿主 Name)
        q.push({ self.id, {pd} });
    }
    uint32_t CreateScalar(GeometryGraph& graph, const RPNParam& expr,const GeoNode::GeoNode::VisualConfig& style ) {
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
        double t_min, double t_max,const GeoNode::VisualConfig& style
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
        node.render_task = Render_Parametric_Delegate; // 绑定参数方程


        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }

    // =========================================================
    // 创建隐函数：f(x, y) = 0
    // =========================================================
    uint32_t CreateImplicitFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens,const GeoNode::VisualConfig& style
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
        node.render_task = Render_Implicit_Delegate; // 绑定隐函数
        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);


        return id;
    }


    uint32_t CreatePoint(GeometryGraph& graph, const RPNParam& x_expr, const RPNParam& y_expr,const GeoNode::VisualConfig& style) {
        // 先创建两个标量节点
        uint32_t sx = CreateScalar(graph, x_expr);
        uint32_t sy = CreateScalar(graph, y_expr);

        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;
        node.parents = { sx, sy };
        node.data = Data_Point{}; // 初始 0,0
        node.solver = Solver_StandardPoint;


        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Free);






        InitializeNodeConfig(graph, node, node.config);


        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        node.render_task = Render_Point_Delegate; // 绑定点渲染
        return id;
    }





    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, const RPNParam& radius_expr,const GeoNode::VisualConfig& style ) {
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

        node.data = d;
        node.solver = Solver_Circle;
        node.render_task = Render_Circle_Delegate; // 绑定圆渲染





        InitializeNodeConfig(graph, node, style);

        // 5. 建立拓扑链接并标记脏
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);


        return id;
    }


    // =========================================================
    // 显式函数创建 (Token 编译)
    // =========================================================
    uint32_t CreateExplicitFunction(GeometryGraph& graph, const std::vector<MixedToken>& tokens,const GeoNode::VisualConfig& style) {
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
        node.render_task = Render_Explicit_Delegate; // 绑定显函数



        // node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Line::Solid);

        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }

    uint32_t CreateLine(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite,const GeoNode::VisualConfig& style) {
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
        node.render_task = Render_Line_Delegate; // 绑定线渲染


        // node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Line::Solid);

        InitializeNodeConfig(graph, node, style);


        LinkAndRank(graph, id, node.parents);


        return id;
    }

    uint32_t CreateMidpoint(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id,const GeoNode::VisualConfig& style) {
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
        node.render_task = Render_Point_Delegate; // 绑定点渲染
        node.config = style;

        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Intersection);

        InitializeNodeConfig(graph, node, node.config);

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
        const std::vector<uint32_t>& parent_ids,const GeoNode::VisualConfig& style
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


        // node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Line::Solid);
        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);

        graph.TouchNode(id); // 注册计算
        return id;
    }

    uint32_t CreatePerpendicular(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id, bool is_infinite, const GeoNode::VisualConfig& style) {
        // 1. 严格类型检查
        if (segment_id >= graph.node_pool.size() || point_id >= graph.node_pool.size() ||
            graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line ||
            graph.node_pool[point_id].render_type != GeoNode::RenderType::Point) {
            throw std::runtime_error("Perpendicular requires a Line and a Point as dependencies.");
        }

        // =========================================================
        // 第一步：创建垂足节点 (Foot Point)
        // =========================================================
        uint32_t foot_id = graph.allocate_node();
        GeoNode& foot_node = graph.node_pool[foot_id];

        // 设置节点类型与渲染代理（让垂足在屏幕上可见）
        foot_node.render_type = GeoNode::RenderType::Point;
        foot_node.render_task = Render_Point_Delegate;

        // 设置逻辑属性
        foot_node.parents = { segment_id, point_id };
        foot_node.data = Data_Point{ 0, 0 };
        foot_node.solver = Solver_PerpendicularFoot;

        // 初始化垂足外观：默认使用“交点”样式，并执行自动命名和Label创建
        GeoNode::VisualConfig foot_config;
        foot_config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Intersection); // 假设用方块表示垂足
        InitializeNodeConfig(graph, foot_node, foot_config);

        // 使用核心拓扑函数：处理双向连接、循环检测和 Rank 递归
        LinkAndRank(graph, foot_id, foot_node.parents);
        graph.TouchNode(foot_id);

        // =========================================================
        // 第二步：创建垂线节点 (Perpendicular Line)
        // =========================================================
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        // 设置节点类型与渲染代理
        line_node.render_type = GeoNode::RenderType::Line;
        line_node.render_task = Render_Line_Delegate;

        // 逻辑依赖：垂线是由“外部点”和“刚才算的垂足”构成的
        line_node.parents = { point_id, foot_id };
        line_node.data = Data_Line{ point_id, foot_id, is_infinite };
        line_node.solver = nullptr; // 直线不需要独立 Solver，渲染时提取端点坐标即可


        // 初始化垂线外观：使用用户传入的 style
        InitializeNodeConfig(graph, line_node, style);

        // 使用核心拓扑函数建立关系
        LinkAndRank(graph, line_id, line_node.parents);
        graph.TouchNode(line_id);

        return line_id;
    }

    uint32_t CreateParallel(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id, const GeoNode::VisualConfig& style) {
        // 1. 严格类型检查
        if (segment_id >= graph.node_pool.size() || point_id >= graph.node_pool.size() ||
            graph.node_pool[segment_id].render_type != GeoNode::RenderType::Line ||
            graph.node_pool[point_id].render_type != GeoNode::RenderType::Point) {
             throw std::runtime_error("Parallel requires a Line and a Point as dependencies.");
        }

        // =========================================================
        // 第一步：创建辅助参考点 (Helper Point)
        // 用于确定平行线的方向向量，该点对用户不可见
        // =========================================================
        uint32_t helper_id = graph.allocate_node();
        GeoNode& helper_node = graph.node_pool[helper_id];

        helper_node.render_type = GeoNode::RenderType::Point;
        helper_node.is_visible = false;    // 核心：不显示
        helper_node.render_task = nullptr; // 核心：不参与渲染采样
        helper_node.config.name = "Parallel Line Helper Point";


        // 逻辑属性：依赖参考线和外部点来确定偏移位置
        helper_node.parents = { segment_id, point_id };
        helper_node.data = Data_Point{ 0, 0 };
        helper_node.solver = Solver_ParallelPoint;

        // 注意：辅助点不需要调用 InitializeNodeConfig，
        // 因为它不需要自动命名，也不需要创建 Label 节点。

        // 使用核心拓扑函数建立链接
        LinkAndRank(graph, helper_id, helper_node.parents);
        graph.TouchNode(helper_id);

        // =========================================================
        // 第二步：创建平行线节点 (Parallel Line)
        // =========================================================
        uint32_t line_id = graph.allocate_node();
        GeoNode& line_node = graph.node_pool[line_id];

        // 设置节点类型与渲染代理
        line_node.render_type = GeoNode::RenderType::Line;
        line_node.render_task = Render_Line_Delegate;

        // 逻辑依赖：直线由“外部点”和“辅助参考点”连线确定
        line_node.parents = { point_id, helper_id };
        line_node.data = Data_Line{ point_id, helper_id, true }; // true 表示无限长直线
        line_node.solver = nullptr; // 直线在渲染阶段提取端点世界坐标


        // line_node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Line::Solid);

        // 初始化样式配置（自动命名、挂载 Label 孩子节点）
        InitializeNodeConfig(graph, line_node, style);

        // 建立最终拓扑链接
        LinkAndRank(graph, line_id, line_node.parents);
        graph.TouchNode(line_id);

        return line_id;
    }

    // --- 文件路径: src/graph/GeoFactory.cpp ---

    uint32_t CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, const RPNParam& x_expr, const RPNParam& y_expr,const GeoNode::VisualConfig& style) {
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


        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Constrained);
        InitializeNodeConfig(graph, node, node.config);


        // 6. 拓扑链接与 JIT 注册
        // LinkAndRank 会自动执行针对这三个父节点的循环依赖检测
        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }

    uint32_t CreateTangent(GeometryGraph& graph, uint32_t constrained_point_id,const GeoNode::VisualConfig& style) {
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
        node.render_task = Render_Line_Delegate; // 切线也是线，复用 Line 代理
        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);

        // ★ 统一修改：不再抢跑
        // node.solver(node, graph.node_pool);
        graph.TouchNode(id); // 注册计算

        return id;
    }
    uint32_t CreateMeasureLength(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id,const GeoNode::VisualConfig& style) {
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
    const std::vector<uint32_t>& target_ids,const GeoNode::VisualConfig& style
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

        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Intersection);
        InitializeNodeConfig(graph, node, node.config);



        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);
        return id;
    }
    uint32_t CreateAnalyticalIntersection(
    GeometryGraph& graph,
    uint32_t id1, uint32_t id2,
    const RPNParam& x_guess,
    const RPNParam& y_guess,const GeoNode::VisualConfig& style
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
        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Intersection);
        InitializeNodeConfig(graph, node, node.config);


        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }
    uint32_t CreateAnalyticalConstrainedPoint(
    GeometryGraph& graph,
    uint32_t target_id,
    const RPNParam& x_guess,
    const RPNParam& y_guess,const GeoNode::VisualConfig& style
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


        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Constrained);
        InitializeNodeConfig(graph, node, node.config);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }
    uint32_t CreateRatioPoint(
    GeometryGraph& graph,
    uint32_t p1_id,
    uint32_t p2_id,
    const RPNParam& ratio_expr,const GeoNode::VisualConfig& style
) {
        // 1. 提升比例表达式为标量节点
        uint32_t s_ratio = CreateScalar(graph, ratio_expr);

        // 2. 分配节点
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];
        node.render_type = GeoNode::RenderType::Point;

        // 3. 建立依赖：[P1, P2, Ratio]
        node.parents = { p1_id, p2_id, s_ratio };

        node.data = Data_RatioPoint{};
        node.solver = Solver_RatioPoint;
        node.render_task = Render_Point_Delegate; // 使用修正后的通用点渲染


        node.config = style;
        node.config.style = static_cast<uint32_t>(GeoNode::ObjectStyle::Point::Intersection);
        InitializeNodeConfig(graph, node, node.config);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }
    uint32_t CreateCircleThreePoints(
    GeometryGraph& graph,
    uint32_t p1_id,
    uint32_t p2_id,
    uint32_t p3_id,const GeoNode::VisualConfig& style
) {
        uint32_t id = graph.allocate_node();
        GeoNode& node = graph.node_pool[id];

        // 归类为圆
        node.render_type = GeoNode::RenderType::Circle;

        // 建立三元依赖
        node.parents = { p1_id, p2_id, p3_id };

        // 初始化圆数据载体 (用于存储计算出的 cx, cy, radius)
        node.data = Data_Circle{};

        // 绑定专用求解器
        node.solver = Solver_CircleThreePoints;

        // 绑定圆的渲染代理（它内部会调用 process_circle_specialized 即 plotCircle）
        node.render_task = Render_Circle_Delegate;



        InitializeNodeConfig(graph, node, style);

        LinkAndRank(graph, id, node.parents);
        graph.TouchNode(id);

        return id;
    }
    void UpdateFreePoint(GeometryGraph& graph, uint32_t id, const RPNParam& x_expr, const RPNParam& y_expr) {
        if (id >= graph.node_pool.size()) return;
        GeoNode& node = graph.node_pool[id];

        // 自由点必须有两个父节点 (Scalar X, Scalar Y)
        if (node.parents.size() < 2) return;

        // 分别更新两个标量父节点
        Internal_UpdateScalarRPN(graph, node.parents[0], x_expr);
        Internal_UpdateScalarRPN(graph, node.parents[1], y_expr);

        // 标记点本身为脏
        graph.TouchNode(id);
    }

    void UpdateAnalyticalConstrainedPoint(GeometryGraph& graph, uint32_t id, const RPNParam& x_expr, const RPNParam& y_expr) {
        if (id >= graph.node_pool.size()) return;
        GeoNode& node = graph.node_pool[id];
        auto* data = std::get_if<Data_AnalyticalConstrainedPoint>(&node.data);
        if (!data) return;

        // 重置初始化标记，Solver 看到 false 会重新投影计算锁定 t
        data->is_initialized = false;

        // 解析约束点 parents: [TargetObj, Scalar_GuessX, Scalar_GuessY]
        if (node.parents.size() >= 3) {
            Internal_UpdateScalarRPN(graph, node.parents[1], x_expr);
            Internal_UpdateScalarRPN(graph, node.parents[2], y_expr);
        }

        graph.TouchNode(id);
    }

    // 3. 更新图解约束点
    void UpdateConstrainedPoint(GeometryGraph& graph, uint32_t id, const RPNParam& x_expr, const RPNParam& y_expr) {
        if (id >= graph.node_pool.size()) return;
        GeoNode& node = graph.node_pool[id];

        // 图解约束点的 parents 布局通常也是: [TargetShape, ScalarGuessX, ScalarGuessY]
        if (node.parents.size() < 3) return;

        // 更新作为“吸附锚点”的两个标量公式
        Internal_UpdateScalarRPN(graph, node.parents[1], x_expr);
        Internal_UpdateScalarRPN(graph, node.parents[2], y_expr);

        // 标记点本身为脏，使其 Solver 在 SolveFrame 中运行寻找最近点的逻辑
        graph.TouchNode(id);
    }

    void UpdateFunctionRPN(GeometryGraph& graph, uint32_t id, const std::vector<MixedToken>& new_tokens_x, const std::vector<MixedToken>& new_tokens_y) {
        if (id >= graph.node_pool.size()) return;
        GeoNode& node = graph.node_pool[id];

        if (auto* d = std::get_if<Data_SingleRPN>(&node.data)) {
            // 处理显式函数或隐函数
            d->tokens.clear();
            d->bindings.clear();
            std::vector<uint32_t> new_parents;
            CompileMixedTokens(new_tokens_x, d->tokens, d->bindings, new_parents);
            node.parents = new_parents;
        }
        else if (auto* d = std::get_if<Data_DualRPN>(&node.data)) {
            // 处理参数方程 (x(t) 和 y(t))
            d->tokens_x.clear(); d->bindings_x.clear();
            d->tokens_y.clear(); d->bindings_y.clear();
            std::vector<uint32_t> new_parents;
            CompileMixedTokens(new_tokens_x, d->tokens_x, d->bindings_x, new_parents);
            CompileMixedTokens(new_tokens_y, d->tokens_y, d->bindings_y, new_parents);
            node.parents = new_parents;
        }

        graph.TouchNode(id);
    }
    uint32_t CreateSmartTextLabel(GeometryGraph& graph, uint32_t target_id) {
        const auto& target_node = graph.node_pool[target_id];

        // 1. 获取初始位置（同之前逻辑）
        double gx = 0, gy = 0;
        if (target_node.current_point_count > 0) {
            uint32_t mid = target_node.buffer_offset + (target_node.current_point_count / 2);
            const auto& pt = wasm_final_contiguous_buffer[mid];
            NDCMap m = BuildNDCMap(g_global_view_state);
            gx = m.center_x + (double)pt.position.x / m.scale_x;
            gy = m.center_y - (double)pt.position.y / m.scale_y;
        }

        // 2. 创建一个“不可见”的辅助锚点节点
        uint32_t sx = CreateScalar(graph, {gx});
        uint32_t sy = CreateScalar(graph, {gy});

        uint32_t anchor_id = graph.allocate_node();
        GeoNode& anchor_node = graph.node_pool[anchor_id];
        anchor_node.render_type = GeoNode::RenderType::None; // 不渲染此点
        anchor_node.parents = { target_id, sx, sy };
        anchor_node.data = Data_Point{};
        anchor_node.solver = Solver_LabelAnchorPoint; // ★ 使用特化求解器
        anchor_node.is_visible = false;

        LinkAndRank(graph, anchor_id, anchor_node.parents);

        // 3. 创建真正的 TextLabel 节点
        uint32_t label_id = graph.allocate_node();
        GeoNode& label_node = graph.node_pool[label_id];
        label_node.render_type = GeoNode::RenderType::Text;
        label_node.parents = { anchor_id }; // 依赖于上面的锚点
        label_node.data = Data_TextLabel{};
        label_node.solver = Solver_TextLabel;
        label_node.render_task = Render_Text_Delegate;

        LinkAndRank(graph, label_id, label_node.parents);

        // 注册计算
        graph.TouchNode(anchor_id);
        graph.TouchNode(label_id);

        return label_id;
    }



    void UpdateStyle(GeometryGraph& graph, uint32_t id,const ViewState& view,const GeoNode::VisualConfig& new_style,const std::vector<uint32_t>& draw_order) {
        if (id >= graph.node_pool.size()) return;

        auto& host_node = graph.node_pool[id];

        // 1. 更新宿主节点的配置 (包括 Name, Color, Thickness, label_offset 等)
        host_node.config = new_style;

        // 2. 收集需要重绘的节点列表
        // 至少宿主节点自己需要重绘 (改变颜色/粗细)
        std::vector<uint32_t> render_dirty_ids;
        render_dirty_ids.push_back(id);

        // 3. 寻找并更新关联的 TextLabel 节点
        // 拓扑结构是: Host -> Anchor (Helper) -> TextNode
        for (uint32_t child_id : host_node.children) {
            auto& child = graph.node_pool[child_id];

            // 检查这个孩子是不是专门为 Label 服务特化锚点
            if (child.solver == Solver_LabelAnchorPoint) {
                // 继续向下找真正的 Text 节点
                for (uint32_t grandchild_id : child.children) {
                    if (graph.node_pool[grandchild_id].render_type == GeoNode::RenderType::Text) {
                        render_dirty_ids.push_back(grandchild_id);
                    }
                }
            }
        }

        // 4. 关键：跳过 SolveFrame，直接触发增量渲染
        // 逻辑：
        // - 对于解析点：Render_Point_Delegate 会读取新的颜色，并利用缓存的世界坐标重新投影。
        // - 对于文字：Render_Text_Delegate 会读取宿主最新的 config.name 和 label_offset。
        // - 结果：新数据被追加到 Ring Buffer 末尾，旧数据失效。
        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph.node_pool,
            draw_order, // 保持原始画图顺序(图层)
            render_dirty_ids,  // 仅仅重绘这几个节点
            view,
            RenderUpdateMode::Incremental
        );
    }



    void UpdateLabelPosition(GeometryGraph& graph, uint32_t label_id, double mouse_wx, double mouse_wy,const std::vector<uint32_t>& draw_order, const ViewState& view) {
    if (label_id >= graph.node_pool.size()) return;

    GeoNode& label_node = graph.node_pool[label_id];
    if (label_node.render_type != GeoNode::RenderType::Text) return;

    // 1. 溯源：TextNode -> AnchorNode -> HostNode
    uint32_t anchor_id = label_node.parents[0];
    uint32_t host_id = graph.node_pool[anchor_id].parents[0];
    auto& host_node = graph.node_pool[host_id];

    // 2. 获取锚点当前的世界坐标
    double anchor_wx = ExtractValue(graph.node_pool[anchor_id], RPNBinding::POS_X, graph.node_pool);
    double anchor_wy = ExtractValue(graph.node_pool[anchor_id], RPNBinding::POS_Y, graph.node_pool);

    // 3. 坐标转换：将 锚点世界坐标 和 鼠标世界坐标 都转为 屏幕像素坐标
    NDCMap m = BuildNDCMap(view);

    auto world_to_pixel = [&](double wx, double wy) -> Vec2 {
        // World -> CLIP (NDC)
        float nx = static_cast<float>((wx - m.center_x) * m.scale_x);
        float ny = -static_cast<float>((wy - m.center_y) * m.scale_y); // 注意 Y 轴反转
        // NDC -> Pixel
        float px = (nx * 0.5f + 0.5f) * (float)view.screen_width;
        float py = (ny * -0.5f + 0.5f) * (float)view.screen_height;
        return { px, py };
    };

    Vec2 anchor_px = world_to_pixel(anchor_wx, anchor_wy);
    Vec2 mouse_px  = world_to_pixel(mouse_wx, mouse_wy);

    // 4. 计算像素差值 (新的 Offset)
    float new_offset_x = mouse_px.x - anchor_px.x;
    float new_offset_y = mouse_px.y - anchor_px.y;

    // 5. 【可选】应用拖拽范围限制（防止标签飞太远找不到了）
    float dist = std::sqrt(new_offset_x * new_offset_x + new_offset_y * new_offset_y);
    float max_radius = 150.0f; // 允许离开锚点 150 像素
    if (dist > max_radius) {
        new_offset_x *= (max_radius / dist);
        new_offset_y *= (max_radius / dist);
    }

    // 6. 更新配置
    host_node.config.label_offset_x = new_offset_x;
    host_node.config.label_offset_y = new_offset_y;

    // 7. 立即重绘（局部更新模式）
    // 只需要重绘这个文字节点即可，不需要 SolveFrame
    std::vector<uint32_t> targets = { label_id };
    calculate_points_core(
        wasm_final_contiguous_buffer,
        wasm_function_ranges_buffer,
        graph.node_pool,
        draw_order,
        targets,
        view,
        RenderUpdateMode::Incremental
    );
}



}
