// --- 文件路径: include/plot/plotCall.h ---

#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"
#include <vector>
#include <string>

// ====================================================================
//          ↓↓↓ 定义结果传递结构体 ↓↓↓
// ====================================================================
struct FunctionResult {
    unsigned int function_index;
    std::vector<PointData> points; // 计算出的点集
};

// ====================================================================
//          ↓↓↓ 核心计算函数声明 ↓↓↓
// ====================================================================

/**
 * @brief 核心绘图调度函数。负责解析参数、启动后台线程、收集并合并结果。
 *
 * @param out_points [输出] 存储所有计算出的点（按顺序排列，或由原子索引决定）。
 * @param out_ranges [输出] 存储每个函数的点在 out_points 中的起始位置和长度。
 * @param implicit_rpn_pairs 普通隐函数列表 (pair: <计算RPN, 检查RPN>)。
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
    const std::vector<std::string>& industry_rpn_list, // <--- 新增参数 (第4个)
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H