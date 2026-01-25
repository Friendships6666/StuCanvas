// --- 文件路径: src/graph/GeoFactory.cpp ---

#include "../../include/graph/GeoFactory.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include "../../include/graph/GeoSolver.h"
#include "../../include/plot/plotSegment.h"
#include <algorithm>

namespace GeoFactory {
    namespace {
        /**
         * @brief 内部辅助：递归收集所有子孙节点 ID (用于删除)
         */
        void CollectDescendants(GeometryGraph &graph, uint32_t id, std::vector<uint32_t> &out_list) {
            if (!graph.is_alive(id)) return;
            auto &node = graph.get_node_by_id(id);
            for (uint32_t child_id : node.children) {
                CollectDescendants(graph, child_id, out_list);
            }
            out_list.push_back(id);
        }

        /**
         * @brief 渲染委托：单个几何点渲染
         * 产出：1个 int16 压缩坐标点
         */
        void Render_Point_Delegate(GeoNode &self, GeometryGraph &graph, const ViewState &view,
                                   oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>> &q) {
            if (!GeoStatus::ok(self.status)) return;

            // 1. 获取压缩坐标
            Vec2i pd = view.WorldToClipNoOffset(self.result.x_view, self.result.y_view);

            // 2. 构造包含单点的 vector
            // 使用初始化列表直接构造，编译器通常能优化掉多余的拷贝
            std::vector<PointData> pts = { { pd.x, pd.y } };

            // 3. 压入队列
            q.push(std::move(pts));
        }

        /**
         * @brief 渲染委托：线段/直线渲染
         * 产出：通过 DDA 算法插值出的 int16 点集
         */
        void Render_Line_Delegate(GeoNode &self, GeometryGraph &graph, const ViewState &view,
                                  oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>> &q) {
            if (!GeoStatus::ok(self.status) || self.parents.size() < 2) return;

            // 获取父点（点已经完成了双轨坐标解算）
            const auto& p1 = graph.get_node_by_id(self.parents[0]);
            const auto& p2 = graph.get_node_by_id(self.parents[1]);

            bool is_segment = !self.result.check_f(ComputedResult::IS_INFINITE);

            // 调用极致优化的 DDA 补点算法
            process_two_point_line(q, p1.result.x_view, p1.result.y_view,
                                   p2.result.x_view, p2.result.y_view,
                                   is_segment, self.id, view);
        }

        /**
         * @brief 内部辅助：编译指定的逻辑通道 (公式解析与 JIT 准备)
         */
        void CompileChannelInternal(GeometryGraph &graph, uint32_t node_id, int channel_idx,
                                   const std::string &infix_expr, std::vector<uint32_t> &out_parents) {
            auto &node = graph.get_node_by_id(node_id);
            auto &channel = node.channels[channel_idx];

            channel.original_infix = infix_expr;

            auto compile_res = CAS::Parser::compile_infix_to_rpn(infix_expr);
            if (!compile_res.success) {
                node.status = GeoStatus::ERR_SYNTAX;
                return;
            }

            uint32_t b_len = static_cast<uint32_t>(compile_res.bytecode.size());
            channel.bytecode_len = b_len;
            channel.bytecode_ptr = new RPNToken[b_len];
            std::memcpy(channel.bytecode_ptr, compile_res.bytecode.data(), b_len * sizeof(RPNToken));

            uint32_t p_len = static_cast<uint32_t>(compile_res.binding_slots.size());
            channel.patch_len = p_len;
            channel.patch_ptr = new RuntimeBindingSlot[p_len];

            for (uint32_t k = 0; k < p_len; ++k) {
                const auto &raw = compile_res.binding_slots[k];
                auto &rt_slot = channel.patch_ptr[k];
                rt_slot.rpn_index = raw.rpn_index;
                rt_slot.func_type = raw.func_type;

                if (raw.type == CAS::Parser::RPNBindingSlot::SlotType::VARIABLE) {
                    uint32_t target_id = graph.GetNodeID(raw.source_name);
                    rt_slot.dependency_ids.push_back(target_id);
                    out_parents.push_back(target_id);
                } else {
                    for (const auto &arg_name : raw.args) {
                        uint32_t target_id = graph.GetNodeID(arg_name);
                        rt_slot.dependency_ids.push_back(target_id);
                        out_parents.push_back(target_id);
                    }
                }
            }
        }

