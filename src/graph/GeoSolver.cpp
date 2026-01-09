// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include <algorithm>
#include <cmath>
#include "../../include/functions/lerp.h"
#include "../../pch.h"
// =========================================================
// [内部辅助] 数值提取器
// =========================================================
double ExtractValue(const GeoNode &parent, RPNBinding::Property prop) {
    // 无论 parent 是什么几何对象，直接从统一的结果区取值
    switch (prop) {
        case RPNBinding::POS_X: return parent.result.x;
        case RPNBinding::POS_Y: return parent.result.y;
        case RPNBinding::VALUE: return parent.result.scalar;
        default: return 0.0;
    }
}
bool is_heuristic_solver_local(SolverFunc s) {
    // 凡是需要回读 Buffer 采样点来确定坐标的求解器
    return (s == Solver_ConstrainedPoint || s == Solver_IntersectionPoint || s == Solver_LabelAnchorPoint);
}


void Solver_ScalarRPN(GeoNode &self, const std::vector<GeoNode> &pool) {
    // 1. 获取逻辑配方（RPN 指令）
    auto &d = std::get<Data_Scalar>(self.data);

    // 2. 动态绑定最新值（从父节点的 result 区拉取）
    for (const auto &bind: d.bindings) {
        const auto &parent = pool[self.parents[bind.parent_index]];
        // 使用新版 ExtractValue，非常快
        d.tokens[bind.token_index].value = ExtractValue(parent, bind.prop);
    }

    // 3. 计算并将结果存入统一结果区，不再覆盖 self.data
    self.result.scalar = evaluate_rpn<double>(d.tokens);
    self.result.is_valid = true;
}

// 这是一个通用的辅助模板，用于 std::visit
template<class... Ts>
struct overload : Ts... {
    using Ts::operator()...;
};

// C++17 需要这个推导指引，C++20 以后通常可以省略，但建议加上以保证兼容性
template<class... Ts>
overload(Ts...) -> overload<Ts...>;

std::optional<LineCoords> ExtractLineCoords(const GeoNode &node, const std::vector<GeoNode> &pool) {
    // 使用访问者模式处理线段的两种存储形态
    return std::visit(overload{
                          [&](const Data_Line &d) -> std::optional<LineCoords> {
                              // 通过引用的点 ID，直接去结果区提值
                              const auto &p1 = pool[d.p1_id];
                              const auto &p2 = pool[d.p2_id];
                              return LineCoords{p1.result.x, p1.result.y, p2.result.x, p2.result.y};
                          },
                          [&](const Data_CalculatedLine &d) -> std::optional<LineCoords> {
                              // 直接返回计算好的坐标
                              return LineCoords{d.x1, d.y1, d.x2, d.y2};
                          },
                          [](const auto &) -> std::optional<LineCoords> {
                              return std::nullopt; // 不是线类型
                          }
                      }, node.data);
}

void Solver_Measure_Length(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    // 直接从父节点的 result 区提取，O(1) 速度
    double x1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_X);
    double y1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_Y);
    double x2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_X);
    double y2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_Y);

    double dist = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));

    // 写入统一结果区
    self.result.scalar = dist;
    self.result.is_valid = true;

    // 同步到逻辑 Payload (可选，用于兼容旧的 get_if 逻辑)
    if (auto d = std::get_if<Data_Scalar>(&self.data)) {
        d->value = dist;
    }
}

// =========================================================
// 2. 角度测量求解器 (重构版)
// =========================================================
void Solver_Measure_Angle(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 3) return;

    double xa = ExtractValue(pool[self.parents[0]], RPNBinding::POS_X);
    double ya = ExtractValue(pool[self.parents[0]], RPNBinding::POS_Y);
    double xb = ExtractValue(pool[self.parents[1]], RPNBinding::POS_X);
    double yb = ExtractValue(pool[self.parents[1]], RPNBinding::POS_Y);
    double xc = ExtractValue(pool[self.parents[2]], RPNBinding::POS_X);
    double yc = ExtractValue(pool[self.parents[2]], RPNBinding::POS_Y);

    double angle = std::atan2(yc - yb, xc - xb) - std::atan2(ya - yb, xa - xb);
    while (angle < 0) angle += 2.0 * M_PI;

    // 写入统一结果区
    self.result.scalar = angle;
    self.result.is_valid = true;

    if (auto d = std::get_if<Data_Scalar>(&self.data)) {
        d->value = angle;
    }
}

