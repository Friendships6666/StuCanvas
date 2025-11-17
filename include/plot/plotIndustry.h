#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"
#include "plotCall.h"

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue, // <-- 修改
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
);

#endif // PLOTINDUSTRY_H