// --- 文件路径: src/graph/GeoFactory.cpp ---

#include "../../include/graph/GeoFactory.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include "../../include/graph/GeoSolver.h"
#include "../../include/plot/plotSegment.h"
#include "../../include/plot/plotCircle.h"
#include <algorithm>

namespace GeoFactory {
    namespace {
        /**
         * @brief 内部辅助：递归收集所有子孙节点 ID
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
         * @brief 渲染委托：点渲染 (应用浮动原点)
         */
        void Render_Point_Delegate(GeoNode &self, GeometryGraph &graph, const NDCMap &m,
                                   oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
            if (!GeoStatus::ok(self.status)) return;

            const auto& v = graph.view;
            PointData pd{};

            // 💡 核心修改：使用 x_view 和 y_view (相对坐标)
            // 此时 dx 和 dy 是已经在 Solver 中减去 offset 的小数字
            double dx = self.result.x_view;
            double dy = self.result.y_view;

            // NDC 坐标 = 相对坐标 * 投影因子
            pd.position.x = static_cast<float>(dx * v.ndc_scale_x);
            pd.position.y = static_cast<float>(dy * v.ndc_scale_y); // Y轴反转
            pd.function_index = self.id;

            q.push({self.id, {pd}});
        }

        /**
         * @brief 渲染委托：线段渲染 (修正版：从父节点提取相对坐标)
         */
        void Render_Line_Delegate(GeoNode &self, GeometryGraph &graph, const NDCMap &m,
                                  oneapi::tbb::concurrent_bounded_queue<FunctionResult> &q) {
            // 1. 基础状态检查
            if (!GeoStatus::ok(self.status)) return;
            if (self.parents.size() < 2) return;

            // 2. 💡 核心修正：通过索引获取两个父点对象
            const auto& p1 = graph.get_node_by_id(self.parents[0]);
            const auto& p2 = graph.get_node_by_id(self.parents[1]);

            // 3. 提取父点中已经在各自 Solver 阶段减去 offset 的相对坐标 (x_view, y_view)
            // 这样即便点在 (99999, 99999)，这里拿到的也是 (0.5, 0.2) 这种高精度小数
            double xv1 = p1.result.x_view;
            double yv1 = p1.result.y_view;
            double xv2 = p2.result.x_view;
            double yv2 = p2.result.y_view;

            const ViewState& v = graph.view;
            bool is_infinite = self.result.check_f(ComputedResult::IS_INFINITE);

            // 4. 计算相对视口的世界原点
            // 因为 xv/yv 是相对于 ViewOffset (屏幕中心) 的，
            // 为了适配裁剪算法，我们需要告诉它屏幕左上角在“以中心为原点”的坐标系中的位置
            Vec2 relative_world_origin = {
                -(v.screen_width * 0.5) * v.wppx,
                -(v.screen_height * 0.5) * v.wppy
            };

            // 5. 调用线段处理函数 (此时传入的坐标均为小数，精度极高)
            process_two_point_line(&q, xv1, yv1, xv2, yv2,
                                   !is_infinite, self.id,
                                   relative_world_origin, // 💡 传入相对参考系的原点
                                   v.wppx, v.wppy,
                                   v.screen_width, v.screen_height, 0, 0, m);
        }

        /**
         * @brief 内部辅助：编译指定的逻辑通道 (嵌入式架构)
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
                    try {
                        uint32_t target_id = graph.GetNodeID(raw.source_name);
                        rt_slot.dependency_ids.push_back(target_id);
                        out_parents.push_back(target_id);
                    } catch (...) {
                        node.status = GeoStatus::ERR_ID_NOT_FOUND;
                    }
                } else {
                    for (const auto &arg_name : raw.args) {
                        try {
                            uint32_t target_id = graph.GetNodeID(arg_name);
                            rt_slot.dependency_ids.push_back(target_id);
                            out_parents.push_back(target_id);
                        } catch (...) {
                            node.status = GeoStatus::ERR_ID_NOT_FOUND;
                        }
                    }
                }
            }
        }

        void SetupNodeBase(GeometryGraph &graph, uint32_t id, const GeoNode::VisualConfig &config,
                           GeoType::Type g_type, SolverFunc s_func, RenderTaskFunc t_func) {
            auto &node = graph.get_node_by_id(id);
            node.config = config;

            if (node.config.name == "BasicObject" || node.config.name.empty()) {
                if (GeoType::is_scalar(g_type) && !node.config.is_visible) {
                    node.config.name = graph.GenerateInternalName();
                } else {
                    node.config.name = graph.GenerateNextName();
                }
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
    // 公开工厂方法 (不创建子标量节点)
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

        // 绑定通用点解算器，它负责填充 world_x 和 x_view
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

            // 💡 清理嵌入式 LogicChannel 的所有堆内存
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

    void RefreshViewState(GeometryGraph& graph) {
        auto& v = graph.view;
        double aspect = v.screen_width / v.screen_height;

        v.wppx = (2.0 * aspect) / (v.zoom * v.screen_width);
        v.wppy = -2.0 / (v.zoom * v.screen_height);

        v.world_origin.x = v.offset_x - (v.screen_width * 0.5) * v.wppx;
        v.world_origin.y = v.offset_y - (v.screen_height * 0.5) * v.wppy;

        // 💡 刷新 NDC 投影因子
        v.ndc_scale_x = 2.0 / (v.screen_width * v.wppx);
        v.ndc_scale_y = 2.0 / (v.screen_height * std::abs(v.wppy));
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