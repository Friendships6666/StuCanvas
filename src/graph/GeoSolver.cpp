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
void Solver_ScalarRPN(GeoNode& self, std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
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
void Solver_StandardPoint(GeoNode& self, std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
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
void Solver_Midpoint(GeoNode& self,std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
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


void Solver_ConstrainedPoint(GeoNode& self, std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
    // 1. 预检
    if (!are_parents_valid(self, pool, lut)) {
        self.result.set_f(ComputedResult::VALID, false);
        return;
    }

    // 2. 获取依赖项引用
    const uint32_t target_id = static_cast<uint32_t>(self.result.i0);
    const auto& target = pool[lut[target_id]];
    auto& ax_node = pool[lut[self.parents[1]]];
    auto& ay_node = pool[lut[self.parents[2]]];

    if (target.current_point_count == 0) {
        self.result.set_f(ComputedResult::VALID, false);
        return;
    }

    // 3. 准备空间转换参数 (Double 精度)
    NDCMap m = BuildNDCMap(view);

    // =========================================================
    // 4. 空间转换：世界坐标 $W_a$ -> 裁剪空间 $C_a$ (Float)
    // =========================================================
    // 从父标量中取出存储的世界坐标锚点
    double world_anchor_x = ax_node.result.s0;
    double world_anchor_y = ay_node.result.s0;

    // 转换为 Float 精度，以便与 wasm_final_contiguous_buffer 里的 PointData 对齐
    float clip_anchor_x = static_cast<float>((world_anchor_x - m.center_x) * m.scale_x);
    float clip_anchor_y = -static_cast<float>((world_anchor_y - m.center_y) * m.scale_y);

    // =========================================================
    // 5. 裁剪空间搜索：寻找离当前显示最近的像素点
    // =========================================================
    float min_dist_sq = std::numeric_limits<float>::max();
    float best_clip_x = clip_anchor_x;
    float best_clip_y = clip_anchor_y;

    uint32_t start = target.buffer_offset;
    uint32_t end = start + target.current_point_count;

    for (uint32_t i = start; i < end; ++i) {
        const auto& pt = wasm_final_contiguous_buffer[i];

        // 在 Float 精度下的裁剪空间进行欧氏距离比对
        float dx = pt.position.x - clip_anchor_x;
        float dy = pt.position.y - clip_anchor_y;
        float d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_clip_x = pt.position.x;
            best_clip_y = pt.position.y;
        }
    }

    // =========================================================
    // 6. 逆向转换：最佳裁剪点 $C_{best}$ -> 新世界坐标 $W_{new}$
    // =========================================================
    // 利用当前视图参数，将选中的那个像素点还原为真实的数学坐标
    double world_new_x = m.center_x + static_cast<double>(best_clip_x) / m.scale_x;
    double world_new_y = m.center_y - static_cast<double>(best_clip_y) / m.scale_y;

    // =========================================================
    // 7. 同步与回馈 (Ping-Pong)
    // =========================================================
    // A. 更新本节点的当前坐标（用于渲染投影）
    self.result.x = world_new_x;
    self.result.y = world_new_y;
    self.result.set_f(ComputedResult::VALID, true);

    // B. 写回父节点：更新“世界坐标锚点”
    // 这样下次 Solver 运行时，会从这个“上一次最接近点”的世界坐标开始重新投影和搜索
    ax_node.result.s0 = world_new_x;
    ay_node.result.s0 = world_new_y;
}

// =========================================================
// 5. 标准线段/直线求解器
// =========================================================
void Solver_StandardLine(GeoNode& self, std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view) {
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