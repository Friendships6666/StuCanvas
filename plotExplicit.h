#ifndef PLOTEXPLICIT_H
#define PLOTEXPLICIT_H

#include "pch.h"
#include "RPN.h"

void process_explicit_chunk(
    double y_min_world, double y_max_world,
    double x_start, double x_end,
    const AlignedVector<RPNToken>& rpn_program,
    double max_dist_sq, int max_depth,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx
);

#endif //PLOTEXPLICIT_H