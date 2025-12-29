// --- 文件路径: include/graph/GeoFactory.h ---
#ifndef GEOFACTORY_H
#define GEOFACTORY_H

#include "GeoGraph.h"

namespace GeoFactory {
    struct Ref {
        uint32_t id;
        explicit Ref(uint32_t i) : id(i) {}
    };

    struct GVar {
        double value = 0.0;
        bool is_ref = false;
        uint32_t ref_id = 0;
        GVar(double v) : value(v), is_ref(false) {}
        GVar(int v) : value(static_cast<double>(v)), is_ref(false) {}
        GVar(Ref r) : value(0), is_ref(true), ref_id(r.id) {}
    };

    // ★★★ 新增：混合 Token 类型 (用于构建 RPN) ★★★
    // 用户可以传入: RPNTokenType::ADD, 3.14, Ref(id)
    using MixedToken = std::variant<RPNTokenType, double, Ref>;

    // 1. 创建统一的点 (支持静态或动态)
    uint32_t CreatePoint(GeometryGraph& graph, GVar x, GVar y);

    // 2. 创建圆 (支持动态半径)
    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, GVar radius);

    // 3. 创建显式函数 (使用混合 Token)
    uint32_t CreateExplicitFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens
    );

    /**
     * @brief 创建一个自由移动的点
     */
    uint32_t CreateFreePoint(GeometryGraph& graph, double x, double y);

    /**
     * @brief 创建一条线段或直线 (依赖两点)
     * @param is_infinite true 为无限直线, false 为线段
     */
    uint32_t CreateLine(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite);

    /**
     * @brief 创建一个中点 (依赖两点)
     */
    uint32_t CreateMidpoint(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id);

    /**
     * @brief 创建一个圆 (依赖圆心和半径)
     */
    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, double radius);

    /**
     * @brief 创建一个动态函数 (显式/隐式/参数方程)
     */
    uint32_t CreateFunction(
        GeometryGraph& graph,
        GeoNode::RenderType r_type,
        const AlignedVector<RPNToken>& tokens,
        const std::vector<RPNBinding>& bindings,
        const std::vector<uint32_t>& parent_ids
    );
    uint32_t CreatePerpendicular(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id, bool is_infinite);
    uint32_t CreateParallel(GeometryGraph& graph, uint32_t segment_id, uint32_t point_id);
    uint32_t CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, double initial_x, double initial_y);
    uint32_t CreateTangent(GeometryGraph& graph, uint32_t constrained_point_id);
    uint32_t CreateMeasureLength(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id);

} // namespace GeoFactory

#endif