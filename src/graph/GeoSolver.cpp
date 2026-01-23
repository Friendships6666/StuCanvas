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
     * @brief 数学结果安全性检查：防止 NaN 和 Infinity 污染图
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
     * 自动处理补丁回填：变量引用永远读取父节点的“世界坐标 (x/y)”槽位
     */
    double SolveChannel(GeoNode& self, int idx, const GeometryGraph& graph) {
        auto& ch = self.channels[idx];
        if (ch.bytecode_len == 0) return ch.value;

        // JIT 补丁回填：从依赖节点中提取“绝对世界坐标”
        for (uint32_t k = 0; k < ch.patch_len; ++k) {
            auto& p = ch.patch_ptr[k];
            const auto& parent_res = get_parent_res(graph, p.dependency_ids[0]);
            double val = 0.0;

            switch (p.func_type) {
                case CAS::Parser::CustomFunctionType::NONE:
                    val = parent_res.s0; // 默认提取第一个标量槽
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_X:
                    val = parent_res.x;  // 💡 永远读取世界坐标
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_Y:
                    val = parent_res.y;  // 💡 永远读取世界坐标
                    break;
                case CAS::Parser::CustomFunctionType::LENGTH: {
                    const auto& r2 = get_parent_res(graph, p.dependency_ids[1]);
                    val = std::hypot(parent_res.x - r2.x, parent_res.y - r2.y);
                    break;
                }
                default: break;
            }
            // 原地降级为常量，回填最新的物理数值
            ch.bytecode_ptr[p.rpn_index].type = RPNTokenType::PUSH_CONST;
            ch.bytecode_ptr[p.rpn_index].value = val;
        }

        // 执行 RPN 虚拟机
        ch.value = evaluate_rpn<double>(ch.bytecode_ptr, ch.bytecode_len);
        return ch.value;
    }
}

// =========================================================
// 1. RPN 通用解算器 (标量/函数)
// =========================================================
void Solver_ScalarRPN(GeoNode& self, GeometryGraph& graph) {
    auto& res = self.result;

    // 遍历 4 个嵌入式逻辑通道进行解算
    for (int i = 0; i < 4; ++i) {
        if (self.channels[i].bytecode_len == 0 && i > 0) continue;

        double abs_val = SolveChannel(self, i, graph);
        if (!validate_math(self, abs_val)) return;

        // 存储到物理槽位 s0-s6
        res._raw_data[i] = abs_val;
    }
    self.status = GeoStatus::VALID;
}

// =========================================================
// 2. 标准点求解器 (双轨坐标：世界 + 视口)
// =========================================================
void Solver_StandardPoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;

    // 1. 解算 X 和 Y 两个通道的世界坐标 (W)
    double abs_x = SolveChannel(self, 0, graph);
    double abs_y = SolveChannel(self, 1, graph);

    if (std::isfinite(abs_x) && std::isfinite(abs_y)) {
        // 2. 存储世界坐标 (Slot 0, 1 -> x, y)
        self.result.x = abs_x;
        self.result.y = abs_y;

        // 3. 💡 浮动原点脱水：计算视口相对坐标 (Slot 4, 5 -> x_view, y_view)
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
    // 获取两个父点（它们已经算好了双轨坐标）
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    // 1. 计算绝对中点
    double mx = (p1.x + p2.x) * 0.5;
    double my = (p1.y + p2.y) * 0.5;

    if (std::isfinite(mx) && std::isfinite(my)) {
        self.result.x = mx;
        self.result.y = my;

        // 2. 计算相对视口坐标
        self.result.x_view = mx - v.offset_x;
        self.result.y_view = my - v.offset_y;

        self.status = GeoStatus::VALID;
    } else {
        self.status = GeoStatus::ERR_OVERFLOW;
    }
}

// =========================================================
// 4. 约束点求解器 (吸附算法 - 修正反向投影)
// =========================================================
void Solver_ConstrainedPoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    const uint32_t target_id = static_cast<uint32_t>(self.result.i0);
    const auto& target = graph.get_node_by_id(target_id);

    if (target.current_point_count == 0) {
        self.status = GeoStatus::ERR_EMPTY_RESULT;
        return;
    }

    // 1. 解算锚点世界坐标
    double anchor_w_x = SolveChannel(self, 0, graph);
    double anchor_w_y = SolveChannel(self, 1, graph);

    // 2. 投影到裁剪空间 (NDC) 参考系
    float clip_anchor_x = static_cast<float>((anchor_w_x - v.offset_x) * v.ndc_scale_x);
    float clip_anchor_y = -static_cast<float>((anchor_w_y - v.offset_y) * v.ndc_scale_y);

    // 3. 在 final_points_buffer 中搜索最近采样点
    float min_dist_sq = std::numeric_limits<float>::max();
    float best_clip_x = clip_anchor_x;
    float best_clip_y = clip_anchor_y;

    const auto& pts = graph.final_points_buffer;
    uint32_t start = target.buffer_offset;
    uint32_t end = start + target.current_point_count;

    for (uint32_t i = start; i < end; ++i) {
        const auto& pt = pts[i];
        float dx = pt.position.x - clip_anchor_x;
        float dy = pt.position.y - clip_anchor_y;
        float d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_clip_x = pt.position.x;
            best_clip_y = pt.position.y;
        }
    }

    // 4. 💡 核心修正：逆向还原相对坐标 (匹配 Render 代理的负号)
    double rel_x = static_cast<double>(best_clip_x) / v.ndc_scale_x;
    double rel_y = static_cast<double>(best_clip_y) / v.ndc_scale_y; // 关键负号

    self.result.x_view = rel_x;
    self.result.y_view = rel_y;
    self.result.x = rel_x + v.offset_x; // 还原世界坐标
    self.result.y = rel_y + v.offset_y;

    self.status = GeoStatus::VALID;
}

// =========================================================
// 5. 标准线段求解器
// =========================================================
void Solver_StandardLine(GeoNode& self, GeometryGraph& graph) {
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    // 快照世界坐标用于后续测量
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