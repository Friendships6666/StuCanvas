// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// =========================================================
// [Section 1] 核心辅助：极速数值提取器
// =========================================================

/**
 * @brief 从父节点提取一个 double 值。
 * 现在的架构下，大部分函数依赖都会直接命中 Data_Scalar。
 */
inline double ExtractValue(const GeoNode& parent, RPNBinding::Property prop) {
    // 1. 优先检查标量节点 (Length, Angle, Area, Slider)
    if (std::holds_alternative<Data_Scalar>(parent.data)) {
        return std::get<Data_Scalar>(parent.data).value;
    }

    // 2. 备选：从点对象提取 X 或 Y 分量
    if (std::holds_alternative<Data_Point>(parent.data)) {
        const auto& p = std::get<Data_Point>(parent.data);
        return (prop == RPNBinding::POS_X) ? p.x : p.y;
    }

    return 0.0;
}

// =========================================================
// [Section 2] 度量求解器 (Measurement Solvers)
// 将几何关系转化为 Data_Scalar 的数值
// =========================================================

// --- 1. 距离/长度求解器 (依赖: 2个点) ---
void Solver_Measure_Length(GeoNode& self, const std::vector<GeoNode>& pool) {
    const auto& p1 = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& p2 = std::get<Data_Point>(pool[self.parents[1]].data);
    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    std::get<Data_Scalar>(self.data).value = std::sqrt(dx * dx + dy * dy);
}

// --- 2. 角度求解器 (依赖: 3个点 A, B(顶点), C) ---
void Solver_Measure_Angle(GeoNode& self, const std::vector<GeoNode>& pool) {
    const auto& pA = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& pB = std::get<Data_Point>(pool[self.parents[1]].data);
    const auto& pC = std::get<Data_Point>(pool[self.parents[2]].data);

    double a1 = std::atan2(pA.y - pB.y, pA.x - pB.x);
    double a2 = std::atan2(pC.y - pB.y, pC.x - pB.x);
    double angle = a2 - a1;

    while (angle < 0) angle += 2.0 * M_PI;
    std::get<Data_Scalar>(self.data).value = angle;
}

// --- 3. 面积求解器 (依赖: N个点，使用鞋带公式) ---
void Solver_Measure_Area(GeoNode& self, const std::vector<GeoNode>& pool) {
    double area = 0.0;
    size_t n = self.parents.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& p1 = std::get<Data_Point>(pool[self.parents[i]].data);
        const auto& p2 = std::get<Data_Point>(pool[self.parents[(i + 1) % n]].data);
        area += (p1.x * p2.y - p2.x * p1.y);
    }
    std::get<Data_Scalar>(self.data).value = std::abs(area) * 0.5;
}

// =========================================================
// [Section 3] 几何求解器 (Geometric Solvers)
// =========================================================

// --- 1. 中点求解器 (依赖: 2个点) ---
void Solver_Midpoint(GeoNode& self, const std::vector<GeoNode>& pool) {
    const auto& p1 = std::get<Data_Point>(pool[self.parents[0]].data);
    const auto& p2 = std::get<Data_Point>(pool[self.parents[1]].data);
    self.data = Data_Point{ (p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5 };
}

// --- 2. 圆求解器 (将圆属性同步给隐式 RPN) ---
void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool) {
    const auto& center = std::get<Data_Point>(pool[self.parents[0]].data);
    auto& circle = std::get<Data_Circle>(self.data);

    // 如果半径依赖于某个标量节点 (如 parents[1])
    if (self.parents.size() > 1) {
        circle.radius = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE);
    }

    // 更新隐式渲染用的 RPN ( (x-cx)^2 + (y-cy)^2 - r^2 )
    circle.implicit_rpn = {
        {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_CONST, center.x}, {RPNTokenType::SUB}, {RPNTokenType::PUSH_CONST, 2.0}, {RPNTokenType::POW},
        {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_CONST, center.y}, {RPNTokenType::SUB}, {RPNTokenType::PUSH_CONST, 2.0}, {RPNTokenType::POW},
        {RPNTokenType::ADD},
        {RPNTokenType::PUSH_CONST, circle.radius * circle.radius}, {RPNTokenType::SUB}
    };
}

