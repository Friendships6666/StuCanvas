// --- 文件路径: include/plot/plotParametric.h ---

#ifndef PLOTPARAMETRIC_H
#define PLOTPARAMETRIC_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include <oneapi/tbb/concurrent_vector.h>

/**
 * @brief 高性能参数方程绘制 (普通模式)
 * 
 * 逻辑仿照 plotExplicit:
 * 1. 严格保证 t 从小到大的顺序。
 * 2. 只有当 x(t) 或 y(t) 为非有限值 (NaN/Inf) 时才剔除。
 * 3. 采样数量由 t 的范围决定 ( (max-min)*20 )。
 * 4. 使用分块局部缓存 + SIMD 加速 + 2x 循环展开。
 * 
 * @param rpn_x x(t) 的 RPN 程序
 * @param rpn_y y(t) 的 RPN 程序
 * @param t_min 参数下限
 * @param t_max 参数上限
 * @param all_points 输出点集 (并发向量)
 * @param func_idx 函数索引
 */
void process_parametric_chunk(
    const AlignedVector<RPNToken>& rpn_x,
    const AlignedVector<RPNToken>& rpn_y,
    double t_min, double t_max,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx
);

#endif //PLOTPARAMETRIC_H