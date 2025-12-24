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
    if (self.parents.empty()) return;
    const auto& center = std::get<Data_Point>(pool[self.parents[0]].data);
    auto& circle = std::get<Data_Circle>(self.data);

    if (self.parents.size() > 1) {
        circle.radius = ExtractValue(pool[self.parents[1]], RPNBinding::VALUE, pool);
    }

    circle.implicit_rpn = {
        {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_CONST, center.x}, {RPNTokenType::SUB}, {RPNTokenType::PUSH_CONST, 2.0}, {RPNTokenType::POW},
        {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_CONST, center.y}, {RPNTokenType::SUB}, {RPNTokenType::PUSH_CONST, 2.0}, {RPNTokenType::POW},
        {RPNTokenType::ADD}, {RPNTokenType::PUSH_CONST, circle.radius * circle.radius}, {RPNTokenType::SUB}
    };
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