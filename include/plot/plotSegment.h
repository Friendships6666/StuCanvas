// --- 文件路径: include/plot/plotSegment.h ---

#ifndef PLOTSEGMENT_H
#define PLOTSEGMENT_H

#include "../../pch.h"
#include "plotCall.h"
#include <oneapi/tbb/concurrent_queue.h>

/**
 * @brief 特化两点连线绘制器 (线段 / 直线)
 *
 * 技术栈：
 * - 算法: Liang-Barsky 参数化裁剪
 * - 并行: TBB parallel_invoke (水平/垂直裁剪并行)
 * - SIMD: 强制 SIMD128 (Batch=2)
 * - 输出: Clip Space (NDC) Float32
 *
 * @param results_queue 结果队列
 * @param x1, y1 起点 (世界坐标)
 * @param x2, y2 终点 (世界坐标)
 * @param is_segment true=线段(0<=t<=1), false=直线(t无限)
 * @param func_idx 函数索引
 * @param world_origin 视口原点
 * @param wppx, wppy 世界/像素比例
 * @param screen_width, screen_height 屏幕尺寸
 * @param offset_x, offset_y 交互偏移 (用于计算中心点)
 */
void process_two_point_line(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    double x1, double y1, double x2, double y2,
    bool is_segment,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
);

#endif // PLOTSEGMENT_H