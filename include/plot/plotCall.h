#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "../../pch.h"

// ====================================================================
//          ↓↓↓ 新增：定义用于在线程间传递的结果结构体 ↓↓↓
// ====================================================================
struct FunctionResult {
    unsigned int function_index;
    // 使用常规的 std::vector，因为此时它是一个独立的数据块，不再需要并发访问
    std::vector<PointData> points;
};
// ====================================================================

// calculate_points_core 的声明现在移除了 explicit 和 parametric 列表
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H