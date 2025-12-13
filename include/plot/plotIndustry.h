// --- 文件路径: include/plot/plotIndustry.h ---

#ifndef PLOTINDUSTRY_H
#define PLOTINDUSTRY_H

#include "../../pch.h"
#include "plotCall.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <functional>

// 回调设置
void SetIndustryStageCallback(std::function<void()> callback);

// 全局视图状态 (Watchdog 核心)
struct GlobalViewState {
    double offset_x;
    double offset_y;
    double zoom;
    double width;
    double height;
};

// 更新全局目标视图 (由 main.cpp 调用)
void UpdateTargetViewState(double ox, double oy, double zoom, double w, double h);

// ★★★ 修复：补回此函数声明 ★★★
// 用于触发 TBB context 的取消，配合 Watchdog 使用
void cancel_industry_calculation();

// 处理函数
void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y,
    double zoom
);

#endif // PLOTINDUSTRY_H