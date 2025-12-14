// --- 文件路径: include/plot/plotExplicit.h ---

#ifndef PLOTEXPLICIT_H
#define PLOTEXPLICIT_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include <oneapi/tbb/concurrent_vector.h>

/**
 * @brief 高性能显函数绘制 (固定步长 + SIMD + 并行分块)
 *
 * 逻辑特性:
 * 1. 严格保证 X 坐标从左到右的顺序。
 * 2. Y 值超出屏幕范围不剔除 (保留用于 GPU 连线)。
 * 3. 仅剔除非有限值 (NaN / Inf)。
 * 4. 使用分块局部缓存策略，避免全局串行过滤。
 *
 * @param y_min_world 屏幕下边界 (仅用于参考，不进行剔除)
 * @param y_max_world 屏幕上边界 (仅用于参考，不进行剔除)
 * @param x_start_world 屏幕左边界世界坐标
 * @param x_end_world 屏幕右边界世界坐标
 * @param rpn_program RPN 程序
 * @param all_points 输出点集 (并发向量)
 * @param func_idx 函数索引
 * @param screen_width 屏幕像素宽度 (用于计算步长)
 */
void process_explicit_chunk(
    double y_min_world, double y_max_world,
    double x_start_world, double x_end_world,
    const AlignedVector<RPNToken>& rpn_program,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx,
    double screen_width
);

#endif //PLOTEXPLICIT_H