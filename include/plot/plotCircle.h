// --- 文件路径: include/plot/plotCircle.h ---

#ifndef PLOTCIRCLE_H
#define PLOTCIRCLE_H

#include "../../pch.h"
#include "plotCall.h"
#include "../graph/GeoGraph.h" // 包含 PointData
#include <oneapi/tbb/concurrent_queue.h>

/**
 * @brief 特化圆绘制器
 *
 * 核心逻辑：
 * 1. 计算圆在屏幕上的像素大小，决定 LOD (采样点数)。
 * 2. 视口关系判定：
 *    - INSIDE: 快速生成整圆。
 *    - OUTSIDE: 直接剔除。
 *    - INTERSECT: 解析解求出可见的 t 区间，分段生成。
 * 3. 输出: Clip Space (Float)
 */
void PlotCircle(
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>* results_queue,
    double cx, double cy, double r,
    const ViewState& view,
    double start_t,
    double end_t,
    bool is_full_circle
);

#endif // PLOTCIRCLE_H