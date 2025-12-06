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
#include <array>

// 全局变量引用
extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern std::atomic<size_t> g_points_atomic_index;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;
extern std::atomic<int> g_industry_stage_version;

#if defined(_MSC_VER)
    #include <windows.h>
    #define ATOMIC_INC_U8(ptr) (_InterlockedExchangeAdd8((char*)(ptr), 1) + 1)
#else
    #define ATOMIC_INC_U8(ptr) (__atomic_add_fetch((ptr), 1, __ATOMIC_RELAXED))
#endif

namespace {
    // 【安全限制】硬物理上限 (约 1.2GB RAM)
    constexpr size_t HARD_LIMIT_POINTS = 50000000;

    // --- 新增：互斥锁，用于串行化多个工业级函数的计算，防止缓冲区写入交错 ---
    std::mutex g_industry_mutex;

    std::mutex g_debug_mutex;
    std::atomic<int> g_debug_log_count{0};
    const int MAX_DEBUG_LOGS = 5;

    template<typename T>
    double to_double(const T& val) { return static_cast<double>(val); }

    template<typename T>
    struct IndustryTask {
        T x, y;
        T w, h;
    };

    // 本地极速 RPN 求值器
    template<typename T>
    MultiInterval<T> evaluate_rpn_fast_local(
        const AlignedVector<RPNToken>& prog,
        const Interval<T>& x_val,
        const Interval<T>& y_val
    ) {
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
        size_t buffer_capacity
    ) {
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, current_tasks.size()),
            [&](const oneapi::tbb::blocked_range<size_t>& range) {

                auto& local_next_tasks = next_tasks_tls.local();
                if (local_next_tasks.capacity() < 128) local_next_tasks.reserve(256);

                Interval<T> ix, iy;
                MultiInterval<T> res;

                for (size_t i = range.begin(); i != range.end(); ++i) {
                    const auto& task = current_tasks[i];

                    ix = Interval<T>(task.x, task.x + task.w);
                    T y_bottom = task.y + task.h;
                    if (task.y < y_bottom) iy = Interval<T>(task.y, y_bottom);
                    else iy = Interval<T>(y_bottom, task.y);

                    res = evaluate_rpn_fast_local<T>(rpn.program, ix, iy);

                    if (!res.contains_zero()) continue;

                    // A. 写入点
                    double cx_dbl = to_double(task.x + task.w * 0.5);
                    double cy_dbl = to_double(task.y + task.h * 0.5);

                    int px = (int)((cx_dbl - to_double(world_origin.x)) / wppx);
                    int py = (int)((cy_dbl - to_double(world_origin.y)) / wppy);

                    if (px >= 0 && px < sw && py >= 0 && py < sh) {
                        size_t p_idx = (size_t)py * sw + px;

                        if (pixel_counts[p_idx] <= 2) {
                            if (ATOMIC_INC_U8(&pixel_counts[p_idx]) <= 2) {
                                // 原子获取写入位置
                                size_t write_idx = g_points_atomic_index.fetch_add(1, std::memory_order_relaxed);

                                // 安全写入
                                if (write_idx < buffer_capacity) {
                                    double final_x = cx_dbl - offset_x;
                                    double final_y = cy_dbl - offset_y;
                                    raw_buffer_ptr[write_idx] = PointData{ {final_x, final_y}, func_idx };
                                }
                            }
                        }
                    }

                    // B. 任务分裂
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
        size_t start_global_index // --- 新增参数：当前函数的起始写入位置 ---
    ) {
        // 1. 获取初始指针和容量
        PointData* raw_buffer_ptr = wasm_final_contiguous_buffer.data();
        size_t buffer_capacity = wasm_final_contiguous_buffer.size();

        // 2. 初始化辅助结构
        int sw = (int)screen_width + 1;
        int sh = (int)screen_height + 1;

        // --- 修改：移除 static，改为局部变量，避免不同函数间干扰 ---
        std::vector<uint8_t> pixel_counts(sw * sh, 0);

        // 3. 初始任务划分
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

        // 4. 阶段循环
        double current_threshold = rpn.start_pixel_threshold;
        double min_threshold = rpn.min_pixel_threshold;
        double step_factor = rpn.step_factor > 1.0 ? rpn.step_factor : 2.0;
        int stage_idx = 0;
        auto total_start = std::chrono::high_resolution_clock::now();

        while (!current_tasks.empty()) {
            auto stage_start = std::chrono::high_resolution_clock::now();

            // =========================================================
            // 1. 智能扩容检查
            // =========================================================
            size_t current_used = g_points_atomic_index.load(std::memory_order_relaxed);
            size_t tasks_count = current_tasks.size();

            bool need_grow = (current_used > buffer_capacity * 0.8) ||
                             ((buffer_capacity > current_used) && (buffer_capacity - current_used < tasks_count * 2));

            if (need_grow && buffer_capacity < HARD_LIMIT_POINTS) {
                size_t new_capacity = static_cast<size_t>(buffer_capacity * 1.5);
                if (new_capacity < current_used + tasks_count * 4) {
                    new_capacity = current_used + tasks_count * 4;
                }
                if (new_capacity > HARD_LIMIT_POINTS) new_capacity = HARD_LIMIT_POINTS;

                if (new_capacity > buffer_capacity) {
                    wasm_final_contiguous_buffer.resize(new_capacity);
                    raw_buffer_ptr = wasm_final_contiguous_buffer.data();
                    buffer_capacity = wasm_final_contiguous_buffer.size();
                }
            }

            // =========================================================
            // 2. 状态重置
            // --- 修改：移除了 g_points_atomic_index.store(0) ---
            // 这里的逻辑改为追加模式，不再每阶段重置缓冲区，保留粗糙阶段的点，
            // 既解决了多函数覆盖问题，也避免了单函数阶段间的数据丢失。
            // --- 修改：移除了 memset(pixel_counts) ---
            // 保留上一阶段的像素标记，避免同一位置重复绘制。
            // =========================================================

            // 清空 TLS 任务缓冲 (这是每轮循环必须的)
            for (auto& local : next_tasks_tls) local.clear();

            // =========================================================

            double next_threshold_val = current_threshold / step_factor;
            if (next_threshold_val < min_threshold) next_threshold_val = min_threshold;
            double split_limit_world = (current_threshold <= min_threshold) ? 1e9 : std::abs(next_threshold_val * wppx);

            // 执行并行计算
            process_tasks_parallel<T>(
                current_tasks, next_tasks_tls, rpn, func_idx,
                wppx, wppy, world_origin, pixel_counts.data(), sw, sh,
                offset_x, offset_y, split_limit_world,
                raw_buffer_ptr, buffer_capacity
            );

            // 同步进度给 JS
            size_t current_global_index = g_points_atomic_index.load(std::memory_order_relaxed);
            if (current_global_index > buffer_capacity) current_global_index = buffer_capacity;

            #ifdef __EMSCRIPTEN__
            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            // --- 修改：Range 计算逻辑，使用 start_global_index ---
            // 确保每个函数的 Range 是正确的 [start, length]，而不是 [0, total]
            size_t count = (current_global_index >= start_global_index) ? (current_global_index - start_global_index) : 0;
            wasm_function_ranges_buffer[func_idx] = {(uint32_t)start_global_index, (uint32_t)count};
            #endif
            g_industry_stage_version.fetch_add(1, std::memory_order_release);

            auto stage_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - stage_start).count();

            if (current_threshold <= min_threshold) break;

            current_threshold = next_threshold_val;

            // 收集下一阶段任务
            size_t total_next = 0;
            for (const auto& local : next_tasks_tls) total_next += local.size();

            if (total_next == 0) break;

            std::vector<IndustryTask<T>> next_phase_tasks;
            next_phase_tasks.reserve(total_next);
            for (auto& local : next_tasks_tls) {
                next_phase_tasks.insert(next_phase_tasks.end(), local.begin(), local.end());
            }
            current_tasks = std::move(next_phase_tasks);
            stage_idx++;
        }

        auto total_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - total_start).count();
        // std::cout << "--- [Industry] Finished. Total Time: " << total_dur << " ms ---" << std::endl;
    }
}

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    // --- 新增：加锁 ---
    // 强制串行化执行，确保多个工业级函数按顺序写入全局连续缓冲区。
    // 这解决了数据交错问题，使得 `FunctionRange` 可以正确表示一段连续的内存。
    std::lock_guard<std::mutex> lock(g_industry_mutex);

    IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);

    // --- 新增：捕获当前全局写入位置作为本函数的起始位置 ---
    size_t start_index = g_points_atomic_index.load(std::memory_order_relaxed);

    if (rpn.precision_bits == 0) {
        execute_industry_fast<double>(
            rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index
        );
    } else {
        hp_float::default_precision(rpn.precision_bits);
        execute_industry_fast<hp_float>(
            rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index
        );
    }

    // 确保缓冲区 size 正确 (虽然 execute 内部有 resize，但为了保险)
    size_t final_count = g_points_atomic_index.load();
    if (final_count < wasm_final_contiguous_buffer.size()) {
        // 只有当 vector 比 atomic 大很多时才需要 resize 减小？
        // 通常不需要 shrink，保持 capacity 即可
        // wasm_final_contiguous_buffer.resize(final_count);
        // 实际上 main.cpp 会处理最终 resize，这里可以不做，或者只 resize 确保数据可访问
        if (final_count > wasm_final_contiguous_buffer.size()) wasm_final_contiguous_buffer.resize(final_count);
    }

    // 发送完成信号 (数据已在 buffer 中)
    results_queue->push({func_idx, {}});
}