// =========================================================
// 3. 面积测量求解器 (重构版)
// =========================================================
void Solver_Measure_Area(GeoNode &self, const std::vector<GeoNode> &pool) {
    double area = 0.0;
    size_t n = self.parents.size();
    if (n < 3) return;

    for (size_t i = 0; i < n; ++i) {
        const auto &p_curr = pool[self.parents[i]];
        const auto &p_next = pool[self.parents[(i + 1) % n]];

        double x1 = ExtractValue(p_curr, RPNBinding::POS_X);
        double y1 = ExtractValue(p_curr, RPNBinding::POS_Y);
        double x2 = ExtractValue(p_next, RPNBinding::POS_X);
        double y2 = ExtractValue(p_next, RPNBinding::POS_Y);

        area += (x1 * y2 - x2 * y1);
    }

    double final_area = std::abs(area) * 0.5;

    // 写入统一结果区
    self.result.scalar = final_area;
    self.result.is_valid = true;

    if (auto d = std::get_if<Data_Scalar>(&self.data)) {
        d->value = final_area;
    }
}

// =========================================================
// 4. 中点求解器 (重构版)
// =========================================================
void Solver_Midpoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    double x1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_X);
    double y1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_Y);
    double x2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_X);
    double y2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_Y);

    double mx = (x1 + x2) * 0.5;
    double my = (y1 + y2) * 0.5;

    // 写入统一结果区
    self.result.x = mx;
    self.result.y = my;
    self.result.is_valid = true;

    // 保持 Data_Point 逻辑同步
    self.data = Data_Point{mx, my};
}

void Solver_StandardPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    // 从父节点标量提取最新的逻辑值
    double px = ExtractValue(pool[self.parents[0]], RPNBinding::VALUE);
    double py = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE);

    // 写入统一结果区
    self.result.x = px;
    self.result.y = py;
    self.result.is_valid = true;

    // 同步到逻辑 Payload (Data_Point)
    self.data = Data_Point{px, py};
}

// =========================================================
// 2. 圆求解器 (重构版)
// =========================================================
void Solver_Circle(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    const auto &center_node = pool[self.parents[0]];
    const auto &radius_node = pool[self.parents[1]];

    // 提取圆心坐标和半径标量
    double cx = ExtractValue(center_node, RPNBinding::POS_X);
    double cy = ExtractValue(center_node, RPNBinding::POS_Y);
    double r = ExtractValue(radius_node, RPNBinding::VALUE);

    // 写入统一结果区
    self.result.x = cx;
    self.result.y = cy;
    self.result.scalar = r;
    self.result.is_valid = true;

    // 同步到逻辑 Payload (Data_Circle)
    // 假设 Data_Circle 结构为 {cx, cy, radius}
    self.data = Data_Circle{cx, cy, r};
}

// =========================================================
// 3. 动态单 RPN 求解器 (显函数/隐函数重构版)
// =========================================================
void Solver_DynamicSingleRPN(GeoNode &self, const std::vector<GeoNode> &pool) {
    auto *func = std::get_if<Data_SingleRPN>(&self.data);
    if (!func) return;

    // 热更新 RPN 指令流中的常量值 (从父节点 result 区拉取)
    for (const auto &bind: func->bindings) {
        if (bind.parent_index < self.parents.size()) {
            const auto &parent = pool[self.parents[bind.parent_index]];
            func->tokens[bind.token_index].value = ExtractValue(parent, bind.prop);
        }
    }

    // 函数对象的结果不是单值，而是后续渲染出的点集，
    // 因此这里通常不需要写入 self.result.scalar
}

// =========================================================
// 4. 动态双 RPN 求解器 (参数方程重构版)
// =========================================================
void Solver_DynamicDualRPN(GeoNode &self, const std::vector<GeoNode> &pool) {
    auto *data = std::get_if<Data_DualRPN>(&self.data);
    if (!data) return;

    // 分别更新 X(t) 和 Y(t) 的绑定参数
    for (const auto &bind: data->bindings_x) {
        if (bind.parent_index < self.parents.size()) {
            const auto &parent = pool[self.parents[bind.parent_index]];
            data->tokens_x[bind.token_index].value = ExtractValue(parent, bind.prop);
        }
    }
    for (const auto &bind: data->bindings_y) {
        if (bind.parent_index < self.parents.size()) {
            const auto &parent = pool[self.parents[bind.parent_index]];
            data->tokens_y[bind.token_index].value = ExtractValue(parent, bind.prop);
        }
    }
}

