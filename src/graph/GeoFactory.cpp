// --- 文件路径: src/graph/GeoFactory.cpp ---

#include "../../include/graph/GeoFactory.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include "../../include/graph/GeoSolver.h"
#include "../../include/plot/plotSegment.h"
#include "../../include/plot/plotCircle.h"

namespace GeoFactory {
    namespace {
        /**
         * @brief 内部私有：处理节点的基础装配
         * 此时 node.result 已经是 God Slot 结构，直接在内部配置元数据
         */
        void SetupNodeBase(GeometryGraph &graph, uint32_t id, const GeoNode::VisualConfig &config,
                           GeoNode::RenderType r_type, SolverFunc s_func, RenderTaskFunc t_func) {
            auto &node = graph.get_node_by_id(id);

            // 1. 同步视觉配置
            node.config = config;
            if (node.config.name == "BasicObject" || node.config.name.empty()) {
                node.config.name = graph.GenerateNextName();
            }

            // 2. 注册名字到全局映射
            graph.RegisterNodeName(node.config.name, id);

            // 3. 配置核心执行元数据
            node.render_type = r_type;
            node.solver = s_func; // 绑定逻辑计算逻辑
            node.render_task = t_func; // 绑定采样/渲染代理
            node.active = true;

            // 4. 初始化状态位 (利用位掩码逻辑)
            node.result.set_f(ComputedResult::VISIBLE, node.config.is_visible);
            node.result.set_f(ComputedResult::VALID, false); // 初始待计算
            graph.mark_as_seed(id);
        }

        /**
         * @brief 核心链接逻辑：将 Parser 产出的 String 槽位链接为 ID，并手动分配 RPN 堆内存
         */
        void CompileAndLinkRPNInternal(GeometryGraph &graph, uint32_t id, const std::string &infix_expr,
                                       std::vector<uint32_t> &out_parents) {
            auto &node = graph.get_node_by_id(id);

            // 调用编译期解析器
            auto compile_res = CAS::Parser::compile_infix_to_rpn(infix_expr);

            // --- 1. 分配并填充指令字节码 (Bytecode) ---
            uint32_t b_len = static_cast<uint32_t>(compile_res.bytecode.size());
            node.result.bytecode_len = b_len;
            // 极致性能：手动管理内存块，减少对象封装开销
            node.result.bytecode_ptr = new RPNToken[b_len];
            std::memcpy(node.result.bytecode_ptr, compile_res.bytecode.data(), b_len * sizeof(RPNToken));

            // --- 2. 准备补丁表 (Patch Table) ---
            uint32_t p_len = static_cast<uint32_t>(compile_res.binding_slots.size());
            node.result.patch_len = p_len;
            node.result.patch_ptr = new RuntimeBindingSlot[p_len];

            for (uint32_t k = 0; k < p_len; ++k) {
                const auto &raw = compile_res.binding_slots[k];
                auto &rt_slot = node.result.patch_ptr[k];

                rt_slot.rpn_index = raw.rpn_index;
                rt_slot.func_type = raw.func_type;

                if (raw.type == CAS::Parser::RPNBindingSlot::SlotType::VARIABLE) {
                    // --- 变量链接 ---
                    uint32_t target_id = graph.GetNodeID(raw.source_name);
                    rt_slot.dependency_ids.push_back(target_id);
                    out_parents.push_back(target_id);
                } else {
                    // --- 自定义黑箱函数链接 (Length, Area, ExtractX, ExtractY) ---
                    for (const auto &arg_name: raw.args) {
                        uint32_t target_id = graph.GetNodeID(arg_name);
                        rt_slot.dependency_ids.push_back(target_id);
                        out_parents.push_back(target_id);
                    }
                }
            }
        }

        // =========================================================
        // 渲染代理 (Delegates) - 适配 6 参数签名
        // =========================================================

        void Render_Point_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const std::vector<int32_t> &id_map,
                                   const ViewState &v, const NDCMap &m,
                                   oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
            if (!self.result.check_f(ComputedResult::VALID)) return;
            PointData pd{};
            // 大一统寻址：直接从联合体取 x, y
            world_to_clip_store(pd, self.result.x, self.result.y, m, self.id);
            q.push({self.id, {pd}});
        }

