// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
    /**
     * @brief 快速获取指定父节点结果引用
     */
    FORCE_INLINE const ComputedResult& get_parent_res(const GeometryGraph& graph, uint32_t pid) {
        return graph.get_node_by_id(pid).result;
    }

    /**
     * @brief 数学结果安全性检查
     */
    FORCE_INLINE bool validate_math(GeoNode& self, double val) {
        if (!std::isfinite(val)) {
            self.status = GeoStatus::ERR_OVERFLOW;
            return false;
        }
        return true;
    }

    /**
     * @brief [核心辅助] 解算单个逻辑通道
     * 变量引用永远读取父节点的物理世界坐标
     */
    double SolveChannel(GeoNode& self, int idx, const GeometryGraph& graph) {
        auto& ch = self.channels[idx];
        if (ch.bytecode_len == 0) return ch.value;

        for (uint32_t k = 0; k < ch.patch_len; ++k) {
            auto& p = ch.patch_ptr[k];
            const auto& parent_res = get_parent_res(graph, p.dependency_ids[0]);
            double val = 0.0;

            switch (p.func_type) {
                case CAS::Parser::CustomFunctionType::NONE:
                    val = parent_res.s0;
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_X:
                    val = parent_res.x;
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_Y:
                    val = parent_res.y;
                    break;
                case CAS::Parser::CustomFunctionType::LENGTH: {
                    const auto& r2 = get_parent_res(graph, p.dependency_ids[1]);
                    val = std::hypot(parent_res.x - r2.x, parent_res.y - r2.y);
                    break;
                }
                default: break;
            }
            ch.bytecode_ptr[p.rpn_index].type = RPNTokenType::PUSH_CONST;
            ch.bytecode_ptr[p.rpn_index].value = val;
        }

        ch.value = evaluate_rpn<double>(ch.bytecode_ptr, ch.bytecode_len);
        return ch.value;
    }
}

// =========================================================
// 1. RPN 通用解算器 (标量)
// =========================================================
void Solver_ScalarRPN(GeoNode& self, GeometryGraph& graph) {
    auto& res = self.result;
    for (int i = 0; i < 4; ++i) {
        if (self.channels[i].bytecode_len == 0 && i > 0) continue;
        double abs_val = SolveChannel(self, i, graph);
        if (!validate_math(self, abs_val)) return;
        res._raw_data[i] = abs_val;
    }
    self.status = GeoStatus::VALID;
}

// =========================================================
// 2. 标准点求解器 (计算世界坐标 + 视口相对坐标)
// =========================================================
void Solver_StandardPoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    double abs_x = SolveChannel(self, 0, graph);
    double abs_y = SolveChannel(self, 1, graph);

    if (std::isfinite(abs_x) && std::isfinite(abs_y)) {
        self.result.x = abs_x;
        self.result.y = abs_y;

        // 维护视口相对坐标 (Floating Origin)
        self.result.x_view = abs_x - v.offset_x;
        self.result.y_view = abs_y - v.offset_y;
        self.status = GeoStatus::VALID;
    } else {
        self.status = GeoStatus::ERR_OVERFLOW;
    }
}

// =========================================================
// 3. 中点求解器
// =========================================================
void Solver_Midpoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    double mx = (p1.x + p2.x) * 0.5;
    double my = (p1.y + p2.y) * 0.5;

    if (std::isfinite(mx) && std::isfinite(my)) {
        self.result.x = mx;
        self.result.y = my;
        self.result.x_view = mx - v.offset_x;
        self.result.y_view = my - v.offset_y;
        self.status = GeoStatus::VALID;
    } else {
        self.status = GeoStatus::ERR_OVERFLOW;
    }
}

// =========================================================
// 4. 约束点求解器 (极致优化的 int16 空间吸附搜索)
// =========================================================
void Solver_ConstrainedPoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    const uint32_t target_id = static_cast<uint32_t>(self.result.i0);
    const auto& target = graph.get_node_by_id(target_id);

    if (target.current_point_count == 0) {
        self.status = GeoStatus::ERR_EMPTY_RESULT;
        return;
    }

    // 1. 解算锚点(鼠标位置或公式位置)的世界坐标
    double anchor_w_x = SolveChannel(self, 0, graph);
    double anchor_w_y = SolveChannel(self, 1, graph);

    // 2. 将锚点投影到 int16 压缩 Clip 空间
    Vec2i anchor_clip = v.WorldToClip(anchor_w_x, anchor_w_y);

    // 3. 在 Ring Buffer 中搜索最近的 int16 采样点
    int32_t min_dist_sq = std::numeric_limits<int32_t>::max();
    int16_t best_cx = anchor_clip.x;
    int16_t best_cy = anchor_clip.y;

    const auto& pts = graph.final_points_buffer; // std::vector<PointData> (int16_t x, y)
    uint32_t start = target.buffer_offset;
    uint32_t end = start + target.current_point_count;

    for (uint32_t i = start; i < end; ++i) {
        const auto& pt = pts[i];

        // 跳过垃圾数据点
        if (pt.x == graph.view.MAGIC_CLIP_X) {
            continue;
        }

        // 整数减法
        int32_t dx = static_cast<int32_t>(pt.x) - anchor_clip.x;
        int32_t dy = static_cast<int32_t>(pt.y) - anchor_clip.y;
        // 整数平方累加，防止 float 转换开销
        int32_t d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_cx = pt.x;
            best_cy = pt.y;
        }
    }

    // 4. 💡 逆向投影：利用 ViewState 成员函数从 int16 还原回 double
    // 这确保了吸附点的位置与 WebGPU 渲染出来的像素位置完全重合
    Vec2 best_world = v.ClipToWorld(best_cx, best_cy);

    self.result.x = best_world.x;
    self.result.y = best_world.y;
    self.result.x_view = best_world.x - v.offset_x;
    self.result.y_view = best_world.y - v.offset_y;

    self.status = GeoStatus::VALID;
}

// =========================================================
// 5. 标准线段求解器
// =========================================================
void Solver_StandardLine(GeoNode& self, GeometryGraph& graph) {
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    self.result.x1 = p1.x; self.result.y1 = p1.y;
    self.result.x2 = p2.x; self.result.y2 = p2.y;

    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    if ((dx * dx + dy * dy) < 1e-15) {
        self.status = GeoStatus::ERR_EMPTY_RESULT;
    } else {
        self.status = GeoStatus::VALID;
    }
}