// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include <algorithm>
#include <cmath>

// =========================================================
// [内部辅助] 数值提取器
// =========================================================
static inline double ExtractValue(const GeoNode& parent, RPNBinding::Property prop, const std::vector<GeoNode>& pool) {
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

void Solver_Circle(GeoNode& self, const std::vector<GeoNode>& pool) {
    // 1. 基础检查
    if (self.parents.empty()) return;

    // 2. 获取数据指针
    auto& circle_data = std::get<Data_Circle>(self.data);

    // 3. 获取圆心 (Parent 0 必须是 Point)
    // 注意：这里假设 CreateCircle 时已经做了类型检查
    if (self.parents[0] < pool.size()) {
        const auto& center_node = pool[self.parents[0]];
        if (std::holds_alternative<Data_Point>(center_node.data)) {
            const auto& p = std::get<Data_Point>(center_node.data);
            circle_data.cx = p.x;
            circle_data.cy = p.y;
        }
    }

    // 4. 获取半径 (Parent 1 可选，如果是标量依赖)
    if (self.parents.size() > 1) {
        // 使用通用的 ExtractValue 获取最新的半径值
        // ExtractValue 需要在 GeoSolver.cpp 内部可见 (前面已定义)
        circle_data.radius = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE, pool);
    }

    // 逻辑结束：现在 circle_data 里的 cx, cy, radius 都是最新的
    // 不需要生成 implicit_rpn
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

    // 1. 获取目标线段 (Parent 0) 和 外部点 (Parent 1)
    const auto& segmentNode = pool[self.parents[0]];
    const auto& sourcePointNode = pool[self.parents[1]];

    // 提取线段的端点 ID
    const auto& segData = std::get<Data_Line>(segmentNode.data);

    // 获取三点坐标：端点 p1, p2 和 外部点 p0
    const auto& p1 = std::get<Data_Point>(pool[segData.p1_id].data);
    const auto& p2 = std::get<Data_Point>(pool[segData.p2_id].data);
    const auto& p0 = std::get<Data_Point>(sourcePointNode.data);

    // 2. 向量数学计算：投影法
    // 向量 v = p2 - p1 (直线的方向向量)
    double vx = p2.x - p1.x;
    double vy = p2.y - p1.y;
    double v_mag_sq = vx * vx + vy * vy;

    // 防止线段长度为 0
    if (v_mag_sq < 1e-12) {
        self.data = Data_Point{ p1.x, p1.y };
        return;
    }

    // 向量 w = p0 - p1
    double wx = p0.x - p1.x;
    double wy = p0.y - p1.y;

    // 投影参数 t = (w · v) / |v|^2
    // 如果 t=0, 垂足是 p1; 如果 t=1, 垂足是 p2
    double t = (wx * vx + wy * vy) / v_mag_sq;

    // 3. 计算垂足坐标并存入 self.data
    Data_Point foot;
    foot.x = p1.x + t * vx;
    foot.y = p1.y + t * vy;

    self.data = foot;
}