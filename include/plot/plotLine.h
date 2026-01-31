

#ifndef PLOTSEGMENT_H
#define PLOTSEGMENT_H

#include "../../pch.h"
#include "plotCall.h"
#include "../functions/lerp.h" // 引入 NDCMap 定义
#include <oneapi/tbb/concurrent_queue.h>
enum class LinePlotType : uint8_t {
    SEGMENT = 0,   // 线段 [P1, P2]
    LINE = 1,  // 直线 (-inf, +inf)
    RAY = 2        // 射线 [P1, +inf)
};

void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& queue,
    double x1, double y1, double x2, double y2, // 输入为相对坐标 (x_view, y_view)
    LinePlotType type, // 修改后的枚举参数
    const ViewState& view
) ;

#endif // PLOTSEGMENT_H