        void Render_Line_Delegate(GeoNode &self, const std::vector<GeoNode> &pool, const std::vector<int32_t> &id_map,
                                  const ViewState &v, const NDCMap &m,
                                  oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
            if (!self.result.check_f(ComputedResult::VALID)) return;

            // 利用 Solver 之前快照好的 x1,y1,x2,y2 直接绘图，减少内存查找
            bool is_infinite = self.result.check_f(ComputedResult::IS_INFINITE);

            process_two_point_line(&q, self.result.x1, self.result.y1, self.result.x2, self.result.y2,
                                   !is_infinite, self.id, v.world_origin, v.wppx, v.wppy,
                                   v.screen_width, v.screen_height, 0, 0, m);
        }

        // 递归辅助：收集后代
        void CollectDescendants(GeometryGraph &graph, uint32_t id, std::vector<uint32_t> &out_list) {
            if (!graph.is_alive(id)) return;
            auto &node = graph.get_node_by_id(id);
            for (uint32_t child_id: node.children) {
                CollectDescendants(graph, child_id, out_list);
            }
            out_list.push_back(id);
        }
    }

    // =========================================================
    // 1. 创建标量节点
    // =========================================================
    uint32_t AddInternalScalar(GeometryGraph &graph, const std::string &infix_expr,
                               const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();

        // 解析与链接 (注入 ComputedResult 逻辑区)
        std::vector<uint32_t> parents;
        CompileAndLinkRPNInternal(graph, id, infix_expr, parents);

        GeoNode::VisualConfig final_cfg = config;
        if (config.name == "BasicObject") final_cfg.is_visible = false;

        SetupNodeBase(graph, id, final_cfg, GeoNode::RenderType::Scalar, Solver_ScalarRPN, nullptr);


        graph.LinkAndRank(id, parents);
        return id;
    }

    // =========================================================
    // 2. 创建自由点 (由两个标量公式驱动)
    // =========================================================
    uint32_t AddFreePoint(GeometryGraph &graph, const std::string &x_expr, const std::string &y_expr,
                          const GeoNode::VisualConfig &config) {
        // 1. 创建坐标驱动引擎 (隐式)
        uint32_t sx = AddInternalScalar(graph, x_expr);
        uint32_t sy = AddInternalScalar(graph, y_expr);

        // 2. 分配主节点
        uint32_t id = graph.allocate_node();

        // 3. 装配
        SetupNodeBase(graph, id, config, GeoNode::RenderType::Point, Solver_StandardPoint, Render_Point_Delegate);

        // 4. 建立拓扑链
        graph.LinkAndRank(id, {sx, sy});
        return id;
    }

    // =========================================================
    // 3. 创建线段 (2P)
    // =========================================================
    uint32_t AddSegment(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        auto &node = graph.get_node_by_id(id);

        // 设置属性：线段(非无限)
        node.result.set_f(ComputedResult::IS_INFINITE, false);

        SetupNodeBase(graph, id, config, GeoNode::RenderType::Line, Solver_StandardLine, Render_Line_Delegate);

        graph.LinkAndRank(id, {p1_id, p2_id});
        return id;
    }

    // =========================================================
    // 4. 创建中点
    // =========================================================
    uint32_t AddMidPoint(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();

        SetupNodeBase(graph, id, config, GeoNode::RenderType::Point, Solver_Midpoint, Render_Point_Delegate);

        graph.LinkAndRank(id, {p1_id, p2_id});
        return id;
    }

    // =========================================================
    // 5. 创建约束点 (Heuristic)
    // =========================================================
    uint32_t AddConstrainedPoint(GeometryGraph &graph, uint32_t target_id,
                                 const std::string &x_expr, const std::string &y_expr,
                                 const GeoNode::VisualConfig &config) {
        // 1. 创建两个锚点标量
        uint32_t sx = AddInternalScalar(graph, x_expr);
        uint32_t sy = AddInternalScalar(graph, y_expr);

        // 2. 建立主节点
        uint32_t id = graph.allocate_node();
        auto &node = graph.get_node_by_id(id);

        // 存储吸附目标到整数槽 i0
        node.result.i0 = static_cast<int32_t>(target_id);

        SetupNodeBase(graph, id, config, GeoNode::RenderType::Point, Solver_ConstrainedPoint, Render_Point_Delegate);

        graph.LinkAndRank(id, {target_id, sx, sy});
        return id;
    }

    // =========================================================
    // 6. 极致递归物理删除
    // =========================================================
    void DeleteObjectRecursive(GeometryGraph &graph, uint32_t target_id) {
        if (!graph.is_alive(target_id)) return;

        // 1. 递归扫描所有受灾子代
        std::vector<uint32_t> targets;
        CollectDescendants(graph, target_id, targets);

        // 2. 消除重复并保持 ID 唯一性
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

        // 3. 按照 ID 逐个执行销毁动作
        for (uint32_t id: targets) {
            if (!graph.is_alive(id)) continue;

            auto &node = graph.get_node_by_id(id);

            // --- A. 物理内存回收 (核心责任) ---
            if (node.result.bytecode_ptr) {
                delete[] node.result.bytecode_ptr;
                node.result.bytecode_ptr = nullptr;
            }
            if (node.result.patch_ptr) {
                delete[] node.result.patch_ptr;
                node.result.patch_ptr = nullptr;
            }

            // --- B. 断开父级链接，防止野引用 ---
            for (uint32_t pid: node.parents) {
                if (graph.is_alive(pid)) {
                    auto &p_kids = graph.get_node_by_id(pid).children;
                    p_kids.erase(std::remove(p_kids.begin(), p_kids.end(), id), p_kids.end());
                }
            }

            // --- C. 映射表注销与物理位移 ---
            // 这一步包含 UnregisterName, DetachFromBucket, erase 和 LUT 重整
            graph.physical_delete(id);
        }
    }

    void InternalUpdateScalar(GeometryGraph &graph, uint32_t scalar_id, const std::string &new_infix) {
        auto &node = graph.get_node_by_id(scalar_id);

        // 1. 物理清理（防止 WASM 堆积）
        if (node.result.bytecode_ptr) {
            delete[] node.result.bytecode_ptr;
            node.result.bytecode_ptr = nullptr;
        }
        if (node.result.patch_ptr) {
            delete[] node.result.patch_ptr;
            node.result.patch_ptr = nullptr;
        }

        // 2. 重新编译新公式到 ComputedResult
        std::vector<uint32_t> new_parents;
        CompileAndLinkRPNInternal(graph, scalar_id, new_infix, new_parents);

        // 3. 重新建立拓扑连接（如果新公式引入了新变量，Rank 会在此刷新）
        graph.LinkAndRank(scalar_id, new_parents);

        // 4. 标记为震源，等待 calculate_points_core 扩散
        graph.mark_as_seed(scalar_id);
    }

    void UpdatePointScalar(GeometryGraph &graph, uint32_t point_id,
                           const std::string &new_x_expr,
                           const std::string &new_y_expr) {
        // 1. 获取点对象并执行严格断言
        auto &point_node = graph.get_node_by_id(point_id);

        if (point_node.parents.size() != 2) {
            throw std::runtime_error("UpdatePointScalar Failure: Target node (ID " +
                                     std::to_string(point_id) + ") must have exactly 2 parents.");
        }

        // 2. 提取父标量 ID (按照 AddFreePoint 的约定：0 是 X, 1 是 Y)
        uint32_t sx_id = point_node.parents[0];
        uint32_t sy_id = point_node.parents[1];

        // 3. 分别更新两个标量
        InternalUpdateScalar(graph, sx_id, new_x_expr);
        InternalUpdateScalar(graph, sy_id, new_y_expr);

        // 注意：这里不需要再 mark_as_seed(point_id)，
        // 因为 FastScan 会因为它的父亲（sx, sy）脏了而自动抓到它。
    }

    // 辅助：根据当前 View 基础属性，刷新所有派生矩阵属性
    void RefreshViewState(GeometryGraph& graph) {
        auto& v = graph.view;
        double aspect = v.screen_width / v.screen_height;

        // 计算每像素代表的世界距离 (World Per Pixel)
        v.wppx = (2.0 * aspect) / (v.zoom * v.screen_width);
        v.wppy = -2.0 / (v.zoom * v.screen_height);

        // 计算世界原点 (屏幕左上角在世界坐标系中的位置)
        v.world_origin.x = v.offset_x - (v.screen_width * 0.5) * v.wppx;
        v.world_origin.y = v.offset_y - (v.screen_height * 0.5) * v.wppy;
    }

    void UpdateViewTransform(GeometryGraph& graph, double ox, double oy, double zoom) {
        graph.view.offset_x = ox;
        graph.view.offset_y = oy;
        graph.view.zoom     = zoom;
        RefreshViewState(graph);
    }

    void UpdateViewSize(GeometryGraph& graph, double w, double h) {
        graph.view.screen_width = w;
        graph.view.screen_height = h;
        RefreshViewState(graph);
    }

} // namespace GeoFactory