void Solver_PerpendicularFoot(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    // 1. 获取输入：线段对象 + 外部点对象
    const auto &segmentNode = pool[self.parents[0]];
    const auto &p0Node = pool[self.parents[1]];

    // 2. 提取外部点坐标 P0 (直接从 result 区提)
    double p0x = ExtractValue(p0Node, RPNBinding::POS_X);
    double p0y = ExtractValue(p0Node, RPNBinding::POS_Y);

    // 3. 提取线段坐标 (利用 visit 兼容 Data_Line 和 Data_CalculatedLine)
    auto line = ExtractLineCoords(segmentNode, pool);
    if (!line) return;

    // 4. 向量投影计算垂足
    double vx = line->x2 - line->x1;
    double vy = line->y2 - line->y1;
    double v_mag_sq = vx * vx + vy * vy;

    double fx, fy;
    if (v_mag_sq < 1e-12) {
        fx = line->x1;
        fy = line->y1;
    } else {
        double wx_rel = p0x - line->x1;
        double wy_rel = p0y - line->y1;
        double t = (wx_rel * vx + wy_rel * vy) / v_mag_sq;
        fx = line->x1 + t * vx;
        fy = line->y1 + t * vy;
    }

    // 5. 写入统一结果区
    self.result.x = fx;
    self.result.y = fy;
    self.result.is_valid = true;

    // 6. 同步 Payload
    self.data = Data_Point{fx, fy};
}


void Solver_ConstrainedPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 3) return;

    // 1. 获取依赖对象信息
    const GeoNode &target_node = pool[self.parents[0]];
    if (target_node.current_point_count == 0) return;

    // 2. 获取当前的“锚点”坐标 (从父标量节点的 result.scalar 提值)
    double anchor_x = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE);
    double anchor_y = ExtractValue(pool[self.parents[2]], RPNBinding::VALUE);

    // 3. 获取坐标转换参数
    const ViewState &view = g_global_view_state;
    NDCMap ndc_map = BuildNDCMap(view);

    // 4. 将锚点转为 Clip Space 用于 Buffer 对比
    PointData anchor_clip{};
    world_to_clip_store(anchor_clip, anchor_x, anchor_y, ndc_map, 0);

    // 5. 暴力搜索最近点
    size_t start = target_node.buffer_offset;
    size_t end = start + target_node.current_point_count;
    float min_dist_sq = std::numeric_limits<float>::max();
    PointData best_point = anchor_clip;
    bool found = false;

    for (size_t i = start; i < end; ++i) {
        const auto &pt = wasm_final_contiguous_buffer[i];
        float dx = pt.position.x - anchor_clip.position.x;
        float dy = pt.position.y - anchor_clip.position.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_point = pt;
            found = true;
        }
    }

    // 6. 写入结果
    if (found) {
        double wx = ndc_map.center_x + (double) best_point.position.x / ndc_map.scale_x;
        double wy = ndc_map.center_y - (double) best_point.position.y / ndc_map.scale_y;

        self.result.x = wx;
        self.result.y = wy;
        self.result.is_valid = true;
        self.data = Data_Point{wx, wy};
    }
}


void Solver_Tangent(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.empty()) return;

    // 1. 获取约束点
    const GeoNode &cp_node = pool[self.parents[0]];

    // 从 result 提取约束点当前世界坐标
    double cpx = ExtractValue(cp_node, RPNBinding::POS_X);
    double cpy = ExtractValue(cp_node, RPNBinding::POS_Y);

    // 2. 获取目标曲线 (约束点的父亲)
    if (cp_node.parents.empty()) return;
    const GeoNode &curve_node = pool[cp_node.parents[0]];
    if (curve_node.current_point_count < 2) return;

    // 3. 准备 NDC 映射
    const ViewState &view = g_global_view_state;
    NDCMap ndc_map = BuildNDCMap(view);

    PointData cp_clip{};
    world_to_clip_store(cp_clip, cpx, cpy, ndc_map, 0);

    // 4. 寻找最近的两个采样点进行斜率逼近
    size_t start = curve_node.buffer_offset;
    size_t end = start + curve_node.current_point_count;
    if (end > wasm_final_contiguous_buffer.size()) end = wasm_final_contiguous_buffer.size();

    size_t idx1 = start, idx2 = start + 1;
    float dist1 = std::numeric_limits<float>::max();
    float dist2 = std::numeric_limits<float>::max();

    for (size_t i = start; i < end; ++i) {
        const auto &pt = wasm_final_contiguous_buffer[i];
        float dx = pt.position.x - cp_clip.position.x;
        float dy = pt.position.y - cp_clip.position.y;
        float d2 = dx * dx + dy * dy;

        if (d2 < dist1) {
            dist2 = dist1;
            idx2 = idx1;
            dist1 = d2;
            idx1 = i;
        } else if (d2 < dist2) {
            dist2 = d2;
            idx2 = i;
        }
    }

    auto clip_to_world = [&](const PointData &p) -> std::pair<double, double> {
        return {
            ndc_map.center_x + (double) p.position.x / ndc_map.scale_x,
            ndc_map.center_y - (double) p.position.y / ndc_map.scale_y
        };
    };

    auto [wx1, wy1] = clip_to_world(wasm_final_contiguous_buffer[idx1]);
    auto [wx2, wy2] = clip_to_world(wasm_final_contiguous_buffer[idx2]);

    // 5. 写入 Payload (Data_CalculatedLine)
    // 注意：线对象通常不存储单点结果到 result.x/y，但设置 is_valid
    Data_CalculatedLine tangent_data{wx1, wy1, wx2, wy2, true};
    self.data = tangent_data;
    self.result.is_valid = true;
}

