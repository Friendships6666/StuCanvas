// --- 文件路径: include/plot/plotCall.h ---

#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"

struct FunctionResult {
    unsigned int function_index;
    std::vector<PointData> points;
};

// 移除 industry_rpn_list 参数
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H