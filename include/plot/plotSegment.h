

#ifndef PLOTSEGMENT_H
#define PLOTSEGMENT_H

#include "../../pch.h"
#include "plotCall.h"
#include "../functions/lerp.h" // 引入 NDCMap 定义
#include <oneapi/tbb/concurrent_queue.h>


void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2,
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, // 对应头文件签名
    const NDCMap& ndc_map
);

#endif // PLOTSEGMENT_H