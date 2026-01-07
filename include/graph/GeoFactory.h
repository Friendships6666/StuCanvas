// --- 文件路径: include/graph/GeoFactory.h ---
#ifndef GEOFACTORY_H
#define GEOFACTORY_H

#include "GeoGraph.h"
#include "CommandManager.h" // 必须包含这个才能识别 Transaction
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



    Transaction CreateScalar(GeometryGraph& graph, const RPNParam& expr, const GeoNode::VisualConfig& style = {});

    Transaction CreatePoint(GeometryGraph& graph, const RPNParam& x_expr, const RPNParam& y_expr, const GeoNode::VisualConfig& style = {});

    Transaction CreateLine(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, bool is_infinite, const GeoNode::VisualConfig& style = {});

    Transaction CreateCircle(GeometryGraph& graph, uint32_t center_id, const RPNParam& radius_expr, const GeoNode::VisualConfig& style = {});

    Transaction CreateCircleThreePoints(GeometryGraph& graph, uint32_t p1, uint32_t p2, uint32_t p3, const GeoNode::VisualConfig& style = {});

    Transaction CreateConstrainedPoint(GeometryGraph& graph, uint32_t target_id, const RPNParam& x_expr, const RPNParam& y_expr, const GeoNode::VisualConfig& style = {});

    Transaction CreateIntersectionPoint(GeometryGraph& graph, const RPNParam& x_e, const RPNParam& y_e, const std::vector<uint32_t>& targets, const GeoNode::VisualConfig& style = {});

    Transaction CreateAnalyticalIntersection(GeometryGraph& graph, uint32_t id1, uint32_t id2, const RPNParam& x_guess, const RPNParam& y_guess, const GeoNode::VisualConfig& style = {});

    Transaction CreateAnalyticalConstrainedPoint(GeometryGraph& graph, uint32_t target_id, const RPNParam& x_guess, const RPNParam& y_guess, const GeoNode::VisualConfig& style = {});

    Transaction CreateRatioPoint(GeometryGraph& graph, uint32_t p1_id, uint32_t p2_id, const RPNParam& ratio_expr, const GeoNode::VisualConfig& style = {});

    Transaction CreateParametricFunction(GeometryGraph& graph, const std::vector<MixedToken>& src_x, const std::vector<MixedToken>& src_y, double t_min, double t_max, const GeoNode::VisualConfig& style = {});

    Transaction CreateImplicitFunction(GeometryGraph& graph, const std::vector<MixedToken>& tokens, const GeoNode::VisualConfig& style = {});

    Transaction CreateExplicitFunction(GeometryGraph& graph, const std::vector<MixedToken>& tokens, const GeoNode::VisualConfig& style = {});

    // --- 更新类 (返回事务) ---
    Transaction UpdateFreePoint_Tx(GeometryGraph& graph, uint32_t id, const RPNParam& x_expr, const RPNParam& y_expr);

    Transaction DeleteObject_Tx(GeometryGraph& graph, uint32_t id);

    Transaction UpdateStyle_Tx(GeometryGraph& graph, uint32_t id, const GeoNode::VisualConfig& new_style);

    Transaction UpdateLabelPosition_Tx(GeometryGraph& graph, uint32_t label_id, double mouse_wx, double mouse_wy, const ViewState& view);

} // namespace GeoFactory

#endif