        void SetupNodeBase(GeometryGraph &graph, uint32_t id, const GeoNode::VisualConfig &config,
                           GeoType::Type g_type, SolverFunc s_func, RenderTaskFunc t_func) {
            auto &node = graph.get_node_by_id(id);
            node.config = config;

            if (node.config.name == "BasicObject" || node.config.name.empty()) {
                node.config.name = (GeoType::is_scalar(g_type) && !node.config.is_visible)
                                   ? graph.GenerateInternalName() : graph.GenerateNextName();
            }

            graph.RegisterNodeName(node.config.name, id);
            node.type = g_type;
            node.solver = s_func;
            node.render_task = t_func;
            node.active = true;
            node.result.set_f(ComputedResult::VISIBLE, node.config.is_visible);
            graph.mark_as_seed(id);
        }
    }

    // =========================================================
    // 公开工厂方法
    // =========================================================

    uint32_t AddInternalScalar(GeometryGraph &graph, const std::string &infix_expr, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        std::vector<uint32_t> parents;
        CompileChannelInternal(graph, id, 0, infix_expr, parents);
        SetupNodeBase(graph, id, config, GeoType::SCALAR_INTERNAL, Solver_ScalarRPN, nullptr);
        graph.LinkAndRank(id, parents);
        return id;
    }

    uint32_t AddFreePoint(GeometryGraph &graph, const std::string &x_expr, const std::string &y_expr, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        std::vector<uint32_t> combined_parents;
        CompileChannelInternal(graph, id, 0, x_expr, combined_parents);
        CompileChannelInternal(graph, id, 1, y_expr, combined_parents);

        std::ranges::sort(combined_parents);
        auto [first, last] = std::ranges::unique(combined_parents);
        combined_parents.erase(first, last);

        SetupNodeBase(graph, id, config, GeoType::POINT_FREE, Solver_StandardPoint, Render_Point_Delegate);
        graph.LinkAndRank(id, combined_parents);
        return id;
    }

    uint32_t AddSegment(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        auto &node = graph.get_node_by_id(id);
        if (!graph.is_alive(p1_id) || !graph.is_alive(p2_id)) {
            node.status = GeoStatus::ERR_ID_NOT_FOUND;
        }
        node.result.set_f(ComputedResult::IS_INFINITE, false);
        SetupNodeBase(graph, id, config, GeoType::LINE_SEGMENT, Solver_StandardLine, Render_Line_Delegate);
        graph.LinkAndRank(id, {p1_id, p2_id});
        return id;
    }

    uint32_t AddMidPoint(GeometryGraph &graph, uint32_t p1_id, uint32_t p2_id, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        auto &node = graph.get_node_by_id(id);

        if (!graph.is_alive(p1_id) || !graph.is_alive(p2_id)) {
            node.status = GeoStatus::ERR_ID_NOT_FOUND;
        }
        SetupNodeBase(graph, id, config, GeoType::POINT_MID, Solver_Midpoint, Render_Point_Delegate);
        graph.LinkAndRank(id, {p1_id, p2_id});
        return id;
    }