// =========================================================
// [Section 4] 函数求解器 (RPN Hot-Patching)
// =========================================================

// --- 1. 通用 RPN 填空求解器 (针对 Data_SingleRPN) ---
void Solver_RPN_Patch(GeoNode& self, const std::vector<GeoNode>& pool) {
    auto& func = std::get<Data_SingleRPN>(self.data);

    for (const auto& bind : func.bindings) {
        if (bind.parent_index >= self.parents.size()) continue;

        const auto& parent = pool[self.parents[bind.parent_index]];
        double val = ExtractValue(parent, bind.prop);

        // 核心：直接注入内存
        if (bind.token_index < func.tokens.size()) {
            func.tokens[bind.token_index].value = val;
        }
    }
}

// =========================================================
// [Section 5] GeometryGraph 核心引擎
// =========================================================

GeometryGraph::GeometryGraph() {
    buckets.resize(128);
    for(auto& b : buckets) b.reserve(32);
}

uint32_t GeometryGraph::allocate_node() {
    auto id = (uint32_t)node_pool.size();
    node_pool.emplace_back(id);
    return id;
}

void GeometryGraph::Enqueue(GeoNode& node) {
    // 防重标记：确保一帧内每个节点只计算一次，不被多个父节点重复触发
    if (node.last_update_frame == current_frame_index) return;
    node.last_update_frame = current_frame_index;

    if (node.rank >= buckets.size()) buckets.resize(node.rank + 32);
    buckets[node.rank].push_back(node.id);

    // 动态调整当前帧的处理范围
    if ((int)node.rank < min_dirty_rank) min_dirty_rank = (int)node.rank;
    if ((int)node.rank > max_dirty_rank) max_dirty_rank = (int)node.rank;
}

void GeometryGraph::TouchNode(uint32_t id) {
    if (id >= node_pool.size()) return;
    Enqueue(node_pool[id]);
}

std::vector<uint32_t> GeometryGraph::SolveFrame() {
    // 开启新的时间戳
    current_frame_index++;

    std::vector<uint32_t> dirty_nodes;
    dirty_nodes.reserve(64);

    if (min_dirty_rank > max_dirty_rank) {
        min_dirty_rank = 10000; max_dirty_rank = 0;
        return dirty_nodes;
    }

    // 按 Rank 从小到大依次处理桶
    for (int r = min_dirty_rank; r <= max_dirty_rank; ++r) {
        auto& bucket = buckets[r];
        if (bucket.empty()) continue;

        for (uint32_t id : bucket) {
            GeoNode& node = node_pool[id];

            // 1. 计算 (Solve)
            // 自由点(Rank 0)跳过计算，依赖节点执行绑定的 Solver
            if (node.rank > 0 && node.solver) {
                node.solver(node, node_pool);
            }

            // 2. 收集清单 (用于局部更新渲染)
            dirty_nodes.push_back(id);

            // 3. 传播 (Propagate)
            for (uint32_t child_id : node.children) {
                Enqueue(node_pool[child_id]);
            }
        }
        // 处理完该层级后立即清空桶，不释放内存以供下帧复用
        bucket.clear();
    }

    // 重置状态
    min_dirty_rank = 10000; max_dirty_rank = 0;
    return dirty_nodes;
}

// =========================================================
// [Section 6] 外部工厂接口 (与 JS 交互用)
// =========================================================

// 示例：创建一个度量节点
uint32_t CreateMeasurementNode(GeometryGraph& g, const std::string& measureType, const std::vector<uint32_t>& parents) {
    uint32_t id = g.allocate_node();
    GeoNode& n = g.node_pool[id];
    n.parents = parents;
    n.render_type = GeoNode::RenderType::Scalar;
    n.data = Data_Scalar{ 0.0 };

    if (measureType == "Length") n.solver = Solver_Measure_Length;
    else if (measureType == "Angle") n.solver = Solver_Measure_Angle;
    else if (measureType == "Area") n.solver = Solver_Measure_Area;

    // 建立反向索引
    uint32_t max_r = 0;
    for (uint32_t pid : parents) {
        g.node_pool[pid].children.push_back(id);
        max_r = std::max(max_r, g.node_pool[pid].rank);
    }
    n.rank = max_r + 1;

    return id;
}