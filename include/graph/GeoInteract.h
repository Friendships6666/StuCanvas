#ifndef GEO_INTERACT_H
#define GEO_INTERACT_H

#include "GeoGraph.h"

/**
 * @brief 交互式点添加函数
 * @param graph 几何图引用
 * @param screen_x 屏幕点击 X 坐标
 * @param screen_y 屏幕点击 Y 坐标
 */
uint32_t AddPoint_Interact(GeometryGraph& graph, double screen_x, double screen_y);

/**
 * @brief 尝试选择点或创建新点以开始绘制线段
 * @param graph 几何图引用
 * @param screen_x 屏幕点击 X 坐标
 * @param screen_y 屏幕点击 Y 坐标
 * @return 选中的或创建的点的ID
 */
uint32_t InitSegment_Interact(GeometryGraph& graph);
uint32_t TrySelect_Interact(GeometryGraph& graph, bool is_multi_select);
Vec2 SnapToGrid_Interact(GeometryGraph& graph, Vec2 world_coord);
void PreviewSegment_Intertact(GeometryGraph& graph);
void CancelPreview_Intectact(GeometryGraph& graph);
void UpdateMousePos_Interact(GeometryGraph& graph,double x,double y);
#endif // GEO_INTERACT_H
