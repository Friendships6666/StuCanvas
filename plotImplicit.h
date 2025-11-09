#ifndef PLOTIMPLICIT_H
#define PLOTIMPLICIT_H

#include "pch.h"
#include "RPN.h"

constexpr unsigned int TILE_W = 512;
constexpr unsigned int TILE_H = 512;

struct ThreadCacheForTiling {
    AlignedVector<double> top_row_vals;
    AlignedVector<double> bot_row_vals;
    AlignedVector<PointData> point_buffer;
    ThreadCacheForTiling();
};

void process_tile(
    const Vec2& world_origin, double wppx, double wppy,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    unsigned int x_start, unsigned int x_end,
    unsigned int y_start, unsigned int y_end,
    ThreadCacheForTiling& cache,
    oneapi::tbb::concurrent_vector<PointData>& all_points
);

#endif //PLOTIMPLICIT_H