#ifndef GEO_INTERACT_H
#define GEO_INTERACT_H

#include "GeoGraph.h"

/**
 * @brief 交互式点添加函数
 * @param graph 几何图引用
 * @param screen_x 屏幕点击 X 坐标
 * @param screen_y 屏幕点击 Y 坐标
 */
void AddPoint_Interact(GeometryGraph& graph, double screen_x, double screen_y);

#endif // GEO_INTERACT_H
