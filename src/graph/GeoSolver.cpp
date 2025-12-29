// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include <algorithm>
#include <cmath>
#include "../../include/functions/lerp.h"
#include "../../pch.h"
// =========================================================
// [内部辅助] 数值提取器
// =========================================================
static double ExtractValue(const GeoNode& parent, RPNBinding::Property prop, const std::vector<GeoNode>& pool) {
    if (std::holds_alternative<Data_Scalar>(parent.data)) {
        return std::get<Data_Scalar>(parent.data).value;
    }
    if (std::holds_alternative<Data_Point>(parent.data)) {
        const auto& p = std::get<Data_Point>(parent.data);
        if (prop == RPNBinding::POS_X) return p.x;
        if (prop == RPNBinding::POS_Y) return p.y;
    }
    return 0.0;
}
struct LineCoords { double x1, y1, x2, y2; };

static std::optional<LineCoords> ExtractLineCoords(const GeoNode& node, const std::vector<GeoNode>& pool) {
    // 情况 A: 普通线 (依赖两个端点 ID)
    if (std::holds_alternative<Data_Line>(node.data)) {
        const auto& d = std::get<Data_Line>(node.data);
        if (d.p1_id >= pool.size() || d.p2_id >= pool.size()) return std::nullopt;

        // 必须确保依赖的点已经计算出坐标
        if (std::holds_alternative<Data_Point>(pool[d.p1_id].data) &&
            std::holds_alternative<Data_Point>(pool[d.p2_id].data)) {
            const auto& p1 = std::get<Data_Point>(pool[d.p1_id].data);
            const auto& p2 = std::get<Data_Point>(pool[d.p2_id].data);
            return LineCoords{p1.x, p1.y, p2.x, p2.y};
            }
    }
    // 情况 B: 计算线 (直接存储坐标，如切线)
    else if (std::holds_alternative<Data_CalculatedLine>(node.data)) {
        const auto& d = std::get<Data_CalculatedLine>(node.data);
        return LineCoords{d.x1, d.y1, d.x2, d.y2};
    }
    return std::nullopt;
}

// =========================================================
// 求解器具体实现
// =========================================================

void Solver_Measure_Length(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.size() < 2) return;
    const auto& p1 = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& p2 = std::get<Data_Point>(pool[self.parents[1]].data);
    std::get<Data_Scalar>(self.data).value = std::sqrt(std::pow(p1.x - p2.x, 2) + std::pow(p1.y - p2.y, 2));
}

void Solver_Measure_Angle(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.size() < 3) return;
    const auto& pA = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& pB = std::get<Data_Point>(pool[self.parents[1]].data); // 顶点
    const auto& pC = std::get<Data_Point>(pool[self.parents[2]].data);
    double a = std::atan2(pC.y - pB.y, pC.x - pB.x) - std::atan2(pA.y - pB.y, pA.x - pB.x);
    while (a < 0) a += 2.0 * M_PI;
    std::get<Data_Scalar>(self.data).value = a;
}

void Solver_Measure_Area(GeoNode& self, const std::vector<GeoNode>& pool) {
    double area = 0.0;
    size_t n = self.parents.size();
    if (n < 3) return;
    for (size_t i = 0; i < n; ++i) {
        const auto& p1 = std::get<Data_Point>(pool[self.parents[i]].data);
        const auto& p2 = std::get<Data_Point>(pool[self.parents[(i + 1) % n]].data);
        area += (p1.x * p2.y - p2.x * p1.y);
    }
    std::get<Data_Scalar>(self.data).value = std::abs(area) * 0.5;
}

void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.size() < 2) return;
    const auto& p1 = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& p2 = std::get<Data_Point>(pool[self.parents[1]].data);
    self.data = Data_Point{ (p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5 };
}

