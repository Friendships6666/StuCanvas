// --- 文件路径: include/plot/plotIndustry.h ---

#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"
#include "plotCall.h"
#include <oneapi/tbb/concurrent_queue.h>

// 导出取消函数
void cancel_industry_calculation();

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
);

#endif // PLOTINDUSTRY_H