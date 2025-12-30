// --- 文件路径: include/plot/plotImplicit.h ---

#ifndef PLOTIMPLICIT_H
#define PLOTIMPLICIT_H

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include "plotCall.h" // 包含 FunctionResult 的定义
#include "../functions/lerp.h" // ★★★ 必须包含，用于 NDCMap 定义

constexpr unsigned int TILE_W = 512;
constexpr unsigned int TILE_H = 512;

struct ThreadCacheForTiling {
    AlignedVector<double> top_row_vals;
    AlignedVector<double> bot_row_vals;
    ThreadCacheForTiling();
};

// 核心隐函数绘制逻辑 (自适应四叉树 + Marching Squares)
void process_implicit_adaptive(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    double offset_x, double offset_y,
    const NDCMap& ndc_map // ★★★ 新增参数：NDC 映射配置
);

#endif //PLOTIMPLICIT_H