void Solver_ParallelPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 2) return;

    const auto &segmentNode = pool[self.parents[0]];
    const auto &throughPointNode = pool[self.parents[1]];

    // 1. 提取参考线坐标 A, B
    // 使用重构后的 ExtractLineCoords，它会自动处理 Data_Line 和 Data_CalculatedLine
    auto line = ExtractLineCoords(segmentNode, pool);
    if (!line) return;

    // 2. 提取通过点坐标 P
    // 使用极简版 ExtractValue，直接从结果区获取坐标
    double px = ExtractValue(throughPointNode, RPNBinding::POS_X);
    double py = ExtractValue(throughPointNode, RPNBinding::POS_Y);

    // 3. 计算平行向量 v = B - A
    double vx = line->x2 - line->x1;
    double vy = line->y2 - line->y1;

    // 4. 计算新参考点世界坐标 P' = P + v
    double res_x = px + vx;
    double res_y = py + vy;

    // 5. 写入统一结果区 (核心)
    self.result.x = res_x;
    self.result.y = res_y;
    self.result.is_valid = true;

    // 6. 同步 Payload 数据 (用于撤销/重做及逻辑一致性)
    self.data = Data_Point{res_x, res_y};
}


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
        for (int i = 0; i < 4; ++i) {
            sum_x[i] = 0;
            sum_y[i] = 0;
            count[i] = 0;
        }
    }
};

struct HashEntry {
    uint32_t pixel_idx;
    int32_t acc_id;
    uint64_t bitmask;
};

static AlignedVector<HashEntry> g_hash_table;
static AlignedVector<uint32_t> g_used_slots;
static std::vector<CellAcc> g_acc_buffer;

