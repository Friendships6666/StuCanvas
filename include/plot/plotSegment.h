

#ifndef PLOTSEGMENT_H
#define PLOTSEGMENT_H

#include "../../pch.h"
#include "plotCall.h"
#include "../functions/lerp.h" // 引入 NDCMap 定义
#include <oneapi/tbb/concurrent_queue.h>


void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2, // 💡 这里的输入已经是相对坐标 (view-relative)
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin, // 保持签名一致，但计算将更多参考屏幕尺寸
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y,
    const NDCMap& ndc_map
);

#endif // PLOTSEGMENT_H