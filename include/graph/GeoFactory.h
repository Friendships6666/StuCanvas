#ifndef GEO_FACTORY_H
#define GEO_FACTORY_H

#include "GeoGraph.h"

namespace GeoFactory {
    enum class OpCode : uint8_t {
        CREATE_INTERNAL_SCALAR = 0x10,
        CREATE_FREE_POINT = 0x20,
        CREATE_SEGMENT_2P = 0x30,
        CREATE_MID_POINT = 0x40, // 创建中点
        CREATE_CONSTRAINED_POINT = 0x50, // 创建约束点（吸附点）
    };

    /**
     * @brief 创建内部标量 (通常不可见，但允许传入配置以供调试或特殊命名)
     */
    uint32_t AddInternalScalar(GeometryGraph &graph,
                               const std::string &infix_expr,
                               const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建自由点
     * @param config 包含颜色、粗细、名字、是否可见等
     */
    uint32_t AddFreePoint(GeometryGraph &graph,
                          const std::string &x_expr,
                          const std::string &y_expr,
                          const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建线段
     */
    uint32_t AddSegment(GeometryGraph &graph,
                        uint32_t p1_id,
                        uint32_t p2_id,
                        const GeoNode::VisualConfig &config = {});

    uint32_t AddMidPoint(GeometryGraph &graph,
                         uint32_t p1_id,
                         uint32_t p2_id,
                         const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建约束点 (吸附在曲线/直线上的点)
     * @param target_id 目标对象 (Line/Circle/Curve)
     * @param x_expr, y_expr 初始吸附位置的锚点公式
     */
    uint32_t AddConstrainedPoint(GeometryGraph &graph,
                                 uint32_t target_id,
                                 const std::string &x_expr,
                                 const std::string &y_expr,
                                 const GeoNode::VisualConfig &config = {});

    void DeleteObjectRecursive(GeometryGraph &graph, uint32_t target_id);

    void InternalUpdateScalar(GeometryGraph &graph, uint32_t scalar_id, const std::string &new_infix);

    void UpdatePointScalar(GeometryGraph &graph, uint32_t point_id,
                           const std::string &new_x_expr,
                           const std::string &new_y_expr);
    void RefreshViewState(GeometryGraph& graph);
    void UpdateViewTransform(GeometryGraph& graph, double ox, double oy, double zoom);
    void UpdateViewSize(GeometryGraph& graph, double w, double h);
} // namespace GeoFactory

#endif
