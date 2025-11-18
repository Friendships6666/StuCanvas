#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"
#include "plotCall.h"

/**
 * @brief 处理单个工业级精度函数（高精度或双精度）的绘图任务。
 *
 * 此函数作为该功能模块的入口点。它会解析输入的RPN字符串以确定所需的计算精度，
 * 然后分派给相应的模板化实现（高精度hp_float或标准double）。
 * 函数内部实现了两阶段并行计算策略：
 * 1. 使用四叉树算法快速剔除没有函数曲线穿过的广阔区域。
 * 2. 对四叉树细分到像素级别的“叶节点”区域，启动大规模并行计算，
 *    在每个像素内部进行100x100的微区块扫描，以高精度定位曲线并生成点。
 *
 * 计算完成后，所有生成的点将被打包成一个 FunctionResult 结构体，并推入线程安全的结果队列中。
 *
 * @param results_queue   指向线程安全队列的指针，用于将计算结果异步返回给主调用线程。
 * @param industry_rpn    一个特殊格式的字符串，格式为 "RPN表达式;精度"，例如 "x 2 pow y 2 pow - 1;256"。
 *                        如果精度为0，则使用双精度(double)进行计算。
 * @param func_idx        当前函数的唯一索引，用于在最终结果中标识这些点。
 * @param world_origin    当前屏幕视图左上角在世界坐标系中的位置。
 * @param wppx            世界坐标系中每个像素的宽度。
 * @param wppy            世界坐标系中每个像素的高度（通常为负值）。
 * @param screen_width    屏幕的总宽度（以像素为单位）。
 * @param screen_height   屏幕的总高度（以像素为单位）。
 * @param offset_x        视图的X轴偏移量（未使用，但保留以兼容接口）。
 * @param offset_y        视图的Y轴偏移量（未使用，但保留以兼容接口）。
 * @param zoom            视图的缩放级别（未使用，但保留以兼容接口）。
 */
void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
);

#endif // PLOTINDUSTRY_H