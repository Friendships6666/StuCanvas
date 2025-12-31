// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include <algorithm>
#include <cmath>
#include "../../include/functions/lerp.h"
#include "../../pch.h"
// =========================================================
// [内部辅助] 数值提取器
// =========================================================
double ExtractValue(const GeoNode& parent, RPNBinding::Property prop, const std::vector<GeoNode>& pool) {
    if (std::holds_alternative<Data_Scalar>(parent.data)) {
        return std::get<Data_Scalar>(parent.data).value;
    }
    // 处理普通点
    if (std::holds_alternative<Data_Point>(parent.data)) {
        const auto& p = std::get<Data_Point>(parent.data);
        if (prop == RPNBinding::POS_X) return p.x;
        if (prop == RPNBinding::POS_Y) return p.y;
    }
    // ★ 新增：处理交点
    if (std::holds_alternative<Data_IntersectionPoint>(parent.data)) {
        const auto& p = std::get<Data_IntersectionPoint>(parent.data);
        if (prop == RPNBinding::POS_X) return p.x;
        if (prop == RPNBinding::POS_Y) return p.y;
    }
    if (std::holds_alternative<Data_AnalyticalIntersection>(parent.data)) {
        const auto& p = std::get<Data_AnalyticalIntersection>(parent.data);
        if (prop == RPNBinding::POS_X) return p.x;
        if (prop == RPNBinding::POS_Y) return p.y;
    }
    return 0.0;
}
struct LineCoords { double x1, y1, x2, y2; };
void Solver_ScalarRPN(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& d = std::get<Data_Scalar>(self.data);
    for (const auto& bind : d.bindings) {
        // 从依赖节点拉取最新的 value
        d.tokens[bind.token_index].value = std::get<Data_Scalar>(pool[self.parents[bind.parent_index]].data).value;
    }
    // 计算并存入 value，供下游节点使用
    self.data = Data_Scalar{ evaluate_rpn<double>(d.tokens), d.type, d.tokens, d.bindings };
}


std::optional<LineCoords> ExtractLineCoords(const GeoNode& node, const std::vector<GeoNode>& pool) {
    if (std::holds_alternative<Data_Line>(node.data)) {
        const auto& d = std::get<Data_Line>(node.data);

        double x1 = ExtractValue(pool[d.p1_id], RPNBinding::POS_X, pool);
        double y1 = ExtractValue(pool[d.p1_id], RPNBinding::POS_Y, pool);
        double x2 = ExtractValue(pool[d.p2_id], RPNBinding::POS_X, pool);
        double y2 = ExtractValue(pool[d.p2_id], RPNBinding::POS_Y, pool);
        return LineCoords{x1, y1, x2, y2};
    }
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
    auto& d = std::get<Data_Point>(self.data);

    // parents[0] 是 x 标量, parents[1] 是 y 标量
    d.x = ExtractValue(pool[self.parents[0]], RPNBinding::VALUE, pool);
    d.y = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE, pool);
}

// --- 文件路径: src/graph/GeoSolver.cpp ---

void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& d = std::get<Data_Circle>(self.data);

    // 1. 使用 ExtractValue 提取圆心坐标 (无论父节点是普通点还是交点)
    const GeoNode& center_node = pool[self.parents[0]];
    d.cx = ExtractValue(center_node, RPNBinding::POS_X, pool);
    d.cy = ExtractValue(center_node, RPNBinding::POS_Y, pool);

    // 2. 使用 ExtractValue 提取半径标量
    const GeoNode& radius_node = pool[self.parents[1]];
    d.radius = ExtractValue(radius_node, RPNBinding::VALUE, pool);
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


// --- 文件路径: src/graph/GeoSolver.cpp ---

