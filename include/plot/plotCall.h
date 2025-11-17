// --- 文件路径: include/plot/plotCall.h ---

#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"

// ====================================================================
//  MODIFIED: calculate_points_core 签名
//  - 第三个参数类型更改为 `const std::vector<std::pair<std::string, std::string>>&`
// ====================================================================
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    const std::vector<std::string>& industry_rpn_list, // <-- 新增参数
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H