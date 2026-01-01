// --- 文件路径: include/graph/GeoFactory.h ---
#ifndef GEOFACTORY_H
#define GEOFACTORY_H

#include "GeoGraph.h"

namespace GeoFactory {

    struct Ref {
        uint32_t id;
        explicit Ref(uint32_t i) : id(i) {}
    };
    using MixedToken = std::variant<RPNTokenType, double, Ref>;
    using RPNParam = std::vector<MixedToken>; // 强制使用序列

    struct GVar {
        double value = 0.0;
        bool is_ref = false;
        uint32_t ref_id = 0;
        GVar(double v) : value(v), is_ref(false) {}
        GVar(int v) : value(static_cast<double>(v)), is_ref(false) {}
        GVar(Ref r) : value(0), is_ref(true), ref_id(r.id) {}
    };



    // 统一后的 API
    uint32_t CreatePoint(GeometryGraph& graph, const RPNParam& x_expr, const RPNParam& y_expr);
    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, const RPNParam& radius_expr);

    // 内部提升函数
    uint32_t CreateScalar(GeometryGraph& graph, const RPNParam& expr);

    // 3. 创建显式函数 (使用混合 Token)
    uint32_t CreateExplicitFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens
    );

    /**
    * @brief 创建动态参数方程 x = f(t), y = g(t)
    * @param tokens_x x(t) 的混合 Token 序列
    * @param tokens_y y(t) 的混合 Token 序列
    */
    uint32_t CreateParametricFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens_x,
        const std::vector<MixedToken>& tokens_y,
        double t_min, double t_max
    );

    /**
     * @brief 创建动态隐函数 f(x, y) = 0
     * @param tokens 混合 Token 序列
     */
    uint32_t CreateImplicitFunction(
        GeometryGraph& graph,
        const std::vector<MixedToken>& tokens
    );




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
    uint32_t CreateCircle(GeometryGraph& graph, uint32_t center_id, const RPNParam& radius_expr);

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
    uint32_t CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, const RPNParam& x_expr, const RPNParam& y_expr);
    uint32_t CreateTangent(GeometryGraph& graph, uint32_t constrained_point_id);
    uint32_t CreateMeasureLength(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id);
    uint32_t CreateIntersectionPoint(
        GeometryGraph& graph,
        const RPNParam& x_init,
        const RPNParam& y_init,
        const std::vector<uint32_t>& target_ids
    );
    uint32_t CreateAnalyticalIntersection(
    GeometryGraph& graph,
    uint32_t id1, uint32_t id2,
    const RPNParam& x_guess,
    const RPNParam& y_guess
    );

    uint32_t CreateAnalyticalConstrainedPoint(
        GeometryGraph &graph,
        uint32_t target_id,
        const RPNParam &x_guess,
        const RPNParam &y_guess
    );

} // namespace GeoFactory

#endif