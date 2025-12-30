// --- 文件路径: src/plot/plotIndustry.cpp ---

#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include "../../include/interval/MultiInterval.h"
#include "../../include/CAS/RPN/MultiIntervalRPN.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/task_group.h>

#include <vector>
#include <atomic>
#include <cstring>
#include <cmath>
#include <iostream>
#include <mutex>
#include <iterator>
#include <array>
#include <functional>
#include <chrono>

// =========================================================
// 平台兼容性原子操作宏
// =========================================================
#if defined(_MSC_VER)
    #include <windows.h>
    #define ATOMIC_FETCH_INC_U8(ptr) _InterlockedExchangeAdd8((char*)(ptr), 1)
#else
    #define ATOMIC_FETCH_INC_U8(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#endif

// =========================================================
// 全局变量引用
// =========================================================
extern std::atomic<int> g_industry_stage_version;
extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;

static AlignedVector<uint8_t> g_pixel_density_map;
static std::mutex g_pixel_map_mutex;

// =========================================================
// 回调机制实现
// =========================================================
static std::function<void()> g_stage_callback = nullptr;
static std::mutex g_callback_mutex;

void SetIndustryStageCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_stage_callback = callback;
    std::cout << "[PlotIndustry] Callback registered." << std::endl;
}

void TriggerStageCallback() {
    std::function<void()> callback_copy;
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        callback_copy = g_stage_callback;
    }
    if (callback_copy) {
        callback_copy();
    }
}

// =========================================================
// ★★★ 仅保留视图看门狗机制 ★★★
// =========================================================

static GlobalViewState g_target_view_state = {0, 0, -1.0, 0, 0};
static std::mutex g_view_state_mutex;

void UpdateTargetViewState(double ox, double oy, double zoom, double w, double h) {
    std::lock_guard<std::mutex> lock(g_view_state_mutex);
    g_target_view_state = {ox, oy, zoom, w, h};
    std::cout << "[Watchdog] Target Updated: Offset=(" << ox << "," << oy << ") Zoom=" << zoom << std::endl;
}

// 核心检查函数：如果是 true，说明当前计算任务已经没用了，应该立即自杀
bool IsViewOutdated(double ox, double oy, double zoom, double w, double h) {


    std::lock_guard<std::mutex> lock(g_view_state_mutex);
    if (g_target_view_state.zoom != zoom) return true;
    if (g_target_view_state.offset_x != ox) return true;
    if (g_target_view_state.offset_y != oy) return true;
    if (g_target_view_state.width != w) return true;
    if (g_target_view_state.height != h) return true;
    return false;
}
void cancel_industry_calculation() {

}

namespace {

    template<typename T> struct IndustryTask { T x; T y; T abs_half_w; T abs_half_h; };

    template<typename T>
    double cast_to_double(const T& val) { return static_cast<double>(val); }

    template<typename T>
    bool is_val_finite(const T& v) {
        if constexpr (std::is_same_v<T, double>) return std::isfinite(v);
        else { using boost::multiprecision::isfinite; return isfinite(v); }
    }

