#ifndef PLOTCALL_H
#define PLOTCALL_H

#include "pch.h"

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

#endif //PLOTCALL_H