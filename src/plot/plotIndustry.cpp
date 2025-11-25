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

// =========================================================
//        ↓↓↓ 全局变量引用 ↓↓↓
// =========================================================

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;
extern std::atomic<int> g_industry_stage_version;
extern AlignedVector<PointData> g_preserved_points;

extern std::atomic<size_t> g_points_atomic_index;

#if defined(_MSC_VER)
    #include <windows.h>
    #define ATOMIC_INC_U8(ptr) (_InterlockedExchangeAdd8((char*)(ptr), 1) + 1)
#else
    #define ATOMIC_INC_U8(ptr) (__atomic_add_fetch((ptr), 1, __ATOMIC_RELAXED))
#endif

namespace {
    // 【内存优化】将最大点数限制在 150万 (约 35MB)
    // 对于 2K 屏幕，这已经足够覆盖极其复杂的曲线
    constexpr size_t MAX_POINTS_CAPACITY = 5000000;

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

    template<typename T>
    void log_task_info(const char* stage, int task_idx, const IndustryTask<T>& task, const MultiInterval<T>& res, bool has_zero) {
        int count = g_debug_log_count.fetch_add(1);
        if (count < MAX_DEBUG_LOGS) {
            std::lock_guard<std::mutex> lock(g_debug_mutex);
            std::cout << "[DEBUG][" << stage << "] T" << task_idx
                      << " Sz:" << to_double(task.w)
                      << " Z:" << (has_zero ? "Y" : "N") << std::endl;
        }
    }

    // 本地极速 RPN 求值器 (保持不变)
    template<typename T>
    MultiInterval<T> evaluate_rpn_fast_local(
        const AlignedVector<RPNToken>& prog,
        const Interval<T>& x_val,
        const Interval<T>& y_val
    ) {
        MultiInterval<T> stack[32];
        int sp = 0;
        MultiInterval<T> mx(x_val);
        MultiInterval<T> my(y_val);

        for (const auto& token : prog) {
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = MultiInterval<T>(T(token.value)); break;
                case RPNTokenType::PUSH_X: stack[sp++] = mx; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = my; break;
                case RPNTokenType::PUSH_T: stack[sp++] = MultiInterval<T>(T(0)); break;
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
}

// =========================================================
//        ↓↓↓ 核心处理逻辑 ↓↓↓
// =========================================================
namespace {

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
                                size_t write_idx = g_points_atomic_index.fetch_add(1, std::memory_order_relaxed);

                                // 【内存安全】严格检查边界，防止溢出
                                if (write_idx < buffer_capacity) {
                                    double final_x = cx_dbl - offset_x;
                                    double final_y = cy_dbl - offset_y;
                                    raw_buffer_ptr[write_idx] = PointData{ {final_x, final_y}, func_idx };
                                } else {
                                    // 缓冲区已满，停止写入（但可能继续分裂以保持精度，或者直接放弃）
                                    // 这里选择继续分裂，因为下一阶段可能会有更准确的点（虽然写不进去了）
                                    // 实际上如果满了，通常意味着点太密了，不写也罢。
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
        double offset_x, double offset_y
    ) {
        // 1. 预分配 (限制最大容量)
        if (wasm_final_contiguous_buffer.capacity() < MAX_POINTS_CAPACITY) {
            wasm_final_contiguous_buffer.reserve(MAX_POINTS_CAPACITY);
        }
        // 确保 size 足够大以允许指针访问
        if (wasm_final_contiguous_buffer.size() < MAX_POINTS_CAPACITY) {
            wasm_final_contiguous_buffer.resize(MAX_POINTS_CAPACITY);
        }

        PointData* raw_buffer_ptr = wasm_final_contiguous_buffer.data();
        size_t buffer_capacity = wasm_final_contiguous_buffer.size();

        // 2. 初始化辅助结构
        int sw = (int)screen_width + 1;
        int sh = (int)screen_height + 1;
        static std::vector<uint8_t> pixel_counts;
        if (pixel_counts.size() < sw * sh) pixel_counts.resize(sw * sh);

        // 3. 初始任务划分 (保持 16x16)
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

        auto total_start = std::chrono::high_resolution_clock::now();
        int stage_idx = 0;

        while (!current_tasks.empty()) {
            auto stage_start = std::chrono::high_resolution_clock::now();

            // 重置状态
            g_points_atomic_index.store(0, std::memory_order_relaxed);
            std::memset(pixel_counts.data(), 0, pixel_counts.size() * sizeof(uint8_t));
            for (auto& local : next_tasks_tls) local.clear();

            double next_threshold_val = current_threshold / step_factor;
            if (next_threshold_val < min_threshold) next_threshold_val = min_threshold;
            double split_limit_world = (current_threshold <= min_threshold) ? 1e9 : std::abs(next_threshold_val * wppx);

            // 执行计算
            process_tasks_parallel<T>(
                current_tasks, next_tasks_tls, rpn, func_idx,
                wppx, wppy, world_origin, pixel_counts.data(), sw, sh,
                offset_x, offset_y, split_limit_world,
                raw_buffer_ptr, buffer_capacity
            );

            // 同步
            size_t points_count = g_points_atomic_index.load(std::memory_order_relaxed);
            // 如果超过容量，截断显示
            if (points_count > buffer_capacity) points_count = buffer_capacity;

            #ifdef __EMSCRIPTEN__
            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            wasm_function_ranges_buffer[func_idx] = {0, (uint32_t)points_count};
            #endif
            g_industry_stage_version.fetch_add(1, std::memory_order_release);

            auto stage_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - stage_start).count();
            std::cout << "[Stage " << stage_idx << "] Threshold " << current_threshold
                      << "px | Points: " << points_count << " | Time: " << stage_dur << " ms" << std::endl;

            if (current_threshold <= min_threshold) break;

            current_threshold = next_threshold_val;

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
        std::cout << "--- [Industry] Finished. Total Time: " << total_dur << " ms ---" << std::endl;
    }
} // namespace

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);

    // 确保 Buffer 初始化 (使用较小的容量)
    if (wasm_final_contiguous_buffer.size() < MAX_POINTS_CAPACITY) {
        wasm_final_contiguous_buffer.resize(MAX_POINTS_CAPACITY);
    }

    if (rpn.precision_bits == 0) {
        execute_industry_fast<double>(
            rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y
        );
    } else {
        hp_float::default_precision(rpn.precision_bits);
        execute_industry_fast<hp_float>(
            rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y
        );
    }

    // Native 调试用：Resize 到实际大小 (可选，为了节省 Native 内存)
    size_t final_count = g_points_atomic_index.load();
    if (final_count < wasm_final_contiguous_buffer.size()) {
        wasm_final_contiguous_buffer.resize(final_count);
    }

    results_queue->push({func_idx, {}});
}