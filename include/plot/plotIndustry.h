#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"

/**
 * @brief (内部辅助函数) 使用高精度区间算术处理单个工业级RPN函数。
 *
 * 该函数执行四叉树细分，并在高精度环境下将最终的世界坐标点转换为
 * 裁剪空间坐标，然后添加到输出向量中。
 *
 * @param out_points 一个线程安全的向量，用于存储生成的点。
 * @param industry_rpn 单个 "RPN;精度" 格式的字符串。
 * @param func_idx 此函数在所有函数列表中的唯一索引。
 * @param world_origin 屏幕左上角对应的世界坐标。
 * @param wppx 每个像素代表的世界坐标宽度。
 * @param wppy 每个像素代表的世界坐标高度。
 * @param screen_width 屏幕总宽度（像素）。
 * @param screen_height 屏幕总高度（像素）。
 * @param offset_x 视图的X轴偏移 (用于坐标转换)。
 * @param offset_y 视图的Y轴偏移 (用于坐标转换)。
 * @param zoom 视图的缩放级别 (用于坐标转换)。
 */
void process_single_industry_function(
    oneapi::tbb::concurrent_vector<PointData>& out_points,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom // <-- 新增参数
);

#endif // PLOTINDUSTRY_H