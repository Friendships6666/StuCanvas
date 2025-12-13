// --- 文件路径: include/plot/plotCall.h ---

#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"
#include <vector>
#include <string>

struct FunctionResult {
    unsigned int function_index;
    std::vector<PointData> points; // 计算出的点集
};

/**
 * @brief 核心绘图调度函数。
 *
 * @param out_points [输出] 存储所有计算出的点。
 * @param out_ranges [输出] 存储每个函数的点在 out_points 中的起始位置和长度。
 * @param implicit_rpn_pairs 普通隐函数列表 (pair: <计算RPN, 检查RPN>) - 用于复杂的中缀转RPN场景。
 * @param implicit_rpn_direct_list ★★★ 新增参数：直接RPN字符串列表 (计算和检查相同) ★★★
 * @param industry_rpn_list 工业级函数列表 (string: "RPN;精度;参数...")。
 * @param offset_x 视图中心 X 偏移。
 * @param offset_y 视图中心 Y 偏移。
 * @param zoom 缩放级别。
 * @param screen_width 屏幕宽度。
 * @param screen_height 屏幕高度。
 */
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& implicit_rpn_direct_list, // <--- 新增
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H