// --- 文件路径: include/plot/plotIndustryParametric.h ---

#ifndef PLOT_INDUSTRY_PARAMETRIC_H
#define PLOT_INDUSTRY_PARAMETRIC_H

#include "../../pch.h"
#include "plotCall.h"
#include <oneapi/tbb/concurrent_queue.h>

/**
 * @brief 处理工业级参数方程绘图任务
 *
 * 输入格式字符串: "x_rpn;y_rpn;t_min;t_max;precision"
 *
 * @param results_queue 结果输出队列
 * @param param_config_str 配置字符串
 * @param func_idx 函数索引
 * @param world_origin 世界坐标原点
 * @param wppx X轴每像素代表的世界距离
 * @param wppy Y轴每像素代表的世界距离
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 * @param offset_x 全局视图 X 偏移
 * @param offset_y 全局视图 Y 偏移
 */
void process_industry_parametric(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& param_config_str,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
);

#endif // PLOT_INDUSTRY_PARAMETRIC_H