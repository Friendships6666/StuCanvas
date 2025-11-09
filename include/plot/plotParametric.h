#ifndef PLOTPARAMETRIC_H
#define PLOTPARAMETRIC_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"

void process_parametric_chunk(
    double y_min_world, double y_max_world, 
    double x_min_world, double x_max_world,
    double t_start, double t_end,
    const AlignedVector<RPNToken>& rpn_x, 
    const AlignedVector<RPNToken>& rpn_y,
    double max_dist_sq, int max_depth,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx
);

#endif