inline uint32_t hash_pixel(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// =========================================================
// 图解交点求解器 (重构版 - 保持原始算法逻辑)
// =========================================================
void Solver_IntersectionPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    // 1. 类型与安全检查
    auto *data_ptr = std::get_if<Data_IntersectionPoint>(&self.data);
    if (!data_ptr) return;

    auto &data = *data_ptr;
    size_t n = data.num_targets;
    if (self.parents.size() < n + 2) return;

    const ViewState &v = g_global_view_state;
    const int sw = (int) v.screen_width;
    const int sh = (int) v.screen_height;

    // 2. 初始化全局哈希表（仅在需要时）
    if (g_hash_table.size() != HASH_TABLE_SIZE) {
        g_hash_table.assign(HASH_TABLE_SIZE, {0xFFFFFFFF, -1, 0});
        g_used_slots.reserve(HASH_TABLE_SIZE);
    }

    NDCMap m = BuildNDCMap(v);

    // 3. 提取锚点坐标 (使用重构后的 ExtractValue)
    if (!data.is_found) {
        data.anchor_x = ExtractValue(pool[self.parents[n]], RPNBinding::VALUE);
        data.anchor_y = ExtractValue(pool[self.parents[n + 1]], RPNBinding::VALUE);
    }
    float a_cx = (float) ((data.anchor_x - m.center_x) * m.scale_x);
    float a_cy = -(float) ((data.anchor_y - m.center_y) * m.scale_y);

    // 4. 参数设定：网格分辨率
    const float GRID_SCALE = 4.0f;
    const float half_sw_s = (float) sw * 0.5f * GRID_SCALE;
    const float half_sh_s = (float) sh * 0.5f * GRID_SCALE;
    const uint32_t scaled_width = (uint32_t) (sw * GRID_SCALE);

    // ---------------------------------------------------------
    // Pass 1: 填充哈希表 (建立碰撞标记) - 逻辑保持不变
    // ---------------------------------------------------------
    g_used_slots.clear();
    for (size_t i = 0; i < n; ++i) {
        const GeoNode &t_node = pool[self.parents[i]];
        uint32_t start = t_node.buffer_offset;
        uint32_t count = t_node.current_point_count;

        for (uint32_t j = 0; j < count; ++j) {
            const auto &pt = wasm_final_contiguous_buffer[start + j];
            int px = (int) ((pt.position.x + 1.0f) * half_sw_s);
            int py = (int) ((pt.position.y + 1.0f) * half_sh_s);
            if (px < 0 || px >= (int) scaled_width || py < 0 || py >= (int) (sh * GRID_SCALE)) continue;

            uint32_t pix_idx = (uint32_t) py * scaled_width + px;
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

    // ---------------------------------------------------------
    // Pass 2: 识别碰撞并分配累加器 - 逻辑保持不变
    // ---------------------------------------------------------
    g_acc_buffer.clear();
    for (uint32_t slot: g_used_slots) {
        uint64_t mask = g_hash_table[slot].bitmask;
        if (mask != 0 && (mask & (mask - 1)) != 0) {
            g_hash_table[slot].acc_id = (int32_t) g_acc_buffer.size();
            CellAcc new_acc;
            new_acc.clear();
            g_acc_buffer.push_back(new_acc);
        }
    }

    if (g_acc_buffer.empty()) {
        for (uint32_t slot: g_used_slots) g_hash_table[slot] = {0xFFFFFFFF, -1, 0};
        data.is_found = false;
        self.result.is_valid = false;
        return;
    }

    // ---------------------------------------------------------
    // Pass 3: 亚像素累加 - 逻辑保持不变
    // ---------------------------------------------------------
    for (size_t i = 0; i < n; ++i) {
        const GeoNode &t_node = pool[self.parents[i]];
        uint32_t start = t_node.buffer_offset;
        uint32_t count = t_node.current_point_count;

        for (uint32_t j = 0; j < count; ++j) {
            const auto &pt = wasm_final_contiguous_buffer[start + j];
            int px = (int) ((pt.position.x + 1.0f) * half_sw_s);
            int py = (int) ((pt.position.y + 1.0f) * half_sh_s);
            if (px < 0 || px >= (int) scaled_width || py < 0 || py >= (int) (sh * GRID_SCALE)) continue;

            uint32_t pix_idx = (uint32_t) py * scaled_width + px;
            uint32_t h = hash_pixel(pix_idx) & HASH_MASK;
            while (g_hash_table[h].pixel_idx != pix_idx) {
                if (g_hash_table[h].pixel_idx == 0xFFFFFFFF) break;
                h = (h + 1) & HASH_MASK;
            }

            int32_t aid = g_hash_table[h].acc_id;
            if (aid != -1 && i < 4) {
                g_acc_buffer[aid].sum_x[i] += pt.position.x;
                g_acc_buffer[aid].sum_y[i] += pt.position.y;
                g_acc_buffer[aid].count[i]++;
            }
        }
    }

    // ---------------------------------------------------------
    // Pass 4: 择优策略 - 逻辑保持不变
    // ---------------------------------------------------------
    float b_cx = 0, b_cy = 0;
    float min_internal_d2 = std::numeric_limits<float>::max();
    float min_anchor_d2 = std::numeric_limits<float>::max();
    bool found = false;

    for (const auto &acc: g_acc_buffer) {
        if (acc.count[0] == 0 || acc.count[1] == 0) continue;

        float ax = acc.sum_x[0] / acc.count[0];
        float ay = acc.sum_y[0] / acc.count[0];
        float bx = acc.sum_x[1] / acc.count[1];
        float by = acc.sum_y[1] / acc.count[1];

        float idx = ax - bx;
        float idy = ay - by;
        float internal_d2 = idx * idx + idy * idy;
        float cand_x = (ax + bx) * 0.5f;
        float cand_y = (ay + by) * 0.5f;

        float adx = cand_x - a_cx;
        float ady = cand_y - a_cy;
        float anchor_d2 = adx * adx + ady * ady;

        if (internal_d2 < min_internal_d2 * 1.2f) {
            if (internal_d2 < min_internal_d2 * 0.8f || anchor_d2 < min_anchor_d2) {
                min_internal_d2 = internal_d2;
                min_anchor_d2 = anchor_d2;
                b_cx = cand_x;
                b_cy = cand_y;
                found = true;
            }
        }
    }

    // 现场清理
    for (uint32_t slot: g_used_slots) g_hash_table[slot] = {0xFFFFFFFF, -1, 0};

    // ---------------------------------------------------------
    // 5. 写回结果 (同时写回 Payload 和 统一结果区)
    // ---------------------------------------------------------
    if (found) {
        double final_wx = m.center_x + (double) b_cx / m.scale_x;
        double final_wy = m.center_y - (double) b_cy / m.scale_y;

        // 更新统一结果区 (供下游节点通过 ExtractValue 使用)
        self.result.x = final_wx;
        self.result.y = final_wy;
        self.result.is_valid = true;

        // 同步更新 Payload 内部状态 (保证逻辑一致性)
        data.x = final_wx;
        data.y = final_wy;
        data.anchor_x = final_wx;
        data.anchor_y = final_wy;
        data.is_found = true;
    } else {
        data.is_found = false;
        self.result.is_valid = false;
    }
}

void Solver_AnalyticalIntersection(GeoNode &self, const std::vector<GeoNode> &pool) {
    auto *data_ptr = std::get_if<Data_AnalyticalIntersection>(&self.data);
    if (!data_ptr) return;
    auto &data = *data_ptr;

    const GeoNode &n1 = pool[self.parents[0]];
    const GeoNode &n2 = pool[self.parents[1]];

    // 1. 提取初始猜测点 (用于首次锁定分支符号)
    // 使用统一的 ExtractValue 提取标量结果
    double gx = ExtractValue(pool[self.parents[2]], RPNBinding::VALUE);
    double gy = ExtractValue(pool[self.parents[3]], RPNBinding::VALUE);

    struct Cand {
        double x, y;
        int sign;
    };
    std::vector<Cand> candidates;

    // =========================================================
    // 情况 A: 直线 - 直线
    // =========================================================
    if (n1.render_type == GeoNode::RenderType::Line && n2.render_type == GeoNode::RenderType::Line) {
        auto l1 = ExtractLineCoords(n1, pool);
        auto l2 = ExtractLineCoords(n2, pool);
        if (l1 && l2) {
            double a1 = l1->y1 - l1->y2;
            double b1 = l1->x2 - l1->x1;
            double c1 = l1->x1 * l1->y2 - l1->x2 * l1->y1;
            double a2 = l2->y1 - l2->y2;
            double b2 = l2->x2 - l2->x1;
            double c2 = l2->x1 * l2->y2 - l2->x2 * l2->y1;
            double det = a1 * b2 - a2 * b1;
            if (std::abs(det) > 1e-10) {
                candidates.push_back({(b1 * c2 - b2 * c1) / det, (a2 * c1 - a1 * c2) / det, 0});
            }
        }
    }
    // =========================================================
    // 情况 B: 直线 - 圆
    // =========================================================
    else if (n1.render_type == GeoNode::RenderType::Line || n2.render_type == GeoNode::RenderType::Line) {
        const auto &ln = (n1.render_type == GeoNode::RenderType::Line) ? n1 : n2;
        const auto &ci = (n1.render_type == GeoNode::RenderType::Circle) ? n1 : n2;
        auto l = ExtractLineCoords(ln, pool);

        // 直接从圆节点的 result 区提取圆心和半径
        double cx = ci.result.x;
        double cy = ci.result.y;
        double cr = ci.result.scalar;

        if (l) {
            double dx = l->x2 - l->x1;
            double dy = l->y2 - l->y1;
            double fx = l->x1 - cx;
            double fy = l->y1 - cy;
            double A = dx * dx + dy * dy;
            double B = 2 * (fx * dx + fy * dy);
            double C = fx * fx + fy * fy - cr * cr;
            double delta = B * B - 4 * A * C;

            if (delta >= 0) {
                double sqD = std::sqrt(delta);
                double t1 = (-B + sqD) / (2 * A);
                double t2 = (-B - sqD) / (2 * A);
                candidates.push_back({l->x1 + t1 * dx, l->y1 + t1 * dy, 1});
                candidates.push_back({l->x1 + t2 * dx, l->y1 + t2 * dy, -1});
            }
        }
    }
    // =========================================================
    // 情况 C: 圆 - 圆
    // =========================================================
    else if (n1.render_type == GeoNode::RenderType::Circle && n2.render_type == GeoNode::RenderType::Circle) {
        double c1x = n1.result.x;
        double c1y = n1.result.y;
        double c1r = n1.result.scalar;
        double c2x = n2.result.x;
        double c2y = n2.result.y;
        double c2r = n2.result.scalar;

        double dx = c2x - c1x;
        double dy = c2y - c1y;
        double d2 = dx * dx + dy * dy;
        double d = std::sqrt(d2);

        if (d <= c1r + c2r && d >= std::abs(c1r - c2r) && d > 1e-10) {
            double a = (c1r * c1r - c2r * c2r + d2) / (2 * d);
            double h = std::sqrt(std::max(0.0, c1r * c1r - a * a));
            double x2 = c1x + a * dx / d;
            double y2 = c1y + a * dy / d;
            candidates.push_back({x2 + h * dy / d, y2 - h * dx / d, 1});
            candidates.push_back({x2 - h * dy / d, y2 + h * dx / d, -1});
        }
    }

    // =========================================================
    // 结果处理与统一结果区写入
    // =========================================================
    if (candidates.empty()) {
        data.is_found = false;
        self.result.is_valid = false;
        return;
    }

    // 1. 分支锁定逻辑 (仅在第一次未锁定时根据猜测点确定 sign)
    if (data.branch_sign == 0 && candidates.size() > 1) {
        double min_d2 = std::numeric_limits<double>::max();
        for (const auto &cand: candidates) {
            double d2 = std::pow(cand.x - gx, 2) + std::pow(cand.y - gy, 2);
            if (d2 < min_d2) {
                min_d2 = d2;
                data.branch_sign = cand.sign;
            }
        }
    }

    // 2. 提取最终结果并写入 result 区
    bool branch_matched = false;
    for (const auto &cand: candidates) {
        // 对于唯一解的情况，cand.sign 为 0，也应通过匹配
        if (cand.sign == data.branch_sign || candidates.size() == 1) {
            // 写入统一结果区 (核心变动)
            self.result.x = cand.x;
            self.result.y = cand.y;
            self.result.is_valid = true;

            // 同步 Payload
            data.x = cand.x;
            data.y = cand.y;
            data.is_found = true;
            branch_matched = true;
            break;
        }
    }

    if (!branch_matched) {
        data.is_found = false;
        self.result.is_valid = false;
    }
}

// =========================================================
// 解析约束点求解器 (重构版)
// =========================================================
void Solver_AnalyticalConstrainedPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    auto *data = std::get_if<Data_AnalyticalConstrainedPoint>(&self.data);
    if (!data) return;

    const GeoNode &target = pool[self.parents[0]];

    // 1. 初始化阶段：仅在第一次运行或交互强制重置时，根据猜测点锁定参数 t
    if (!data->is_initialized) {
        // 使用新版 ExtractValue 提取猜测值
        double gx = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE);
        double gy = ExtractValue(pool[self.parents[2]], RPNBinding::VALUE);

        if (target.render_type == GeoNode::RenderType::Line) {
            auto line = ExtractLineCoords(target, pool);
            if (line) {
                double vx = line->x2 - line->x1;
                double vy = line->y2 - line->y1;
                double v_mag_sq = vx * vx + vy * vy;
                if (v_mag_sq > 1e-12) {
                    data->t = ((gx - line->x1) * vx + (gy - line->y1) * vy) / v_mag_sq;
                    // 若是有限线段，限制 t 在 [0, 1]
                    if (auto ld = std::get_if<Data_Line>(&target.data)) {
                        if (!ld->is_infinite) data->t = std::clamp(data->t, 0.0, 1.0);
                    }
                    data->is_initialized = true;
                }
            }
        } else if (target.render_type == GeoNode::RenderType::Circle) {
            // 直接读取圆心结果 (target.result.x/y)
            data->t = std::atan2(gy - target.result.y, gx - target.result.x);
            data->is_initialized = true;
        }
    }

    // 2. 正常运行阶段：根据锁定的 t 随父对象变形
    if (data->is_initialized) {
        double rx = 0.0, ry = 0.0;
        if (target.render_type == GeoNode::RenderType::Line) {
            auto line = ExtractLineCoords(target, pool);
            if (line) {
                rx = line->x1 + data->t * (line->x2 - line->x1);
                ry = line->y1 + data->t * (line->y2 - line->y1);
            }
        } else if (target.render_type == GeoNode::RenderType::Circle) {
            // 读取圆心 (x,y) 和 半径 (scalar) 结果
            rx = target.result.x + target.result.scalar * std::cos(data->t);
            ry = target.result.y + target.result.scalar * std::sin(data->t);
        }

        // --- 写入统一结果区 ---
        self.result.x = rx;
        self.result.y = ry;
        self.result.is_valid = true;

        // 同步 Payload (保持数据一致性)
        data->x = rx;
        data->y = ry;
    }
}