void Solver_StandardPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (!std::holds_alternative<Data_Point>(self.data)) return;
    auto& d = std::get<Data_Point>(self.data);

    // 绑定 X
    if (d.bind_index_x.has_value()) {
        uint32_t p_idx = d.bind_index_x.value();
        if (p_idx < self.parents.size()) {
            d.x = ExtractValue(pool[self.parents[p_idx]], RPNBinding::VALUE, pool);
        }
    }

    // 绑定 Y
    if (d.bind_index_y.has_value()) {
        uint32_t p_idx = d.bind_index_y.value();
        if (p_idx < self.parents.size()) {
            d.y = ExtractValue(pool[self.parents[p_idx]], RPNBinding::VALUE, pool);
        }
    }
}

// =========================================================
// Solver: 圆 (Circle)
// =========================================================
void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.empty()) return;
    auto& d = std::get<Data_Circle>(self.data);

    // 1. 获取圆心坐标 (parents[0])
    uint32_t center_id = self.parents[0];
    if (std::holds_alternative<Data_Point>(pool[center_id].data)) {
        const auto& p = std::get<Data_Point>(pool[center_id].data);
        d.cx = p.x;
        d.cy = p.y;
    }

    // 2. 获取半径 (如果有绑定)
    if (d.bind_index_radius.has_value()) {
        uint32_t p_idx = d.bind_index_radius.value();
        if (p_idx < self.parents.size()) {
            d.radius = ExtractValue(pool[self.parents[p_idx]], RPNBinding::VALUE, pool);
        }
    }
}


void Solver_DynamicSingleRPN(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& func = std::get<Data_SingleRPN>(self.data);
    for (const auto& bind : func.bindings) {
        if (bind.parent_index < self.parents.size()) {
            double val = ExtractValue(pool[self.parents[bind.parent_index]], bind.prop, pool);
            if (bind.token_index < func.tokens.size()) func.tokens[bind.token_index].value = val;
        }
    }
}

void Solver_DynamicDualRPN(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& data = std::get<Data_DualRPN>(self.data);
    for (const auto& bind : data.bindings_x) {
        if (bind.parent_index < self.parents.size()) {
            double val = ExtractValue(pool[self.parents[bind.parent_index]], bind.prop, pool);
            if (bind.token_index < data.tokens_x.size()) data.tokens_x[bind.token_index].value = val;
        }
    }
    for (const auto& bind : data.bindings_y) {
        if (bind.parent_index < self.parents.size()) {
            double val = ExtractValue(pool[self.parents[bind.parent_index]], bind.prop, pool);
            if (bind.token_index < data.tokens_y.size()) data.tokens_y[bind.token_index].value = val;
        }
    }
}

void Solver_PerpendicularFoot(GeoNode& self, const std::vector<GeoNode>& pool) {


    if (self.parents.size() < 2) return;

    // 1. 获取输入：线段对象 + 外部点对象
    const auto& segmentNode = pool[self.parents[0]];
    const auto& sourcePointNode = pool[self.parents[1]];

    // 2. 提取外部点坐标 P0
    if (!std::holds_alternative<Data_Point>(sourcePointNode.data)) return;
    const auto& p0 = std::get<Data_Point>(sourcePointNode.data);

    // 3. 提取线段坐标 P1, P2 (支持普通线和切线)
    auto line = ExtractLineCoords(segmentNode, pool);
    if (!line) return;

    // 4. 向量投影计算垂足
    double vx = line->x2 - line->x1;
    double vy = line->y2 - line->y1;
    double v_mag_sq = vx * vx + vy * vy;

    if (v_mag_sq < 1e-12) {
        self.data = Data_Point{ line->x1, line->y1 };
        return;
    }

    double wx = p0.x - line->x1;
    double wy = p0.y - line->y1;

    double t = (wx * vx + wy * vy) / v_mag_sq;

    Data_Point foot;
    foot.x = line->x1 + t * vx;
    foot.y = line->y1 + t * vy;

    self.data = foot;
}


