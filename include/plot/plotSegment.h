

#ifndef PLOTSEGMENT_H
#define PLOTSEGMENT_H

#include "../../pch.h"
#include "plotCall.h"
#include "../functions/lerp.h" // 引入 NDCMap 定义
#include <oneapi/tbb/concurrent_queue.h>


void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& queue,
    double x1, double y1, double x2, double y2, // 输入为相对坐标 (x_view, y_view)
    bool is_segment,
    uint32_t func_id,
    const ViewState& view
);

#endif // PLOTSEGMENT_H