#ifndef PLOTIMPLICIT_H
#define PLOTIMPLICIT_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include "plotCall.h" // 包含 FunctionResult 的定义

constexpr unsigned int TILE_W = 512;
constexpr unsigned int TILE_H = 512;

struct ThreadCacheForTiling {
    AlignedVector<double> top_row_vals;
    AlignedVector<double> bot_row_vals;
    ThreadCacheForTiling();
};

// 函数签名已更新
void process_implicit_adaptive(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx
);
#endif //PLOTIMPLICIT_H