// --- 文件路径: src/plot/plotIndustry.cpp ---

#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include "../../include/interval/MultiInterval.h"
#include "../../include/CAS/RPN/MultiIntervalRPN.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>

#include <vector>
#include <atomic>
#include <cstring>
#include <cmath>
#include <iostream>
#include <chrono>
#include <mutex>

// 全局变量引用
extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern std::atomic<size_t> g_points_atomic_index;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;
extern std::atomic<int> g_industry_stage_version;

// --- 新增：全局取消标记 ---
std::atomic<bool> g_cancel_compute{false};
// --- 新增：记录上一帧最终达到的细分阈值 (world units) ---
double g_last_finished_threshold = 1e9;

namespace {
    constexpr size_t HARD_LIMIT_POINTS = 50000000;
    std::mutex g_industry_mutex;
    AlignedVector<uint8_t> g_cached_pixel_counts;

    template<typename T>
    double to_double(const T& val) { return static_cast<double>(val); }

    template<typename T>
    struct IndustryTask {
        T x, y;
        T w, h;
    };

    // ... (evaluate_rpn_fast_local 保持不变，省略以节省空间) ...
    template<typename T>
    MultiInterval<T> evaluate_rpn_fast_local(const AlignedVector<RPNToken>& prog, const Interval<T>& x_val, const Interval<T>& y_val) {
        // ... (原样保留)
        MultiInterval<T> stack[64];
        int sp = 0;
        MultiInterval<T> mx(x_val);
        MultiInterval<T> my(y_val);
        for (const auto& token : prog) {
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = MultiInterval<T>(T(token.value)); break;
                case RPNTokenType::PUSH_X: stack[sp++] = mx; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = my; break;
                case RPNTokenType::ADD: { sp--; stack[sp-1] = stack[sp-1] + stack[sp]; break; }
                case RPNTokenType::SUB: { sp--; stack[sp-1] = stack[sp-1] - stack[sp]; break; }
                case RPNTokenType::MUL: { sp--; stack[sp-1] = stack[sp-1] * stack[sp]; break; }
                case RPNTokenType::DIV: { sp--; stack[sp-1] = stack[sp-1] / stack[sp]; break; }
                case RPNTokenType::POW: { sp--; stack[sp-1] = multi_pow(stack[sp-1], stack[sp]); break; }
                case RPNTokenType::SIN: stack[sp-1] = multi_sin(stack[sp-1]); break;
                case RPNTokenType::COS: stack[sp-1] = multi_cos(stack[sp-1]); break;
                case RPNTokenType::TAN: stack[sp-1] = multi_tan(stack[sp-1]); break;
                case RPNTokenType::EXP: stack[sp-1] = multi_exp(stack[sp-1]); break;
                case RPNTokenType::LN:  stack[sp-1] = multi_ln(stack[sp-1]); break;
                case RPNTokenType::ABS: stack[sp-1] = multi_abs(stack[sp-1]); break;
                default: break;
            }
        }
        if (sp == 0) return MultiInterval<T>();
        return stack[sp-1];
    }

    template<typename T>
    void process_tasks_parallel(
        const std::vector<IndustryTask<T>>& current_tasks,
        oneapi::tbb::enumerable_thread_specific<std::vector<IndustryTask<T>>>& next_tasks_tls,
        const IndustrialRPN& rpn,
        unsigned int func_idx,
        double wppx, double wppy,
        const Vec2& world_origin,
        uint8_t* pixel_counts, int sw, int sh,
        double offset_x, double offset_y,
        double next_split_threshold_w,
        PointData* raw_buffer_ptr,
        size_t buffer_capacity,
        double current_threshold_w // 当前任务的宽度
    ) {
        // ★ 核心逻辑：智能跳过 ★
        // 如果当前细分粒度比上一帧结束时的粒度还要粗，说明这个区域上一帧已经画过更好的点了。
        // 此时我们不生成点（避免覆盖高清旧点），只进行细分计算。
        // 只有当细分粒度 <= 上一帧粒度时，才开始写入新点（此时 Web 端会清除旧点）。
        bool skip_writing_points = (current_threshold_w > g_last_finished_threshold);

        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, current_tasks.size()),
            [&](const oneapi::tbb::blocked_range<size_t>& range) {

                // 检查取消标记 (虽然 parallel_for 内部不能直接 return 外层，但可以快速跳过)
                if (g_cancel_compute.load(std::memory_order_relaxed)) return;

                auto& local_next_tasks = next_tasks_tls.local();
                if (local_next_tasks.capacity() < 128) local_next_tasks.reserve(256);

                Interval<T> ix, iy;
                MultiInterval<T> res;

                for (size_t i = range.begin(); i != range.end(); ++i) {
                    if (g_cancel_compute.load(std::memory_order_relaxed)) break;

                    const auto& task = current_tasks[i];

                    // 构建区间
                    ix = Interval<T>(task.x, task.x + task.w);
                    T y_bottom = task.y + task.h;
                    if (task.y < y_bottom) iy = Interval<T>(task.y, y_bottom);
                    else iy = Interval<T>(y_bottom, task.y);

                    res = evaluate_rpn_fast_local<T>(rpn.program, ix, iy);

                    if (!res.contains_zero()) continue;

                    // --- 只有在精度足够细时，才写入点 ---
                    if (!skip_writing_points) {
                        double cx_dbl = to_double(task.x + task.w * 0.5);
                        double cy_dbl = to_double(task.y + task.h * 0.5);

                        int px = (int)((cx_dbl - to_double(world_origin.x)) / wppx);
                        int py = (int)((cy_dbl - to_double(world_origin.y)) / wppy);

                        if (px >= 0 && px < sw && py >= 0 && py < sh) {
                            size_t p_idx = (size_t)py * sw + px;
                            // 简单的原子去重
                            if (pixel_counts[p_idx] <= 1) {
                                if (ATOMIC_INC_U8(&pixel_counts[p_idx]) <= 1) {
                                    size_t write_idx = g_points_atomic_index.fetch_add(1, std::memory_order_relaxed);
                                    if (write_idx < buffer_capacity) {
                                        double final_x = cx_dbl - offset_x;
                                        double final_y = cy_dbl - offset_y;
                                        raw_buffer_ptr[write_idx] = PointData{ {final_x, final_y}, func_idx };
                                    }
                                }
                            }
                        }
                    }

                    // B. 任务分裂 (条件不变)
                    if (std::abs(to_double(task.w)) > next_split_threshold_w) {
                        T half_w = task.w * 0.5;
                        T half_h = task.h * 0.5;
                        local_next_tasks.push_back({task.x,          task.y,          half_w, half_h});
                        local_next_tasks.push_back({task.x + half_w, task.y,          half_w, half_h});
                        local_next_tasks.push_back({task.x,          task.y + half_h, half_w, half_h});
                        local_next_tasks.push_back({task.x + half_w, task.y + half_h, half_w, half_h});
                    }
                }
            }
        );
    }

    template<typename T>
    void execute_industry_fast(
        const IndustrialRPN& rpn,
        unsigned int func_idx,
        const Vec2& world_origin,
        double wppx, double wppy,
        double screen_width, double screen_height,
        double offset_x, double offset_y,
        size_t start_global_index
    ) {
        PointData* raw_buffer_ptr = wasm_final_contiguous_buffer.data();
        size_t buffer_capacity = wasm_final_contiguous_buffer.size();

        int sw = (int)screen_width + 1;
        int sh = (int)screen_height + 1;
        size_t needed_pixel_size = (size_t)sw * sh;

        if (g_cached_pixel_counts.size() < needed_pixel_size) {
            g_cached_pixel_counts.resize(needed_pixel_size);
        }
        // 每次重新计算都要清空像素计数，因为坐标系变了
        std::memset(g_cached_pixel_counts.data(), 0, needed_pixel_size * sizeof(uint8_t));

        std::vector<IndustryTask<T>> current_tasks;
        int grid_x = 16; int grid_y = 16;
        T total_w = T(screen_width * wppx);
        T total_h = T(screen_height * wppy);
        T step_w = total_w / T(grid_x);
        T step_h = total_h / T(grid_y);
        T start_x = T(world_origin.x);
        T start_y = T(world_origin.y);

        current_tasks.reserve(grid_x * grid_y);
        for (int gy = 0; gy < grid_y; ++gy) {
            for (int gx = 0; gx < grid_x; ++gx) {
                current_tasks.push_back({
                    start_x + step_w * T(gx), start_y + step_h * T(gy), step_w, step_h
                });
            }
        }

        oneapi::tbb::enumerable_thread_specific<std::vector<IndustryTask<T>>> next_tasks_tls;

        double current_threshold = rpn.start_pixel_threshold; // 比如 10.0
        double min_threshold = rpn.min_pixel_threshold;       // 比如 1.0
        double step_factor = rpn.step_factor > 1.0 ? rpn.step_factor : 2.0;

        while (!current_tasks.empty()) {
            // 每一轮细分前，检查是否取消
            if (g_cancel_compute.load(std::memory_order_relaxed)) return;

            // 扩容检查 ... (保持不变)
            size_t current_used = g_points_atomic_index.load(std::memory_order_relaxed);
            size_t tasks_count = current_tasks.size();
            bool need_grow = (current_used > buffer_capacity * 0.8) || ((buffer_capacity > current_used) && (buffer_capacity - current_used < tasks_count * 4));
            if (need_grow && buffer_capacity < HARD_LIMIT_POINTS) {
                size_t new_capacity = static_cast<size_t>(buffer_capacity * 1.5);
                if (new_capacity < current_used + tasks_count * 8) new_capacity = current_used + tasks_count * 8;
                if (new_capacity > HARD_LIMIT_POINTS) new_capacity = HARD_LIMIT_POINTS;
                if (new_capacity > buffer_capacity) {
                    wasm_final_contiguous_buffer.resize(new_capacity);
                    raw_buffer_ptr = wasm_final_contiguous_buffer.data();
                    buffer_capacity = wasm_final_contiguous_buffer.size();
                }
            }

            for (auto& local : next_tasks_tls) local.clear();

            double next_threshold_val = current_threshold / step_factor;
            if (next_threshold_val < min_threshold) next_threshold_val = min_threshold;
            double split_limit_world = (current_threshold <= min_threshold) ? 1e9 : std::abs(next_threshold_val * wppx);

            // 执行并行任务
            // 传入 current_threshold，内部会对比 g_last_finished_threshold
            process_tasks_parallel<T>(
                current_tasks, next_tasks_tls, rpn, func_idx,
                wppx, wppy, world_origin, g_cached_pixel_counts.data(), sw, sh,
                offset_x, offset_y, split_limit_world,
                raw_buffer_ptr, buffer_capacity,
                std::abs(to_double(step_w)) // 使用当前块的宽度近似作为阈值
            );

            // 如果这一轮被取消，直接退出
            if (g_cancel_compute.load(std::memory_order_relaxed)) return;

            // 成功完成一轮细分，更新全局索引范围
            size_t current_global_index = g_points_atomic_index.load(std::memory_order_relaxed);
            if (current_global_index > buffer_capacity) current_global_index = buffer_capacity;

            #ifdef __EMSCRIPTEN__
            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            size_t count = (current_global_index >= start_global_index) ? (current_global_index - start_global_index) : 0;
            wasm_function_ranges_buffer[func_idx] = {(uint32_t)start_global_index, (uint32_t)count};
            #endif

            // 通知前端更新画面
            g_industry_stage_version.fetch_add(1, std::memory_order_release);

            // 更新本轮达到的精度
            g_last_finished_threshold = current_threshold;

            if (current_threshold <= min_threshold) break;
            current_threshold = next_threshold_val;
            step_w = step_w / T(step_factor); // 更新下一步的宽度参考

            size_t total_next = 0;
            for (const auto& local : next_tasks_tls) total_next += local.size();
            if (total_next == 0) break;

            std::vector<IndustryTask<T>> next_phase_tasks;
            next_phase_tasks.reserve(total_next);
            for (auto& local : next_tasks_tls) {
                next_phase_tasks.insert(next_phase_tasks.end(), local.begin(), local.end());
            }
            current_tasks = std::move(next_phase_tasks);
        }
    }
}

// 暴露给外部的取消函数
void cancel_industry_calculation() {
    g_cancel_compute.store(true, std::memory_order_release);
}

// 入口函数
void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    std::lock_guard<std::mutex> lock(g_industry_mutex);

    // 重置取消标记，开始新计算
    g_cancel_compute.store(false, std::memory_order_release);

    IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);
    size_t start_index = g_points_atomic_index.load(std::memory_order_relaxed);

    if (rpn.precision_bits == 0) {
        execute_industry_fast<double>(rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index);
    } else {
        hp_float::default_precision(rpn.precision_bits);
        execute_industry_fast<hp_float>(rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index);
    }

    // 确保 buffer 大小正确
    size_t final_count = g_points_atomic_index.load();
    if (final_count > wasm_final_contiguous_buffer.size()) wasm_final_contiguous_buffer.resize(final_count);

    results_queue->push({func_idx, {}});
}