// --- 文件路径: include/graph/GeoFactory.h ---
#ifndef GEOFACTORY_H
#define GEOFACTORY_H

#include "GeoGraph.h"

namespace GeoFactory {

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

} // namespace GeoFactory

#endif