// =========================================================
// 比例分割点求解器 (重构版)
// =========================================================
void Solver_RatioPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 3) return;

    // 提取 P1, P2 的坐标和比例 k
    // 所有的提取动作都直接指向 result 区，速度极快
    double x1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_X);
    double y1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_Y);
    double x2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_X);
    double y2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_Y);
    double k = ExtractValue(pool[self.parents[2]], RPNBinding::VALUE);

    // 计算插值
    double rx = x1 + k * (x2 - x1);
    double ry = y1 + k * (y2 - y1);

    // --- 写入统一结果区 ---
    self.result.x = rx;
    self.result.y = ry;
    self.result.is_valid = true;

    // 同步到 Payload 变体
    self.data = Data_RatioPoint{rx, ry};
}

void Solver_CircleThreePoints(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 3) return;

    // 1. 提取三个点的世界坐标 (直接从 result 区提，极速)
    double x1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_X);
    double y1 = ExtractValue(pool[self.parents[0]], RPNBinding::POS_Y);
    double x2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_X);
    double y2 = ExtractValue(pool[self.parents[1]], RPNBinding::POS_Y);
    double x3 = ExtractValue(pool[self.parents[2]], RPNBinding::POS_X);
    double y3 = ExtractValue(pool[self.parents[2]], RPNBinding::POS_Y);

    // 2. 计算分母 D
    double D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));

    // 3. 检查共线
    if (std::abs(D) < 1e-10) {
        self.result.is_valid = false;
        return;
    }

    // 4. 计算圆心与半径
    double a = x1 * x1 + y1 * y1;
    double b = x2 * x2 + y2 * y2;
    double c = x3 * x3 + y3 * y3;

    double cx = (a * (y2 - y3) + b * (y3 - y1) + c * (y1 - y2)) / D;
    double cy = (a * (x3 - x2) + b * (x1 - x3) + c * (x2 - x1)) / D;
    double r = std::sqrt(std::pow(cx - x1, 2) + std::pow(cy - y1, 2));

    // 5. 写入统一结果区 (核心)
    self.result.x = cx;
    self.result.y = cy;
    self.result.scalar = r;
    self.result.is_valid = true;

    // 6. 同步逻辑 Payload
    self.data = Data_Circle{cx, cy, r};
}