    uint32_t AddConstrainedPoint(GeometryGraph &graph, uint32_t target_id, const std::string &x_expr, const std::string &y_expr, const GeoNode::VisualConfig &config) {
        uint32_t id = graph.allocate_node();
        auto &node = graph.get_node_by_id(id);
        node.result.i0 = static_cast<int32_t>(target_id);

        std::vector<uint32_t> combined_parents = { target_id };
        CompileChannelInternal(graph, id, 0, x_expr, combined_parents);
        CompileChannelInternal(graph, id, 1, y_expr, combined_parents);

        std::ranges::sort(combined_parents);
        auto [first, last] = std::ranges::unique(combined_parents);
        combined_parents.erase(first, last);

        SetupNodeBase(graph, id, config, GeoType::POINT_CONSTRAINED, Solver_ConstrainedPoint, Render_Point_Delegate);
        node.result.set_f(ComputedResult::IS_HEURISTIC, true);
        graph.LinkAndRank(id, combined_parents);
        return id;
    }

    void DeleteObjectRecursive(GeometryGraph &graph, uint32_t target_id) {
        if (!graph.is_alive(target_id)) return;
        std::vector<uint32_t> targets;
        CollectDescendants(graph, target_id, targets);
        std::ranges::sort(targets);
        targets.erase(std::ranges::unique(targets).begin(), targets.end());

        for (uint32_t id : targets) {
            if (!graph.is_alive(id)) continue;
            auto &node = graph.get_node_by_id(id);
            for (int i = 0; i < 4; ++i) node.channels[i].clear();
            for (uint32_t pid : node.parents) {
                if (graph.is_alive(pid)) {
                    auto &p_kids = graph.get_node_by_id(pid).children;
                    std::erase(p_kids, id);
                }
            }
            graph.physical_delete(id);
        }
    }

    void InternalUpdateScalar(GeometryGraph &graph, uint32_t scalar_id, const std::string &new_infix) {
        auto &node = graph.get_node_by_id(scalar_id);
        node.channels[0].clear();
        std::vector<uint32_t> new_parents;
        CompileChannelInternal(graph, scalar_id, 0, new_infix, new_parents);
        if (node.status == GeoStatus::ERR_SYNTAX || node.status == GeoStatus::ERR_ID_NOT_FOUND) {
            node.status = GeoStatus::VALID;
        }
        graph.LinkAndRank(scalar_id, new_parents);
        graph.mark_as_seed(scalar_id);
    }

    void UpdatePointScalar(GeometryGraph &graph, uint32_t point_id, const std::string &new_x_expr, const std::string &new_y_expr) {
        if (!graph.is_alive(point_id)) return;
        auto &node = graph.get_node_by_id(point_id);
        if (!GeoType::is_point(node.type)) return;

        node.channels[0].clear();
        node.channels[1].clear();
        std::vector<uint32_t> combined_parents;
        CompileChannelInternal(graph, point_id, 0, new_x_expr, combined_parents);
        CompileChannelInternal(graph, point_id, 1, new_y_expr, combined_parents);

        std::ranges::sort(combined_parents);
        auto [first, last] = std::ranges::unique(combined_parents);
        combined_parents.erase(first, last);
        if (node.status == GeoStatus::ERR_SYNTAX || node.status == GeoStatus::ERR_ID_NOT_FOUND) {
            node.status = GeoStatus::VALID;
        }

        graph.LinkAndRank(point_id, combined_parents);
        graph.mark_as_seed(point_id);
    }

    /**
     * @brief 极致优化的视口同步逻辑
     * 严格遵循 $M=32767$ 的推导公式
     */
    void RefreshViewState(GeometryGraph& graph) {
        graph.view.Refresh(); // 调用 ViewState 内部的高效计算逻辑
    }

    void UpdateViewTransform(GeometryGraph& graph, double ox, double oy, double zoom) {
        graph.view.offset_x = ox; graph.view.offset_y = oy; graph.view.zoom = zoom;
        RefreshViewState(graph);
    }

    void UpdateViewSize(GeometryGraph& graph, double w, double h) {
        graph.view.screen_width = w; graph.view.screen_height = h;
        RefreshViewState(graph);
    }

} // namespace GeoFactory