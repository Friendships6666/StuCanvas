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
#include <iomanip>

// 平台兼容性原子操作宏
#if defined(_MSC_VER)
    #include <windows.h>
    #define ATOMIC_INC_U8(ptr) (_InterlockedExchangeAdd8((char*)(ptr), 1) + 1)
#else
    #define ATOMIC_INC_U8(ptr) (__atomic_add_fetch((ptr), 1, __ATOMIC_RELAXED))
#endif

// 全局变量引用
extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern std::atomic<size_t> g_points_atomic_index;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;
extern std::atomic<int> g_industry_stage_version;

// --- 全局控制变量 ---
std::atomic<bool> g_cancel_compute{false};
double g_last_finished_threshold = 1e9;

// --- 调试辅助函数 ---
auto start_time_log = std::chrono::high_resolution_clock::now();
std::mutex log_mutex;

void log_debug(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_log).count();
    std::cout << "[CPP " << std::setw(6) << ms << "ms] " << msg << std::endl;
}

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

    // =========================================================
    //  新增: 本地标量求值函数 (用于四角检测)
    // =========================================================
    template<typename T>
    T evaluate_rpn_scalar_local(
        const AlignedVector<RPNToken>& prog,
        const T& x_val,
        const T& y_val
    ) {
        // 使用固定栈避免内存分配开销
        T stack[64];
        int sp = 0;

        // 引入数学函数 (支持 double 和 hp_float)
        using std::sin; using std::cos; using std::tan; using std::exp; using std::log; using std::abs; using std::pow; using std::sqrt;
        // 使用 ADL
        using boost::multiprecision::sin; using boost::multiprecision::cos;
        using boost::multiprecision::tan; using boost::multiprecision::exp;
        using boost::multiprecision::log; using boost::multiprecision::abs;
        using boost::multiprecision::pow; using boost::multiprecision::sqrt;

        for (const auto& token : prog) {
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = T(token.value); break;
                case RPNTokenType::PUSH_X: stack[sp++] = x_val; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = y_val; break;
                case RPNTokenType::ADD: { sp--; stack[sp-1] = stack[sp-1] + stack[sp]; break; }
                case RPNTokenType::SUB: { sp--; stack[sp-1] = stack[sp-1] - stack[sp]; break; }
                case RPNTokenType::MUL: { sp--; stack[sp-1] = stack[sp-1] * stack[sp]; break; }
                case RPNTokenType::DIV: { sp--; stack[sp-1] = stack[sp-1] / stack[sp]; break; }
                case RPNTokenType::POW: { sp--; stack[sp-1] = pow(stack[sp-1], stack[sp]); break; }
                case RPNTokenType::SIN: stack[sp-1] = sin(stack[sp-1]); break;
                case RPNTokenType::COS: stack[sp-1] = cos(stack[sp-1]); break;
                case RPNTokenType::TAN: stack[sp-1] = tan(stack[sp-1]); break;
                case RPNTokenType::EXP: stack[sp-1] = exp(stack[sp-1]); break;
                case RPNTokenType::LN:  stack[sp-1] = log(stack[sp-1]); break;
                case RPNTokenType::ABS: stack[sp-1] = abs(stack[sp-1]); break;
                case RPNTokenType::SIGN: {
                     if (stack[sp-1] > T(0)) stack[sp-1] = T(1);
                     else if (stack[sp-1] < T(0)) stack[sp-1] = T(-1);
                     else stack[sp-1] = T(0);
                     break;
                }
                default: break;
            }
        }
        if (sp == 0) return T(0);
        return stack[sp-1];
    }

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
        size_t buffer_capacity,
        bool skip_writing_points
    ) {
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, current_tasks.size()),
            [&](const oneapi::tbb::blocked_range<size_t>& range) {

                if (g_cancel_compute.load(std::memory_order_relaxed)) return;

                auto& local_next_tasks = next_tasks_tls.local();
                if (local_next_tasks.capacity() < 128) local_next_tasks.reserve(256);

                Interval<T> ix, iy;
                MultiInterval<T> res;

                // 定义一个检测有限性的 Lambda
                auto is_val_finite = [](const T& v) -> bool {
                    if constexpr (std::is_same_v<T, double>) {
                        return std::isfinite(v);
                    } else {
                        // 对于 hp_float，boost::multiprecision::isfinite 通常是可用的
                        using boost::multiprecision::isfinite;
                        return isfinite(v);
                    }
                };

                for (size_t i = range.begin(); i != range.end(); ++i) {
                    if (g_cancel_compute.load(std::memory_order_relaxed)) break;

                    const auto& task = current_tasks[i];

                    ix = Interval<T>(task.x, task.x + task.w);
                    T y_bottom = task.y + task.h;
                    if (task.y < y_bottom) iy = Interval<T>(task.y, y_bottom);
                    else iy = Interval<T>(y_bottom, task.y);

                    res = evaluate_rpn_fast_local<T>(rpn.program, ix, iy);

                    if (!res.contains_zero()) continue;

                    if (!skip_writing_points) {
                        bool wrote_interpolated = false;

                        // 1. 计算四个角的函数值 (强制 T 类型转换)
                        T v00 = evaluate_rpn_scalar_local<T>(rpn.program, task.x, task.y);
                        T v10 = evaluate_rpn_scalar_local<T>(rpn.program, T(task.x + task.w), task.y);
                        T v01 = evaluate_rpn_scalar_local<T>(rpn.program, task.x, T(task.y + task.h));
                        T v11 = evaluate_rpn_scalar_local<T>(rpn.program, T(task.x + task.w), T(task.y + task.h));

                        // 2. 检查是否所有值都是有限的 (非 NaN 非 Inf)
                        // 如果有任何一个值不正常，直接跳过插值，fall through 到中心点逻辑
                        bool all_values_valid = is_val_finite(v00) && is_val_finite(v10) &&
                                                is_val_finite(v01) && is_val_finite(v11);

                        if (all_values_valid) {
                            // 定义处理边的 Lambda
                            auto try_emit_edge = [&](T val1, T val2, T p1_coord, T p2_coord, bool is_x_axis, T other_coord_val) {
                                if (val1 * val2 <= T(0)) {
                                    T t_ratio;
                                    if (val1 == val2) t_ratio = T(0.5);
                                    else t_ratio = val1 / (val1 - val2);

                                    T interp_coord = p1_coord + t_ratio * (p2_coord - p1_coord);

                                    double final_world_x, final_world_y;
                                    if (is_x_axis) {
                                        final_world_x = to_double(interp_coord);
                                        final_world_y = to_double(other_coord_val);
                                    } else {
                                        final_world_x = to_double(other_coord_val);
                                        final_world_y = to_double(interp_coord);
                                    }

                                    int px = (int)((final_world_x - to_double(world_origin.x)) / wppx);
                                    int py = (int)((final_world_y - to_double(world_origin.y)) / wppy);

                                    if (px >= 0 && px < sw && py >= 0 && py < sh) {
                                        size_t p_idx = (size_t)py * sw + px;
                                        if (pixel_counts[p_idx] <= 2) {
                                            if (ATOMIC_INC_U8(&pixel_counts[p_idx]) <= 2) {
                                                size_t write_idx = g_points_atomic_index.fetch_add(1, std::memory_order_relaxed);
                                                if (write_idx < buffer_capacity) {
                                                    raw_buffer_ptr[write_idx] = PointData{ {final_world_x - offset_x, final_world_y - offset_y}, func_idx };
                                                }
                                            }
                                        }
                                    }
                                    wrote_interpolated = true;
                                }
                            };

                            // 检测四条边
                            try_emit_edge(v00, v10, task.x, T(task.x + task.w), true, task.y);
                            try_emit_edge(v01, v11, task.x, T(task.x + task.w), true, T(task.y + task.h));
                            try_emit_edge(v00, v01, task.y, T(task.y + task.h), false, task.x);
                            try_emit_edge(v10, v11, task.y, T(task.y + task.h), false, T(task.x + task.w));
                        }

                        // 3. 回退逻辑：如果插值没写入任何点，或者因为 NaN/Inf 跳过了插值
                        if (!wrote_interpolated) {
                            double cx_dbl = to_double(task.x + task.w * 0.5);
                            double cy_dbl = to_double(task.y + task.h * 0.5);

                            int px = (int)((cx_dbl - to_double(world_origin.x)) / wppx);
                            int py = (int)((cy_dbl - to_double(world_origin.y)) / wppy);

                            if (px >= 0 && px < sw && py >= 0 && py < sh) {
                                size_t p_idx = (size_t)py * sw + px;
                                if (pixel_counts[p_idx] <= 2) {
                                    if (ATOMIC_INC_U8(&pixel_counts[p_idx]) <= 2) {
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
                    }

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
        log_debug("EXEC START: execute_industry_fast");

        PointData* raw_buffer_ptr = wasm_final_contiguous_buffer.data();
        size_t buffer_capacity = wasm_final_contiguous_buffer.size();

        int sw = (int)screen_width + 1;
        int sh = (int)screen_height + 1;
        size_t needed_pixel_size = (size_t)sw * sh;

        if (g_cached_pixel_counts.size() < needed_pixel_size) {
            g_cached_pixel_counts.resize(needed_pixel_size);
        }

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

        double current_threshold = rpn.start_pixel_threshold;
        double min_threshold = rpn.min_pixel_threshold;
        double step_factor = rpn.step_factor > 1.0 ? rpn.step_factor : 2.0;
        int loop_count = 0;

        while (!current_tasks.empty()) {
            loop_count++;

            if (g_cancel_compute.load(std::memory_order_relaxed)) {
                log_debug("ABORT: Cancelled at Start of Loop " + std::to_string(loop_count));
                return;
            }

            // 动态扩容
            size_t current_used = g_points_atomic_index.load(std::memory_order_relaxed);
            size_t tasks_count = current_tasks.size();
            bool need_grow = (current_used > buffer_capacity * 0.8) ||
                             ((buffer_capacity > current_used) && (buffer_capacity - current_used < tasks_count * 4));

            if (need_grow && buffer_capacity < HARD_LIMIT_POINTS) {
                size_t new_capacity = static_cast<size_t>(buffer_capacity * 1.5);
                if (new_capacity < current_used + tasks_count * 8) new_capacity = current_used + tasks_count * 8;
                if (new_capacity > HARD_LIMIT_POINTS) new_capacity = HARD_LIMIT_POINTS;
                if (new_capacity > buffer_capacity) {
                    wasm_final_contiguous_buffer.resize(new_capacity);
                    raw_buffer_ptr = wasm_final_contiguous_buffer.data();
                    buffer_capacity = wasm_final_contiguous_buffer.size();
                    log_debug("BUFFER GROW: New Capacity " + std::to_string(new_capacity));
                }
            }

            // ==========================================================
            // ★★★ 状态检查与清空 ★★★
            // ==========================================================

            bool is_coarser = (current_threshold > g_last_finished_threshold);

            if (!is_coarser) {
                // Double Check Cancel Before Clear
                if (g_cancel_compute.load(std::memory_order_relaxed)) {
                    log_debug("ABORT: Cancelled RIGHT BEFORE CLEAR (Saved!)");
                    return; // Return without clearing
                }

                g_points_atomic_index.store(0, std::memory_order_relaxed);
                std::memset(g_cached_pixel_counts.data(), 0, needed_pixel_size * sizeof(uint8_t));
                log_debug("!!! BUFFER CLEARED !!! (New Frame Start)");
            }

            for (auto& local : next_tasks_tls) local.clear();

            double next_threshold_val = current_threshold / step_factor;
            if (next_threshold_val < min_threshold) next_threshold_val = min_threshold;
            double split_limit_world = (current_threshold <= min_threshold) ? 1e9 : std::abs(next_threshold_val * wppx);

            // TBB Parallel
            process_tasks_parallel<T>(
                current_tasks, next_tasks_tls, rpn, func_idx,
                wppx, wppy, world_origin, g_cached_pixel_counts.data(), sw, sh,
                offset_x, offset_y, split_limit_world,
                raw_buffer_ptr, buffer_capacity,
                is_coarser
            );

            if (g_cancel_compute.load(std::memory_order_relaxed)) {
                log_debug("ABORT: Cancelled immediately after TBB");
                return;
            }

            #ifdef __EMSCRIPTEN__
            size_t points_count = g_points_atomic_index.load(std::memory_order_relaxed);
            if (points_count > buffer_capacity) points_count = buffer_capacity;
            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            wasm_function_ranges_buffer[func_idx] = {0, (uint32_t)points_count};
            #endif
            g_industry_stage_version.fetch_add(1, std::memory_order_release);

            if (!is_coarser) {
                g_last_finished_threshold = current_threshold;
            }

            if (current_threshold <= min_threshold) {
                log_debug("EXEC DONE: Reached Min Threshold");
                break;
            }
            current_threshold = next_threshold_val;
            step_w = step_w / T(step_factor);

            size_t total_next = 0;
            for (const auto& local : next_tasks_tls) total_next += local.size();

            if (total_next == 0) {
                log_debug("EXEC DONE: No more tasks");
                break;
            }

            std::vector<IndustryTask<T>> next_phase_tasks;
            next_phase_tasks.reserve(total_next);
            for (auto& local : next_tasks_tls) {
                next_phase_tasks.insert(next_phase_tasks.end(), local.begin(), local.end());
            }
            current_tasks = std::move(next_phase_tasks);
        }
    }
}

void cancel_industry_calculation() {
    log_debug(">>> SIGNAL: Cancel requested via API");
    g_cancel_compute.store(true, std::memory_order_release);
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
    std::lock_guard<std::mutex> lock(g_industry_mutex);

    log_debug("ENTRY process_single. Resetting Cancel=False, Threshold=1e9");

    // ★★★ 核心修复：防止“卡死” ★★★
    g_cancel_compute.store(false, std::memory_order_release);

    // ★★★ 核心修复：防止 Zoom 后不显示 ★★★
    g_last_finished_threshold = 1e9;

    IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);
    size_t start_index = 0;

    if (rpn.precision_bits == 0) {
        execute_industry_fast<double>(rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index);
    } else {
        hp_float::default_precision(rpn.precision_bits);
        execute_industry_fast<hp_float>(rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, start_index);
    }

    size_t final_count = g_points_atomic_index.load();
    if (final_count > wasm_final_contiguous_buffer.size()) wasm_final_contiguous_buffer.resize(final_count);

    log_debug("EXIT process_single. Final Points: " + std::to_string(final_count));
    results_queue->push({func_idx, {}});
}