// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoGraph.h"
#include <cmath>
#include <limits>

namespace {
    /**
     * @brief 极致性能预检：利用传入的实时 LUT 检查父节点有效性
     * 这保证了失效状态（VALID=false）能顺着 Rank 链条向下传播
     */
    FORCE_INLINE bool are_parents_valid(const GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut) {
        for (uint32_t pid : self.parents) {
            // 通过传入的映射表实时寻址
            if (!pool[lut[pid]].result.check_f(ComputedResult::VALID)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 快速获取指定父节点结果引用 (基于传入的 LUT)
     */
    FORCE_INLINE const ComputedResult& get_parent_res(const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, uint32_t pid) {
        return pool[lut[pid]].result;
    }
}

// =========================================================
// 1. RPN 标量求解器 (补丁填充 + 数值计算)
// =========================================================
void Solver_ScalarRPN(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    auto& res = self.result;
    if (!res.bytecode_ptr) return;

    // 1. 级联失效预检
    if (!are_parents_valid(self, pool, lut)) {
        res.set_f(ComputedResult::VALID, false);
        return;
    }

    // 2. JIT 补丁回填：将父节点最新数据写入指令流
    for (uint32_t i = 0; i < res.patch_len; ++i) {
        auto& p = res.patch_ptr[i];
        double val = 0.0;

        if (p.func_type == CAS::Parser::CustomFunctionType::NONE) {
            // 变量路径：从父节点的 s0 槽位提数
            val = get_parent_res(pool, lut, p.dependency_ids[0]).s0;
        } else {
            // 黑箱函数路径：例如 Length(A, B)
            if (p.func_type == CAS::Parser::CustomFunctionType::LENGTH) {
                const auto& r1 = get_parent_res(pool, lut, p.dependency_ids[0]);
                const auto& r2 = get_parent_res(pool, lut, p.dependency_ids[1]);
                val = std::hypot(r1.x - r2.x, r1.y - r2.y);
            }
            // 后续可在此扩展 EXTRACT_X/Y, AREA 等
        }
        // 原地覆盖指令中的立即数
        res.bytecode_ptr[p.rpn_index].value = val;
    }

    // 3. 执行纯数学虚拟机指令
    res.s0 = evaluate_rpn<double>(res.bytecode_ptr, res.bytecode_len);

    // 4. 数学自检：处理 NaN 或 Infinity
    res.set_f(ComputedResult::VALID, std::isfinite(res.s0));
}

// =========================================================
// 2. 标准点求解器 (由 X, Y 标量合成)
// =========================================================
void Solver_StandardPoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    const auto& res_x = get_parent_res(pool, lut, self.parents[0]);
    const auto& res_y = get_parent_res(pool, lut, self.parents[1]);

    if (res_x.check_f(ComputedResult::VALID) && res_y.check_f(ComputedResult::VALID)) {
        self.result.x = res_x.s0;
        self.result.y = res_y.s0;
        self.result.set_f(ComputedResult::VALID, true);
    } else {
        self.result.set_f(ComputedResult::VALID, false);
    }
}

// =========================================================
// 3. 中点求解器
// =========================================================
void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    const auto& p1 = get_parent_res(pool, lut, self.parents[0]);
    const auto& p2 = get_parent_res(pool, lut, self.parents[1]);

    if (p1.check_f(ComputedResult::VALID) && p2.check_f(ComputedResult::VALID)) {
        self.result.x = (p1.x + p2.x) * 0.5;
        self.result.y = (p1.y + p2.y) * 0.5;
        self.result.set_f(ComputedResult::VALID, true);
    } else {
        self.result.set_f(ComputedResult::VALID, false);
    }
}

// =========================================================
// 4. 约束点求解器 (图解吸附：Heuristic)
// =========================================================
void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    // 预检：父母（目标曲线、锚点X、锚点Y）必须全部有效
    if (!are_parents_valid(self, pool, lut)) {
        self.result.set_f(ComputedResult::VALID, false);
        return;
    }

    // target_id 存储在 i0 中
    const auto& target = pool[lut[static_cast<uint32_t>(self.result.i0)]];
    const auto& anchor_x = get_parent_res(pool, lut, self.parents[1]);
    const auto& anchor_y = get_parent_res(pool, lut, self.parents[2]);

    // 如果目标对象本帧没有采样数据（例如无效的隐函数），无法吸附
    if (target.current_point_count == 0) {
        self.result.set_f(ComputedResult::VALID, false);
        return;
    }

    // 构建 NDC 映射用于像素空间比对（利用参数 view）
    NDCMap ndc_map = BuildNDCMap(view);

    double min_dist_sq = std::numeric_limits<double>::max();
    double best_x = 0, best_y = 0;

    uint32_t start = target.buffer_offset;
    uint32_t end = start + target.current_point_count;

    // 在父对象的采样 Buffer 中执行暴力搜索 (Hot Path)
    for (uint32_t i = start; i < end; ++i) {
        const auto& pt = wasm_final_contiguous_buffer[i];

        // 此处对比逻辑：计算采样点与锚点的欧氏距离平方
        double dx = (double)pt.position.x - anchor_x.s0;
        double dy = (double)pt.position.y - anchor_y.s0;
        double d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_x = (double)pt.position.x;
            best_y = (double)pt.position.y;
        }
    }

    // 更新结果坐标
    self.result.x = best_x;
    self.result.y = best_y;
    self.result.set_f(ComputedResult::VALID, true);
}

// =========================================================
// 5. 标准线段/直线求解器
// =========================================================
void Solver_StandardLine(GeoNode& self, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    auto& res = self.result;

    const auto& p1_res = get_parent_res(pool, lut, self.parents[0]);
    const auto& p2_res = get_parent_res(pool, lut, self.parents[1]);

    // 级联失效判定
    if (!p1_res.check_f(ComputedResult::VALID) || !p2_res.check_f(ComputedResult::VALID)) {
        res.set_f(ComputedResult::VALID, false);
        return;
    }

    // 坐标快照到线段自己的结果槽，方便 RenderTask 直接读取
    res.x1 = p1_res.x; res.y1 = p1_res.y;
    res.x2 = p2_res.x; res.y2 = p2_res.y;

    // 自检：重合点判定
    double dx = res.x1 - res.x2;
    double dy = res.y1 - res.y2;
    res.set_f(ComputedResult::VALID, (dx * dx + dy * dy) > 1e-15);
}