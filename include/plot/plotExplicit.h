// --- 文件路径: include/plot/plotExplicit.h ---

#ifndef PLOTEXPLICIT_H
#define PLOTEXPLICIT_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include "../functions/lerp.h" // 必须包含，因为需要 NDCMap 结构体定义
#include <oneapi/tbb/concurrent_vector.h>
#include "../../include/plot/plotCall.h"
/**
 * @brief 高性能显函数绘制器 (Explicit Function Plotter)
 *
 * 核心逻辑:
 * 1. 采样: 根据屏幕宽度决定采样步长 (Step Size)。
 * 2. 并行: 使用 TBB 将 X 轴范围切分为多个块 (Chunks) 并行计算。
 * 3. 向量化: 内部使用 XSIMD (AVX/SSE/WASM) 批量计算 RPN。
 * 4. 精度控制:
 *    - 输入和中间计算全部使用 double 精度 (World Space)。
 *    - 输出时通过 NDCMap 转换为 float 精度 (Clip Space / NDC)。
 *

 * @param x_start_world 采样起始 X (世界坐标)
 * @param x_end_world 采样结束 X (世界坐标)
 * @param rpn_program 预编译的 RPN 指令流
 * @param results_queue [输出] 线程安全的点收集容器
 * @param func_idx 函数的唯一 ID (用于着色)
 * @param screen_width 屏幕像素宽度 (用于计算采样密度)
 * @param ndc_map NDC 映射参数包 (用于将结果从 World 转换到 Clip)
 */
void process_explicit_chunk(

    double x_start_world, double x_end_world,
    const AlignedVector<RPNToken>& rpn_program,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    unsigned int func_idx,
    double screen_width,
    const NDCMap& ndc_map // ★★★ 新增：用于坐标空间转换
);

#endif //PLOTEXPLICIT_H