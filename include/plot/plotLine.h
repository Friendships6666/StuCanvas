// --- 文件路径: include/plot/plotLine.h ---

#ifndef PLOTLINE_H
#define PLOTLINE_H
#include "plotCall.h"
// 引入预编译头，获取 PointData, FunctionResult, Vec2 等定义
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>

/**
 * @brief 特化直线绘制器 Ax + By + C = 0
 *
 * 内部实现细节：
 * 使用 TBB parallel_invoke 进行任务并行 (水平/垂直拆分)。
 * 使用强制宽度的 SIMD (128-bit) 进行数据并行。
 */
void process_line_equation(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double A, double B, double C,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
);

#endif // PLOTLINE_H