void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    // 1. 安全检查

    if (self.parents.empty()) return;
    if (!std::holds_alternative<Data_Point>(self.data)) return;


    // 2. 获取目标对象 (父节点)
    uint32_t target_id = self.parents[0];
    const GeoNode& target_node = pool[target_id];

    // ★★★ 核心逻辑：直接检查 Buffer 是否有点 ★★★
    // 如果上一帧没有生成点（count=0），或者还没开始渲染，则无法吸附，保持不动
    if (target_node.current_point_count == 0) return;

    // 3. 准备坐标转换参数
    // 直接读取 pch.h 中的全局变量 g_global_view_state
    const auto& view = g_global_view_state;

    // 根据全局 View 计算 NDCMap (用于 World <-> Clip 转换)
    // 逻辑需与 plotCall.cpp 中保持一致
    double half_w = view.screen_width * 0.5;
    double half_h = view.screen_height * 0.5;

    // 手动计算 Center World (参考 lerp.h 的 screen_to_world)
    double center_world_x = view.world_origin.x + half_w * view.wppx;
    double center_world_y = view.world_origin.y + half_h * view.wppy;

    NDCMap ndc_map;
    ndc_map.center_x = center_world_x;
    ndc_map.center_y = center_world_y;
    ndc_map.scale_x = 2.0 / (view.screen_width * view.wppx);
    ndc_map.scale_y = 2.0 / (view.screen_height * view.wppy);

    // 4. 将自己当前的世界坐标 (Double) 转换为 Clip Space (Float)
    // 这样才能和 Buffer 里的 float 数据进行距离比较
    Data_Point& self_pos = std::get<Data_Point>(self.data);
    PointData self_clip;
    world_to_clip_store(self_clip, self_pos.x, self_pos.y, ndc_map, self.id);

    // 5. ★★★ 暴力搜索：主动拉取 Buffer 数据 ★★★
    // 利用 target_node 记录的偏移量，直接在全局 wasm_final_contiguous_buffer 中遍历
    size_t start_idx = target_node.buffer_offset;
    size_t end_idx = start_idx + target_node.current_point_count;

    // 越界保护
    if (end_idx > wasm_final_contiguous_buffer.size()) {
        end_idx = wasm_final_contiguous_buffer.size();
    }

    float min_dist_sq = std::numeric_limits<float>::max();
    PointData best_point = self_clip; // 如果没找到更好的，就保持不动（但这不可能，因为 count > 0）
    bool found_valid = false;

    // 遍历显存中的点
    for (size_t i = start_idx; i < end_idx; ++i) {
        const PointData& pt = wasm_final_contiguous_buffer[i];

        // 计算 Clip Space 下的欧几里得距离平方
        float dx = pt.position.x - self_clip.position.x;
        float dy = pt.position.y - self_clip.position.y;
        float d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_point = pt;
            found_valid = true;
        }
    }

    // 6. 如果找到了最近点，逆变换回 World Space 更新自己
    if (found_valid) {
        // 逆变换公式: World = Center + NDC / Scale
        // 注意 Y 轴：渲染时 Y 是翻转的 (screen y 向下)，所以逆变换要处理符号
        // world_to_clip_store 中: out.y = -dy * scale_y
        // 所以: dy = -out.y / scale_y => world_y = center_y + dy

        double ndc_x = static_cast<double>(best_point.position.x);
        double ndc_y = static_cast<double>(best_point.position.y);

        double new_world_x = ndc_map.center_x + ndc_x / ndc_map.scale_x;
        double new_world_y = ndc_map.center_y - ndc_y / ndc_map.scale_y;

        self_pos.x = new_world_x;
        self_pos.y = new_world_y;
    }
}
void Solver_Tangent(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.empty()) return;

    // 1. 获取约束点 (Constrained Point)
    uint32_t cp_id = self.parents[0];
    const GeoNode& cp_node = pool[cp_id];

    // 约束点本身必须有坐标
    if (!std::holds_alternative<Data_Point>(cp_node.data)) return;
    const auto& cp_pos = std::get<Data_Point>(cp_node.data);

    // 2. 获取目标曲线 (约束点的父亲)
    if (cp_node.parents.empty()) return;
    uint32_t curve_id = cp_node.parents[0];
    const GeoNode& curve_node = pool[curve_id];

    // 检查曲线是否有渲染数据 (至少2个点才能定直线)
    if (curve_node.current_point_count < 2) return;

    // 3. 准备坐标转换 (World -> Clip)
    // 我们需要在 Clip Space 找最近点，因为 Buffer 里存的是 Clip 坐标
    const ViewState& view = g_global_view_state;

    NDCMap ndc_map;
    // 构建 NDC Map (同 Solver_ConstrainedPoint)
    double cx = view.world_origin.x + view.screen_width * view.wppx * 0.5;
    double cy = view.world_origin.y + view.screen_height * view.wppy * 0.5;
    ndc_map.center_x = cx; ndc_map.center_y = cy;
    ndc_map.scale_x = 2.0 / (view.screen_width * view.wppx);
    ndc_map.scale_y = 2.0 / (view.screen_height * view.wppy);

    // 将约束点转为 Clip 坐标作为搜索基准
    PointData cp_clip;
    world_to_clip_store(cp_clip, cp_pos.x, cp_pos.y, ndc_map, 0);

    // 4. ★★★ 核心逻辑：找到距离约束点最近的“两个”点 ★★★
    size_t start = curve_node.buffer_offset;
    size_t end = start + curve_node.current_point_count;
    // 安全钳制
    if (end > wasm_final_contiguous_buffer.size()) end = wasm_final_contiguous_buffer.size();

    // 维护两个最近点的索引和距离
    size_t idx1 = start, idx2 = start + 1;
    float dist1 = std::numeric_limits<float>::max(); // 最近
    float dist2 = std::numeric_limits<float>::max(); // 次近

    for (size_t i = start; i < end; ++i) {
        const auto& pt = wasm_final_contiguous_buffer[i];
        float dx = pt.position.x - cp_clip.position.x;
        float dy = pt.position.y - cp_clip.position.y;
        float d2 = dx * dx + dy * dy;

        if (d2 < dist1) {
            // 原第一名降级为第二名
            dist2 = dist1;
            idx2 = idx1;
            // 更新第一名
            dist1 = d2;
            idx1 = i;
        } else if (d2 < dist2) {
            // 更新第二名
            dist2 = d2;
            idx2 = i;
        }
    }

    // 5. 将这两个点逆变换回 World Space
    // 辅助 lambda: Clip -> World
    auto clip_to_world = [&](const PointData& p) -> std::pair<double, double> {
        double nx = static_cast<double>(p.position.x);
        double ny = static_cast<double>(p.position.y);
        double wx = ndc_map.center_x + nx / ndc_map.scale_x;
        // 注意 Y 轴反转：渲染时 y = -dy * scale，所以逆变换 dy = -y / scale
        double wy = ndc_map.center_y - ny / ndc_map.scale_y;
        return {wx, wy};
    };

    auto [wx1, wy1] = clip_to_world(wasm_final_contiguous_buffer[idx1]);
    auto [wx2, wy2] = clip_to_world(wasm_final_contiguous_buffer[idx2]);

    // 6. 防止两点重合 (距离过近会导致方向计算错误)
    // 如果两点几乎重合(例如约束点正好卡在顶点上，且Buffer里有重复点)，
    // 我们可以尝试取 idx1 的前一个或后一个点作为替代。


    // 7. 直接设置端点，传输给 Segment 绘制
    Data_CalculatedLine tangent_data{};
    tangent_data.x1 = wx1;
    tangent_data.y1 = wy1;
    tangent_data.x2 = wx2;
    tangent_data.y2 = wy2;
    tangent_data.is_infinite = true; // 切线默认是无限长的

    self.data = tangent_data;
}

void Solver_ParallelPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.size() < 2) return;

    const auto& segmentNode = pool[self.parents[0]];
    const auto& throughPointNode = pool[self.parents[1]];

    // 1. 提取参考线坐标 A, B
    auto line = ExtractLineCoords(segmentNode, pool);
    if (!line) return;

    // 2. 提取通过点坐标 P
    if (!std::holds_alternative<Data_Point>(throughPointNode.data)) return;
    const auto& pP = std::get<Data_Point>(throughPointNode.data);

    // 3. 计算平行向量 v = B - A
    double vx = line->x2 - line->x1;
    double vy = line->y2 - line->y1;

    // 4. 计算新参考点 P' = P + v
    Data_Point p_prime;
    p_prime.x = pP.x + vx;
    p_prime.y = pP.y + vy;

    self.data = p_prime;
}