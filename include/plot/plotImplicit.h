// --- 文件路径: include/plot/plotImplicit.h ---

#ifndef PLOTIMPLICIT_H
#define PLOTIMPLICIT_H

#include "../graph/GeoGraph.h"      // 包含 GeometryGraph, PointData, RPNToken 等定义
#include <oneapi/tbb/concurrent_queue.h>
#include <vector>

/**
 * @brief 极致性能隐函数解算核心入口 (单核极致逻辑版)
 *
 * 架构说明：
 * 1. 采用垂直向量化区间算术 (IA) 进行四叉树快速剪枝。
 * 2. 剪枝至 4x4 像素级别后，进入 Fused Sampling Kernel 进行采样。
 * 3. 采样层使用 4路 RPN 展开 + 寄存器栈顶缓存，实现单核极限吞吐。
 * 4. 结果直接在寄存器内完成视图变换并转为 int16 CLIP 坐标。
 * 5. 生成的点位以 std::vector<PointData> 为块压入并发有界队列。
 *
 * @param graph  几何图实例，提供当前视口(ViewState)的变换矩阵与缩放系数。
 * @param tokens 预编译完成的 RPN 指令序列 (必须以 RPNTokenType::STOP 结尾)。
 * @param queue  TBB 并发有界队列，用于接收计算线程生产的点位数据块。
 */
void calculate_implicit_core(
    GeometryGraph& graph,
    const std::vector<RPNToken>& tokens,
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& queue
);
#endif // PLOTIMPLICIT_H