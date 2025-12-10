// --- 文件路径: include/plot/plotIndustry.h ---

#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"
#include "plotCall.h"
#include <oneapi/tbb/concurrent_queue.h>

/**
 * @brief 工业级精度绘图任务入口 (Windows Native 重构版)
 *
 * 逻辑流程:
 * 1. 解析 "RPN;Precision" 字符串。
 * 2. 初始化 16x16 像素的网格任务。
 * 3. 阶段一：使用普通区间算术筛选活跃网格。
 * 4. 阶段二：使用扩展区间算术(MultiInterval)进行四叉树细分。
 * 5. 将结果汇总到 results_queue。
 *
 * @param results_queue  结果输出队列
 * @param industry_rpn   "RPN公式;精度;..."
 * @param func_idx       函数索引
 * @param world_origin   世界坐标原点 (对应屏幕(0,0))
 * @param wppx           X轴像素的世界宽度
 * @param wppy           Y轴像素的世界高度 (通常为负)
 * @param screen_width   屏幕宽度
 * @param screen_height  屏幕高度
 * @param offset_x       输出时需要减去的中心偏移 X
 * @param offset_y       输出时需要减去的中心偏移 Y
 */
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