    template<typename T>
    T evaluate_rpn_scalar_local(const AlignedVector<RPNToken>& prog, const T& x_val, const T& y_val) {
        std::array<T, 64> stack; int sp = 0;
        using std::sin; using std::cos; using std::tan; using std::exp; using std::log; using std::abs; using std::pow;
        for (const auto& token : prog) {
            if (sp>=63 && token.type <= RPNTokenType::PUSH_T) return T(0);
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = T(token.value); break;
                case RPNTokenType::PUSH_X: stack[sp++] = x_val; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = y_val; break;
                case RPNTokenType::PUSH_T: stack[sp++] = T(0); break;
                case RPNTokenType::ADD: sp--; stack[sp-1] = stack[sp-1] + stack[sp]; break;
                case RPNTokenType::SUB: sp--; stack[sp-1] = stack[sp-1] - stack[sp]; break;
                case RPNTokenType::MUL: sp--; stack[sp-1] = stack[sp-1] * stack[sp]; break;
                case RPNTokenType::DIV: sp--; stack[sp-1] = stack[sp-1] / stack[sp]; break;
                case RPNTokenType::POW: sp--; stack[sp-1] = pow(stack[sp-1], stack[sp]); break;
                case RPNTokenType::SIN: stack[sp-1] = sin(stack[sp-1]); break;
                case RPNTokenType::COS: stack[sp-1] = cos(stack[sp-1]); break;
                case RPNTokenType::TAN: stack[sp-1] = tan(stack[sp-1]); break;
                case RPNTokenType::EXP: stack[sp-1] = exp(stack[sp-1]); break;
                case RPNTokenType::LN:  stack[sp-1] = log(stack[sp-1]); break;
                case RPNTokenType::ABS: stack[sp-1] = abs(stack[sp-1]); break;
                case RPNTokenType::SIGN: if(stack[sp-1]>T(0)) stack[sp-1]=T(1); else if(stack[sp-1]<T(0)) stack[sp-1]=T(-1); else stack[sp-1]=T(0); break;
                default: break;
            }
        }
        return sp==0 ? T(0) : stack[sp-1];
    }

    template<typename T>
    MultiInterval<T> evaluate_rpn_fast_local(const AlignedVector<RPNToken>& prog, const Interval<T>& x, const Interval<T>& y) {
        MultiInterval<T> stack[64]; int sp = 0; MultiInterval<T> mx(x), my(y);
        for (const auto& token : prog) {
            if (sp>=63 && token.type <= RPNTokenType::PUSH_T) return MultiInterval<T>();
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = MultiInterval<T>(T(token.value)); break;
                case RPNTokenType::PUSH_X: stack[sp++] = mx; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = my; break;
                case RPNTokenType::PUSH_T: stack[sp++] = MultiInterval<T>(T(0)); break;
                case RPNTokenType::ADD: sp--; stack[sp-1] = stack[sp-1] + stack[sp]; break;
                case RPNTokenType::SUB: sp--; stack[sp-1] = stack[sp-1] - stack[sp]; break;
                case RPNTokenType::MUL: sp--; stack[sp-1] = stack[sp-1] * stack[sp]; break;
                case RPNTokenType::DIV: sp--; stack[sp-1] = stack[sp-1] / stack[sp]; break;
                case RPNTokenType::POW: sp--; stack[sp-1] = multi_pow(stack[sp-1], stack[sp]); break;
                case RPNTokenType::SIN: stack[sp-1] = multi_sin(stack[sp-1]); break;
                case RPNTokenType::COS: stack[sp-1] = multi_cos(stack[sp-1]); break;
                case RPNTokenType::TAN: stack[sp-1] = multi_tan(stack[sp-1]); break;
                case RPNTokenType::EXP: stack[sp-1] = multi_exp(stack[sp-1]); break;
                case RPNTokenType::LN:  stack[sp-1] = multi_ln(stack[sp-1]); break;
                case RPNTokenType::ABS: stack[sp-1] = multi_abs(stack[sp-1]); break;
                case RPNTokenType::SQRT: stack[sp-1] = multi_pow(stack[sp-1], MultiInterval<T>(T(0.5))); break;
                default: break;
            }
        }
        return sp==0 ? MultiInterval<T>() : stack[sp-1];
    }

    struct ParsedConfig { std::string rpn_str; unsigned int precision = 0; };
    ParsedConfig parse_config_simple(const std::string& input) {
        ParsedConfig config; std::stringstream ss(input); std::string segment;
        if (std::getline(ss, segment, ';')) config.rpn_str = segment;
        if (std::getline(ss, segment, ';')) try { config.precision = std::stoul(segment); } catch(...) { config.precision = 0; }
        return config;
    }

    // =========================================================
    // 核心计算逻辑 (纯 Watchdog 模式)
    // =========================================================
    template<typename T>
    void execute_native_solver(
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
        const std::string& rpn_str,
        unsigned int func_idx,
        const Vec2& world_origin,
        double wppx, double wppy,
        double screen_width, double screen_height,
        double offset_x, double offset_y,
        double zoom,
        unsigned int precision_bits
    ) {
        std::cout << "[Plot] Solver started. Zoom=" << zoom << " Offset=(" << offset_x << "," << offset_y << ")" << std::endl;

        AlignedVector<RPNToken> prog = parse_rpn(rpn_str);

        double initial_box_pixels = 32.0;
        T step_x = T(wppx * initial_box_pixels);
        T step_y = T(wppy * initial_box_pixels);
        using std::abs; using boost::multiprecision::abs;
        T initial_abs_half_w = abs(step_x) * 0.5;
        T initial_abs_half_h = abs(step_y) * 0.5;
        T termination_w = abs(T(wppx));

        int sw = static_cast<int>(screen_width);
        int sh = static_cast<int>(screen_height);
        size_t total_pixels = static_cast<size_t>(sw * sh);
        {
            std::lock_guard<std::mutex> lock(g_pixel_map_mutex);
            if (g_pixel_density_map.size() < total_pixels) g_pixel_density_map.resize(total_pixels);
        }
        uint8_t* pixel_map_ptr = g_pixel_density_map.data();

        std::vector<IndustryTask<T>> current_tasks;
        int grid_cols = static_cast<int>(std::ceil(screen_width / initial_box_pixels)) + 1;
        int grid_rows = static_cast<int>(std::ceil(screen_height / initial_box_pixels)) + 1;
        current_tasks.reserve(grid_cols * grid_rows);

        T origin_x = T(world_origin.x);
        T origin_y = T(world_origin.y);
        T start_center_x = origin_x + step_x * 0.5;
        T start_center_y = origin_y + step_y * 0.5;

        for (int r = 0; r < grid_rows; ++r) {
            T row_center_y = start_center_y + T(r) * step_y;
            for (int c = 0; c < grid_cols; ++c) {
                current_tasks.push_back({ start_center_x + T(c) * step_x, row_center_y, initial_abs_half_w, initial_abs_half_h });
            }
        }

        std::vector<PointData> final_points;
        oneapi::tbb::enumerable_thread_specific<std::vector<PointData>> ets_new_points;
        oneapi::tbb::enumerable_thread_specific<std::vector<IndustryTask<T>>> ets_next_tasks;

        int stage_counter = 0;
        int last_stage_counter = 0;
        const int MAX_DEPTH = 100;
        auto last_check_time = std::chrono::steady_clock::now();
        bool is_cancelled = false;

        while (!current_tasks.empty()) {
            std::cout << "[Watchdogggggggg] Target Is: Offset=(" << offset_x << "," << offset_y << ") Zoom=" << zoom << std::endl;
            stage_counter++;

            // ★★★ Checkpoint 1: 仅依赖 Watchdog ★★★
            // 如果全局目标变了，我就不是被需要的那个了，自杀。
            if (IsViewOutdated(offset_x, offset_y, zoom, screen_width, screen_height)) {
                std::cout << "[Watchdog] View Changed (Stage " << stage_counter << "). Aborting." << std::endl;
                is_cancelled = true;
                break;
            }

            ets_next_tasks.clear();
            ets_new_points.clear();
            if (total_pixels > 0) std::memset(pixel_map_ptr, 0, total_pixels * sizeof(uint8_t));

            // 并行计算
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, current_tasks.size()),
                [&](const oneapi::tbb::blocked_range<size_t>& range) {
                    // ★★★ Checkpoint 2: 循环内高频检查 ★★★
                    // 如果发现过期，直接 return，让循环空转结束
                    if (IsViewOutdated(offset_x, offset_y, zoom, screen_width, screen_height)) return;

                    auto& local_next_tasks = ets_next_tasks.local();
                    auto& local_points = ets_new_points.local();
                    if (local_next_tasks.capacity() < 128) local_next_tasks.reserve(128);

                    for (size_t i = range.begin(); i != range.end(); ++i) {
                        // 细粒度检查：可以适度放宽，每几个任务检查一次，但这里直接检查也行
                        // IsViewOutdated 只是简单的 float 比较，消耗极低
                        if (IsViewOutdated(offset_x, offset_y, zoom, screen_width, screen_height)) return;

                        const auto& task = current_tasks[i];
                        Interval<T> ix(task.x - task.abs_half_w, task.x + task.abs_half_w);
                        Interval<T> iy(task.y - task.abs_half_h, task.y + task.abs_half_h);
                        MultiInterval<T> res = evaluate_rpn_fast_local(prog, ix, iy);

                        if (res.contains_zero()) {
                            bool has_point = false;
                            PointData pt;
                            pt.function_index = func_idx;
                            bool interpolated = false;
                            T min_x = task.x - task.abs_half_w; T max_x = task.x + task.abs_half_w;
                            T min_y = task.y - task.abs_half_h; T max_y = task.y + task.abs_half_h;
                            T v00 = evaluate_rpn_scalar_local<T>(prog, min_x, min_y);
                            T v10 = evaluate_rpn_scalar_local<T>(prog, max_x, min_y);
                            T v01 = evaluate_rpn_scalar_local<T>(prog, min_x, max_y);
                            T v11 = evaluate_rpn_scalar_local<T>(prog, max_x, max_y);

                            if (is_val_finite(v00) && is_val_finite(v10) && is_val_finite(v01) && is_val_finite(v11)) {
                                auto sign = [](const T& v) { return (v > T(0)) - (v < T(0)); };
                                int s00 = sign(v00), s10 = sign(v10), s01 = sign(v01), s11 = sign(v11);
                                if (!(s00 == s10 && s10 == s01 && s01 == s11)) {
                                    auto try_lerp = [&](T val_a, T val_b, T c_a, T c_b, bool x_edge, T other) {
                                        if (val_a * val_b <= T(0)) {
                                            T t = (val_a == val_b) ? T(0.5) : val_a / (val_a - val_b);
                                            T interp = c_a + t * (c_b - c_a);
                                            if (x_edge) { pt.position.x = cast_to_double(interp) - offset_x; pt.position.y = cast_to_double(other) - offset_y; }
                                            else { pt.position.x = cast_to_double(other) - offset_x; pt.position.y = cast_to_double(interp) - offset_y; }
                                            interpolated = true;
                                        }
                                    };
                                    if (!interpolated) try_lerp(v00, v10, min_x, max_x, true, min_y);
                                    if (!interpolated) try_lerp(v01, v11, min_x, max_x, true, max_y);
                                    if (!interpolated) try_lerp(v00, v01, min_y, max_y, false, min_x);
                                    if (!interpolated) try_lerp(v10, v11, min_y, max_y, false, max_x);
                                }
                            }
                            if (interpolated) { has_point = true; }
                            else { pt.position.x = cast_to_double(task.x) - offset_x; pt.position.y = cast_to_double(task.y) - offset_y; has_point = true; }

                            if (has_point) {
                                double scr_x = (pt.position.x + offset_x - cast_to_double(world_origin.x)) / wppx;
                                double scr_y = (pt.position.y + offset_y - cast_to_double(world_origin.y)) / wppy;
                                int px = static_cast<int>(scr_x); int py = static_cast<int>(scr_y);
                                bool keep = true;
                                if (px >= 0 && px < sw && py >= 0 && py < sh) {
                                    size_t p_idx = static_cast<size_t>(py) * sw + px;
                                    char old_count = ATOMIC_FETCH_INC_U8(&pixel_map_ptr[p_idx]);
                                    if (old_count >= 2) keep = false;
                                }
                                if (keep) local_points.push_back(pt);
                            }

                            if (task.abs_half_w * 2.0 > termination_w) {
                                T new_hw = task.abs_half_w * 0.5; T new_hh = task.abs_half_h * 0.5;
                                local_next_tasks.push_back({task.x - new_hw, task.y - new_hh, new_hw, new_hh});
                                local_next_tasks.push_back({task.x + new_hw, task.y - new_hh, new_hw, new_hh});
                                local_next_tasks.push_back({task.x - new_hw, task.y + new_hh, new_hw, new_hh});
                                local_next_tasks.push_back({task.x + new_hw, task.y + new_hh, new_hw, new_hh});
                            }
                        }
                    }
                }
            );

            // ★★★ Checkpoint 3: Stage 后再次检查 ★★★
            if (IsViewOutdated(offset_x, offset_y, zoom, screen_width, screen_height)) {
                std::cout << "[Watchdog] View Changed (Post-Stage). Aborting." << std::endl;
                is_cancelled = true;
                break;
            }
            std::cout << "test" << std::endl;

            // 定时 Watchdog (10ms check)
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check_time).count() > 10) {
                if (IsViewOutdated(offset_x, offset_y, zoom, screen_width, screen_height)) {
                    is_cancelled = true;
                    break;
                }
                last_check_time = now;
            }

            // 汇总结果
            std::vector<PointData> current_stage_points;
            for (auto& lv : ets_new_points) {
                current_stage_points.insert(current_stage_points.end(),
                    std::make_move_iterator(lv.begin()),
                    std::make_move_iterator(lv.end()));
            }

            if (!current_stage_points.empty()) {
                final_points = std::move(current_stage_points);

                std::cout << "[Plot] Stage " << stage_counter
                          << " found " << final_points.size() << " points. REPLACING Buffer..." << std::endl;

                if (final_points.size() > wasm_final_contiguous_buffer.capacity()) {
                    wasm_final_contiguous_buffer.reserve(final_points.size() * 1.5);
                }

                wasm_final_contiguous_buffer.assign(final_points.begin(), final_points.end());

                if (wasm_function_ranges_buffer.size() <= func_idx) {
                    wasm_function_ranges_buffer.resize(func_idx + 1);
                }
                wasm_function_ranges_buffer[func_idx] = {0, (uint32_t)final_points.size()};

                TriggerStageCallback();
            }

            current_tasks.clear();
            for (auto& lv : ets_next_tasks) {
                current_tasks.insert(current_tasks.end(), lv.begin(), lv.end());
            }
        }

        if (!is_cancelled) {
            std::cout << "[Plot] Finished successfully." << std::endl;
        } else {
            std::cout << "[Plot] Job cancelled. Pushing empty result to unblock consumer." << std::endl;
        }

        results_queue->push({func_idx, std::vector<PointData>()});
    }

} // namespace anonymous

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y,
    double zoom
) {
    ParsedConfig config = parse_config_simple(industry_rpn);
    if (config.precision == 0) {
        execute_native_solver<double>(results_queue, config.rpn_str, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, zoom, 53);
    } else {
        hp_float::default_precision(config.precision);
        execute_native_solver<hp_float>(results_queue, config.rpn_str, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, zoom, config.precision);
    }
}