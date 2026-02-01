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
    uint32_t CreateInternalScalar(GeometryGraph &graph,
                               const std::string &infix_expr,
                               const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建自由点
     * @param config 包含颜色、粗细、名字、是否可见等
     */
    uint32_t CreateFreePoint(GeometryGraph &graph,
                          const std::string &x_expr,
                          const std::string &y_expr,
                          const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建线段
     */
    uint32_t CreateSegment(GeometryGraph &graph,
                        uint32_t p1_id,
                        uint32_t p2_id,
                        const GeoNode::VisualConfig &config = {});
    uint32_t CreateLine(GeometryGraph &graph,
                        uint32_t p1_id,
                        uint32_t p2_id,
                        const GeoNode::VisualConfig &config = {});

    uint32_t CreateRay(GeometryGraph &graph,
                    uint32_t p1_id,
                    uint32_t p2_id,
                    const GeoNode::VisualConfig &config = {});
    uint32_t CreateMidPoint(GeometryGraph &graph,
                         uint32_t p1_id,
                         uint32_t p2_id,
                         const GeoNode::VisualConfig &config = {});

    /**
     * @brief 创建约束点 (吸附在曲线/直线上的点)
     * @param target_id 目标对象 (Line/Circle/Curve)
     * @param x_expr, y_expr 初始吸附位置的锚点公式
     */
    uint32_t CreateConstrainedPoint(GeometryGraph &graph,
                                 uint32_t target_id,
                                 const std::string &x_expr,
                                 const std::string &y_expr,
                                 const GeoNode::VisualConfig &config = {});
    uint32_t CreateCircle_1Point_1Radius(GeometryGraph &graph,uint32_t center_id,const std::string &r,const GeoNode::VisualConfig &config = {});
    uint32_t CreateCircle_2Points(GeometryGraph &graph,uint32_t id1,uint32_t id2,const GeoNode::VisualConfig &config = {});
    uint32_t CreateCircle_3Points(GeometryGraph &graph,uint32_t id1,uint32_t id2,uint32_t id3,const GeoNode::VisualConfig &config = {});
    void CompileChannelInternal(GeometryGraph &graph, uint32_t node_id, int channel_idx,
                            const std::string &infix_expr, std::vector<uint32_t> &out_parents, bool is_preview);

    /**
     * @brief 创建图解交点
     * @param target_ids 参与交点计算的对象ID列表，要求为非点、非标量对象
     */
    uint32_t CreateGraphicalIntersection(GeometryGraph &graph,
                                      const std::vector<uint32_t> &target_ids,
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
    uint32_t CreateIntersection(GeometryGraph &graph,
                                     uint32_t target_id1,
                                     uint32_t target_id2,
                                     const std::string &x_expr,
                                     const std::string &y_expr,
                                     const GeoNode::VisualConfig &config);
    uint32_t CreateArc_2Points_1Radius(GeometryGraph &graph, uint32_t id1, uint32_t id2, const std::string &r, const GeoNode::VisualConfig &config);
    uint32_t CreateArc_3Points(GeometryGraph &graph, uint32_t id1, uint32_t id2, uint32_t id3,
                              const GeoNode::VisualConfig &config);

} // namespace GeoFactory

#endif