void Solver_ConstrainedPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (self.parents.size() < 3) return;

    // 1. 获取依赖对象信息
    uint32_t target_id = self.parents[0];
    const GeoNode& target_node = pool[target_id];

    // 如果父对象没生成点，则无法吸附，保持不动
    if (target_node.current_point_count == 0) return;

    // 2. 获取当前的“锚点”坐标（由 RPN 标量计算出）
    // 即使是静态值 3.0，也会在上一层 Rank 被算好存入 Data_Scalar::value
    double anchor_x = std::get<Data_Scalar>(pool[self.parents[1]].data).value;
    double anchor_y = std::get<Data_Scalar>(pool[self.parents[2]].data).value;

    // 3. 获取视图与坐标转换参数 (读取 pch.h 中的全局变量)
    const ViewState& view = g_global_view_state;
    NDCMap ndc_map = BuildNDCMap(view); // 假设你在 GeoSolver 内可见此辅助函数

    // 4. 将锚点转为 Clip Space 方便与 Buffer 数据对比
    PointData anchor_clip{};
    world_to_clip_store(anchor_clip, anchor_x, anchor_y, ndc_map, 0);

    // 5. 暴力搜索最近点
    size_t start = target_node.buffer_offset;
    size_t end = start + target_node.current_point_count;

    float min_dist_sq = std::numeric_limits<float>::max();
    PointData best_point = anchor_clip;
    bool found = false;

    for (size_t i = start; i < end; ++i) {
        const auto& pt = wasm_final_contiguous_buffer[i];
        float dx = pt.position.x - anchor_clip.position.x;
        float dy = pt.position.y - anchor_clip.position.y;
        float d2 = dx * dx + dy * dy;

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_point = pt;
            found = true;
        }
    }

    // 6. 如果找到，反转回世界坐标并更新 Data_Point 缓存
    if (found) {
        double wx = ndc_map.center_x + static_cast<double>(best_point.position.x) / ndc_map.scale_x;
        double wy = ndc_map.center_y - static_cast<double>(best_point.position.y) / ndc_map.scale_y;

        self.data = Data_Point{ wx, wy };
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


// --- 文件路径: src/graph/GeoSolver.cpp ---

#include <vector>
#include <limits>
#include <cstdint>
#include <cmath>
#include <algorithm>

// =========================================================
// 极致优化：分对象高精度空间哈希
// =========================================================
constexpr uint32_t HASH_TABLE_SIZE = 131072;
constexpr uint32_t HASH_MASK = HASH_TABLE_SIZE - 1;

// 每个像素内支持最多 4 个对象的分开统计
struct CellAcc {
    float sum_x[4];
    float sum_y[4];
    uint32_t count[4];

    void clear() {
        for(int i=0; i<4; ++i) { sum_x[i] = 0; sum_y[i] = 0; count[i] = 0; }
    }
};

struct HashEntry {
    uint32_t pixel_idx;
    int32_t  acc_id;
    uint64_t bitmask;
};

static AlignedVector<HashEntry> g_hash_table;
static AlignedVector<uint32_t>  g_used_slots;
static std::vector<CellAcc> g_acc_buffer;

inline uint32_t hash_pixel(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

void Solver_IntersectionPoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    if (!std::holds_alternative<Data_IntersectionPoint>(self.data)) return;
    auto& data = std::get<Data_IntersectionPoint>(self.data);
    size_t n = data.num_targets; // 参与对象数
    if (self.parents.size() < n + 2) return;

    const ViewState& v = g_global_view_state;
    const int sw = (int)v.screen_width;
    const int sh = (int)v.screen_height;

    // 1. 初始化
    if (g_hash_table.size() != HASH_TABLE_SIZE) {
        g_hash_table.resize(HASH_TABLE_SIZE, {0xFFFFFFFF, -1, 0});
        g_used_slots.reserve(HASH_TABLE_SIZE);
    }

    NDCMap m = BuildNDCMap(v);
    if (!data.is_found) {
        data.anchor_x = std::get<Data_Scalar>(pool[self.parents[n]].data).value;
        data.anchor_y = std::get<Data_Scalar>(pool[self.parents[n + 1]].data).value;
    }
    float a_cx = (float)((data.anchor_x - m.center_x) * m.scale_x);
    float a_cy = -(float)((data.anchor_y - m.center_y) * m.scale_y);

    // 2. 参数设定：提升网格分辨率到 0.25 像素
    const float GRID_SCALE = 4.0f;
    const float half_sw_s = (float)sw * 0.5f * GRID_SCALE;
    const float half_sh_s = (float)sh * 0.5f * GRID_SCALE;
    const uint32_t scaled_width = (uint32_t)(sw * GRID_SCALE);

    // 3. Pass 1: 填充哈希表 (建立 0.25 像素级的碰撞标记)
    g_used_slots.clear();
    for (size_t i = 0; i < n; ++i) {
        const GeoNode& t_node = pool[self.parents[i]];
        uint32_t start = t_node.buffer_offset;
        uint32_t count = t_node.current_point_count;

        for (uint32_t j = 0; j < count; ++j) {
            const auto& pt = wasm_final_contiguous_buffer[start + j];
            int px = (int)((pt.position.x + 1.0f) * half_sw_s);
            int py = (int)((pt.position.y + 1.0f) * half_sh_s);
            if (px < 0 || px >= (int)scaled_width || py < 0 || py >= (int)(sh * GRID_SCALE)) continue;

            uint32_t pix_idx = (uint32_t)py * scaled_width + px;
            uint32_t h = hash_pixel(pix_idx) & HASH_MASK;

            while (g_hash_table[h].pixel_idx != 0xFFFFFFFF && g_hash_table[h].pixel_idx != pix_idx) {
                h = (h + 1) & HASH_MASK;
            }

            if (g_hash_table[h].pixel_idx == 0xFFFFFFFF) {
                g_hash_table[h].pixel_idx = pix_idx;
                g_used_slots.push_back(h);
            }
            g_hash_table[h].bitmask |= (1ULL << i);
        }
    }

    // 4. Pass 2: 识别碰撞并分配独立对象累加器
    g_acc_buffer.clear();
    for (uint32_t slot : g_used_slots) {
        uint64_t mask = g_hash_table[slot].bitmask;
        // 判定：是否有至少两个对象重叠
        if (mask != 0 && (mask & (mask - 1)) != 0) {
            g_hash_table[slot].acc_id = (int32_t)g_acc_buffer.size();
            CellAcc new_acc;
            new_acc.clear();
            g_acc_buffer.push_back(new_acc);
        }
    }

    if (g_acc_buffer.empty()) {
        for (uint32_t slot : g_used_slots) g_hash_table[slot] = {0xFFFFFFFF, -1, 0};
        data.is_found = false;
        return;
    }

    // 5. Pass 3: 亚像素累加 (按对象分开累加)
    for (size_t i = 0; i < n; ++i) {
        const GeoNode& t_node = pool[self.parents[i]];
        uint32_t start = t_node.buffer_offset;
        uint32_t count = t_node.current_point_count;

        for (uint32_t j = 0; j < count; ++j) {
            const auto& pt = wasm_final_contiguous_buffer[start + j];
            int px = (int)((pt.position.x + 1.0f) * half_sw_s);
            int py = (int)((pt.position.y + 1.0f) * half_sh_s);
            if (px < 0 || px >= (int)scaled_width || py < 0 || py >= (int)(sh * GRID_SCALE)) continue;

            uint32_t pix_idx = (uint32_t)py * scaled_width + px;
            uint32_t h = hash_pixel(pix_idx) & HASH_MASK;
            while (g_hash_table[h].pixel_idx != pix_idx) {
                if (g_hash_table[h].pixel_idx == 0xFFFFFFFF) break;
                h = (h + 1) & HASH_MASK;
            }

            int32_t aid = g_hash_table[h].acc_id;
            if (aid != -1 && i < 4) { // 仅处理前 4 个求交对象
                g_acc_buffer[aid].sum_x[i] += pt.position.x;
                g_acc_buffer[aid].sum_y[i] += pt.position.y;
                g_acc_buffer[aid].count[i]++;
            }
        }
    }

    // 6. Pass 4: 寻找重叠质量最高的候选点
    float b_cx = 0, b_cy = 0;
    float min_internal_d2 = std::numeric_limits<float>::max();
    float min_anchor_d2 = std::numeric_limits<float>::max();
    bool found = false;

    for (const auto& acc : g_acc_buffer) {
        // 只检查对象 0 和 1 的交叠质量（支持多对象时可扩展）
        if (acc.count[0] == 0 || acc.count[1] == 0) continue;

        // 计算两个对象在该像素内的各自质心
        float ax = acc.sum_x[0] / acc.count[0];
        float ay = acc.sum_y[0] / acc.count[0];
        float bx = acc.sum_x[1] / acc.count[1];
        float by = acc.sum_y[1] / acc.count[1];

        // 质量评估：两个质心贴得越近，说明交点越真实
        float idx = ax - bx; float idy = ay - by;
        float internal_d2 = idx*idx + idy*idy;

        // 像素候选坐标（取两对象均值的中心）
        float cand_x = (ax + bx) * 0.5f;
        float cand_y = (ay + by) * 0.5f;

        // 与锚点的距离
        float adx = cand_x - a_cx; float ady = cand_y - a_cy;
        float anchor_d2 = adx*adx + ady*ady;

        // 择优策略：优先匹配内部距离更小的（即相交得更准的），其次匹配离锚点近的
        // 允许 1.2 倍的紧密度容差，在此范围内优先看锚点距离
        if (internal_d2 < min_internal_d2 * 1.2f) {
            if (internal_d2 < min_internal_d2 * 0.8f || anchor_d2 < min_anchor_d2) {
                min_internal_d2 = internal_d2;
                min_anchor_d2 = anchor_d2;
                b_cx = cand_x; b_cy = cand_y;
                found = true;
            }
        }
    }

    // 7. 现场清理
    for (uint32_t slot : g_used_slots) g_hash_table[slot] = {0xFFFFFFFF, -1, 0};

    // 8. 写回结果
    if (found) {
        data.x = m.center_x + (double)b_cx / m.scale_x;
        data.y = m.center_y - (double)b_cy / m.scale_y;
        data.anchor_x = data.x; data.anchor_y = data.y;
        data.is_found = true;
    } else {
        data.is_found = false;
    }
}

void Solver_AnalyticalIntersection(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& data = std::get<Data_AnalyticalIntersection>(self.data);
    const GeoNode& n1 = pool[self.parents[0]];
    const GeoNode& n2 = pool[self.parents[1]];

    // 获取初始猜测点（用于首次锁定分支）
    double gx = std::get<Data_Scalar>(pool[self.parents[2]].data).value;
    double gy = std::get<Data_Scalar>(pool[self.parents[3]].data).value;

    // 存储解析出的候选点
    struct Cand { double x, y; int sign; };
    std::vector<Cand> candidates;

    // =========================================================
    // 情况 A: 直线 - 直线 (解线性方程组)
    // =========================================================
    if (n1.render_type == GeoNode::RenderType::Line && n2.render_type == GeoNode::RenderType::Line) {
        auto l1 = ExtractLineCoords(n1, pool);
        auto l2 = ExtractLineCoords(n2, pool);
        if (l1 && l2) {
            double a1 = l1->y1 - l1->y2; double b1 = l1->x2 - l1->x1; double c1 = l1->x1 * l1->y2 - l1->x2 * l1->y1;
            double a2 = l2->y1 - l2->y2; double b2 = l2->x2 - l2->x1; double c2 = l2->x1 * l2->y2 - l2->x2 * l2->y1;
            double det = a1 * b2 - a2 * b1;
            if (std::abs(det) > 1e-10) {
                candidates.push_back({ (b1 * c2 - b2 * c1) / det, (a2 * c1 - a1 * c2) / det, 0 });
            }
        }
    }
    // =========================================================
    // 情况 B: 直线 - 圆 (解二次方程)
    // =========================================================
    else if (n1.render_type == GeoNode::RenderType::Line || n2.render_type == GeoNode::RenderType::Line) {
        const auto& ln = (n1.render_type == GeoNode::RenderType::Line) ? n1 : n2;
        const auto& ci = (n1.render_type == GeoNode::RenderType::Circle) ? n1 : n2;
        auto l = ExtractLineCoords(ln, pool);
        const auto& c = std::get<Data_Circle>(ci.data);

        if (l) {
            double dx = l->x2 - l->x1; double dy = l->y2 - l->y1;
            double fx = l->x1 - c.cx; double fy = l->y1 - c.cy;
            double A = dx * dx + dy * dy;
            double B = 2 * (fx * dx + fy * dy);
            double C = fx * fx + fy * fy - c.radius * c.radius;
            double delta = B * B - 4 * A * C;

            if (delta >= 0) {
                double sqD = std::sqrt(delta);
                double t1 = (-B + sqD) / (2 * A);
                double t2 = (-B - sqD) / (2 * A);
                candidates.push_back({ l->x1 + t1 * dx, l->y1 + t1 * dy, 1 });
                candidates.push_back({ l->x1 + t2 * dx, l->y1 + t2 * dy, -1 });
            }
        }
    }
    // =========================================================
    // 情况 C: 圆 - 圆 (解相交弦方程)
    // =========================================================
    else if (n1.render_type == GeoNode::RenderType::Circle && n2.render_type == GeoNode::RenderType::Circle) {
        const auto& c1 = std::get<Data_Circle>(n1.data);
        const auto& c2 = std::get<Data_Circle>(n2.data);
        double dx = c2.cx - c1.cx; double dy = c2.cy - c1.cy;
        double d2 = dx * dx + dy * dy;
        double d = std::sqrt(d2);

        if (d <= c1.radius + c2.radius && d >= std::abs(c1.radius - c2.radius) && d > 1e-10) {
            double a = (c1.radius * c1.radius - c2.radius * c2.radius + d2) / (2 * d);
            double h = std::sqrt(std::max(0.0, c1.radius * c1.radius - a * a));
            double x2 = c1.cx + a * dx / d;
            double y2 = c1.cy + a * dy / d;
            // 两个分支解
            candidates.push_back({ x2 + h * dy / d, y2 - h * dx / d, 1 });
            candidates.push_back({ x2 - h * dy / d, y2 + h * dx / d, -1 });
        }
    }

    // =========================================================
    // 分支锁定与结果输出
    // =========================================================
    if (candidates.empty()) {
        data.is_found = false;
        return;
    }

    // 1. 如果尚未锁定分支，根据距离初始猜测点最近的一个来锁定 sign
    if (data.branch_sign == 0) {
        double min_d2 = std::numeric_limits<double>::max();
        for (const auto& cand : candidates) {
            double d2 = std::pow(cand.x - gx, 2) + std::pow(cand.y - gy, 2);
            if (d2 < min_d2) {
                min_d2 = d2;
                data.branch_sign = cand.sign;
            }
        }
        // 如果是唯一解 (如线线交点)，sign 仍为 0
    }

    // 2. 根据锁定的 branch_sign 提取结果
    bool branch_matched = false;
    for (const auto& cand : candidates) {
        if (cand.sign == data.branch_sign) {
            data.x = cand.x;
            data.y = cand.y;
            data.is_found = true;
            branch_matched = true;
            break;
        }
    }

    if (!branch_matched) data.is_found = false;
}