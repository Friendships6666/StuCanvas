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
//  MODIFIED: 函数声明修正回 'process_implicit_adaptive'
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

#endif //PLOTIMPLICIT_H