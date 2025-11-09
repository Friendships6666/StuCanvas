// --- 文件路径: include/plot/plotImplicit.h ---

#ifndef PLOTIMPLICIT_H
#define PLOTIMPLICIT_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"

constexpr unsigned int TILE_W = 512;
constexpr unsigned int TILE_H = 512;

struct ThreadCacheForTiling {
    AlignedVector<double> top_row_vals;
    AlignedVector<double> bot_row_vals;
    AlignedVector<PointData> point_buffer;
    ThreadCacheForTiling();
};

// ====================================================================
//  在这里添加新函数的声明
// ====================================================================
void process_implicit_adaptive(
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    oneapi::tbb::combinable<ThreadCacheForTiling>& thread_local_caches,
    oneapi::tbb::concurrent_vector<PointData>& all_points
);
// ====================================================================


// 注意：我们不再直接从 plotCall.cpp 调用 process_tile，
// 所以它的声明可以移除，或者保留为内部实现细节。
// 为了清晰，我们暂时将其移除。
/*
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
*/

#endif //PLOTIMPLICIT_H