void Solver_LabelAnchorPoint(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.size() < 3) return;

    // 1. 获取几何宿主节点
    const auto &target_node = pool[self.parents[0]];

    // ★ 性能截断：若不显示标签，直接标记无效并退出，不进行后续 $O(N)$ 搜索
    if (!target_node.config.show_label) {
        self.result.is_valid = false;
        return;
    }

    // 2. 检查父对象是否有采样点
    if (target_node.current_point_count == 0) return;

    // 3. 提取锚点(猜测点)的投影坐标
    const ViewState &view = g_global_view_state;
    NDCMap ndc_map = BuildNDCMap(view);

    double anchor_x = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE);
    double anchor_y = ExtractValue(pool[self.parents[2]], RPNBinding::VALUE);

    PointData anchor_clip{};
    world_to_clip_store(anchor_clip, anchor_x, anchor_y, ndc_map, 0);

    // 4. 图解搜索：在父对象的采样 Buffer 中寻找最优点
    size_t start = target_node.buffer_offset;
    size_t end = start + target_node.current_point_count;
    float min_dist_sq = std::numeric_limits<float>::max();
    PointData best_point = anchor_clip;
    bool found = false;

    for (size_t i = start; i < end; ++i) {
        const auto &pt = wasm_final_contiguous_buffer[i];
        float dx = pt.position.x - anchor_clip.position.x;
        float dy = pt.position.y - anchor_clip.position.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_point = pt;
            found = true;
        }
    }

    // 5. 写入结果
    if (found) {
        double wx = ndc_map.center_x + (double) best_point.position.x / ndc_map.scale_x;
        double wy = ndc_map.center_y - (double) best_point.position.y / ndc_map.scale_y;

        self.result.x = wx;
        self.result.y = wy;
        self.result.is_valid = true;

        // 同步 Payload
        self.data = Data_Point{wx, wy};
    }
}

void Solver_TextLabel(GeoNode &self, const std::vector<GeoNode> &pool) {
    if (self.parents.empty()) return;

    // 父母是那个专门负责吸附的辅助锚点
    const auto &anchor_node = pool[self.parents[0]];

    // 如果锚点无效（由于 show_label=false 导致的），自己也标记无效
    if (!anchor_node.result.is_valid) {
        self.result.is_valid = false;
        return;
    }

    // 直接从锚点结果区复制坐标，O(1) 零判断
    double wx = anchor_node.result.x;
    double wy = anchor_node.result.y;

    // 1. 写入统一结果区
    self.result.x = wx;
    self.result.y = wy;
    self.result.is_valid = true;

    // 2. 同步 Payload
    self.data = Data_TextLabel{wx, wy};
}
