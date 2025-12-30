// --- 文件路径: include/plot/plotParametric.h ---

#ifndef PLOTPARAMETRIC_H
#define PLOTPARAMETRIC_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include "../functions/lerp.h" // 引入 NDCMap 定义
#include <oneapi/tbb/concurrent_vector.h>
#include "../../include/plot/plotCall.h"
/**
 * @brief 高性能参数方程绘制 (Parametric Function Plotter)
 *
 * 逻辑:
 * 1. 采样: 根据 t_min 和 t_max 计算总点数 (目前固定为每单位 20 点，未来可优化为自适应)。
 * 2. 并行: 使用 TBB 将 t 的范围切分为多个块 (Chunks) 并行计算。
 * 3. 向量化: 内部使用 XSIMD (AVX/SSE/WASM) 批量计算 X(t) 和 Y(t)。
 * 4. 精度控制:
 *    - 输入和中间计算全部使用 double 精度 (World Space)。
 *    - 输出时通过 NDCMap 转换为 float 精度 (Clip Space / NDC)。
 *
 * @param rpn_x x(t) 的 RPN 程序
 * @param rpn_y y(t) 的 RPN 程序
 * @param t_min 参数下限
 * @param t_max 参数上限
 * @param results_queue [输出] 线程安全的点收集容器
 * @param func_idx 函数索引
 * @param ndc_map NDC 映射参数包 (用于将结果从 World 转换到 Clip)
 */
void process_parametric_chunk(
    const AlignedVector<RPNToken>& rpn_x,
    const AlignedVector<RPNToken>& rpn_y,
    double t_min, double t_max,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    unsigned int func_idx,
    const NDCMap& ndc_map // ★★★ 新增参数
);

#endif //PLOTPARAMETRIC_H