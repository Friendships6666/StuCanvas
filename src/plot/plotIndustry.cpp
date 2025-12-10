// --- 文件路径: src/plot/plotIndustry.cpp ---

#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include "../../include/interval/MultiInterval.h"
#include "../../include/CAS/RPN/MultiIntervalRPN.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/task_group.h> // 引入 task_group_context

#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <iterator>
#include <cstring>

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

// 定义全局上下文指针，用于外部终止
// 使用原子指针确保线程安全
std::atomic<oneapi::tbb::task_group_context*> g_active_context{nullptr};

namespace {

    AlignedVector<uint8_t> g_pixel_density_map;
    std::mutex g_pixel_map_mutex;

    template<typename T>
    struct IndustryTask {
        T x; T y; T abs_half_w; T abs_half_h;
    };

    // =========================================================
    // 本地标量求值 (保持不变)
    // =========================================================
    template<typename T>
    T evaluate_rpn_scalar_local(const AlignedVector<RPNToken>& prog, const T& x_val, const T& y_val) {
        T stack[64];
        int sp = 0;
        using std::sin; using std::cos; using std::tan; using std::exp; using std::log; using std::abs; using std::pow; using std::sqrt;
        using boost::multiprecision::sin; using boost::multiprecision::cos;
        using boost::multiprecision::tan; using boost::multiprecision::exp;
        using boost::multiprecision::log; using boost::multiprecision::abs;
        using boost::multiprecision::pow; using boost::multiprecision::sqrt;

        for (const auto& token : prog) {
            switch (token.type) {
                case RPNTokenType::PUSH_CONST: stack[sp++] = T(token.value); break;
                case RPNTokenType::PUSH_X: stack[sp++] = x_val; break;
                case RPNTokenType::PUSH_Y: stack[sp++] = y_val; break;
                case RPNTokenType::PUSH_T: stack[sp++] = T(0); break;
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

    // =========================================================
    // 本地 RPN 求值器 (保持不变)
    // =========================================================
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
        if (sp == 0) return MultiInterval<T>();
        return stack[sp-1];
    }

    struct ParsedConfig {
        std::string rpn_str;
        unsigned int precision = 0;
    };
    ParsedConfig parse_config_simple(const std::string& input) {
        ParsedConfig config;
        std::stringstream ss(input);
        std::string segment;
        if (std::getline(ss, segment, ';')) config.rpn_str = segment;
        if (std::getline(ss, segment, ';')) {
            try { config.precision = std::stoul(segment); } catch (...) { config.precision = 0; }
        }
        return config;
    }

    template<typename T>
    double to_double(const T& val) { return static_cast<double>(val); }

    // =========================================================
    // 核心计算逻辑模板 (集成 TBB Context)
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
        unsigned int precision_bits
    ) {
        // ----------------------------------------------------------------
        // ★★★ 1. 初始化 TBB 任务上下文 ★★★
        // ----------------------------------------------------------------
        // 创建新的上下文，不继承父级，确保独立控制
        oneapi::tbb::task_group_context task_ctx;

        // 注册到全局变量，供外部取消调用
        g_active_context.store(&task_ctx);

        // RAII Guard: 确保函数退出时清空全局指针
        struct ContextGuard {
            ~ContextGuard() { g_active_context.store(nullptr); }
        } _context_guard;

        // ----------------------------------------------------------------

        // 初始化 RPN
        AlignedVector<RPNToken> prog = parse_rpn(rpn_str);

        // 参数计算
        double initial_box_pixels = 32.0;
        T step_x = T(wppx * initial_box_pixels);
        T step_y = T(wppy * initial_box_pixels);

        using std::abs; using boost::multiprecision::abs;
        T initial_abs_half_w = abs(step_x) * 0.5;
        T initial_abs_half_h = abs(step_y) * 0.5;
        T termination_w = abs(T(wppx));

        // 像素密度映射表
        int sw = static_cast<int>(screen_width);
        int sh = static_cast<int>(screen_height);
        size_t total_pixels = static_cast<size_t>(sw * sh);
        {
            std::lock_guard<std::mutex> lock(g_pixel_map_mutex);
            if (g_pixel_density_map.size() < total_pixels) g_pixel_density_map.resize(total_pixels);
        }
        uint8_t* pixel_map_ptr = g_pixel_density_map.data();

        // 生成初始任务
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
                current_tasks.push_back({
                    start_center_x + T(c) * step_x,
                    row_center_y,
                    initial_abs_half_w,
                    initial_abs_half_h
                });
            }
        }

        // 3. 渐进式循环
        std::vector<PointData> final_points;
        oneapi::tbb::enumerable_thread_specific<std::vector<PointData>> ets_new_points;
        oneapi::tbb::enumerable_thread_specific<std::vector<IndustryTask<T>>> ets_next_tasks;

        auto cast_to_double = [](const T& val) { return static_cast<double>(val); };

        auto is_val_finite = [](const T& v) -> bool {
            if constexpr (std::is_same_v<T, double>) return std::isfinite(v);
            else { using boost::multiprecision::isfinite; return isfinite(v); }
        };

        while (!current_tasks.empty()) {

            // ★★★ 检查上下文是否已取消 ★★★
            if (task_ctx.is_group_execution_cancelled()) return;

            ets_next_tasks.clear();
            ets_new_points.clear();

            if (total_pixels > 0) std::memset(pixel_map_ptr, 0, total_pixels * sizeof(uint8_t));

            // --- 并行计算 ---
            // ★★★ 将上下文传递给 parallel_for ★★★
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<size_t>(0, current_tasks.size()),
                [&](const oneapi::tbb::blocked_range<size_t>& range) {

                    // 双重检查：如果已取消，立即停止当前 chunk
                    if (task_ctx.is_group_execution_cancelled()) return;

                    auto& local_next_tasks = ets_next_tasks.local();
                    auto& local_points = ets_new_points.local();
                    if (local_next_tasks.capacity() < 128) local_next_tasks.reserve(256);

                    for (size_t i = range.begin(); i != range.end(); ++i) {
                        const auto& task = current_tasks[i];

                        // 密度剔除
                        double cx_dbl = cast_to_double(task.x);
                        double cy_dbl = cast_to_double(task.y);
                        int px = static_cast<int>((cx_dbl - cast_to_double(world_origin.x)) / wppx);
                        int py = static_cast<int>((cy_dbl - cast_to_double(world_origin.y)) / wppy);

                        if (px >= 0 && px < sw && py >= 0 && py < sh) {
                            size_t p_idx = static_cast<size_t>(py) * sw + px;
                            char old_count = ATOMIC_FETCH_INC_U8(&pixel_map_ptr[p_idx]);
                            if (old_count >= 2) continue;
                        }

                        Interval<T> ix(task.x - task.abs_half_w, task.x + task.abs_half_w);
                        Interval<T> iy(task.y - task.abs_half_h, task.y + task.abs_half_h);

                        MultiInterval<T> res = evaluate_rpn_fast_local(prog, ix, iy);

                        if (res.contains_zero()) {
                            // 插值逻辑
                            bool interpolated = false;

                            T min_x = task.x - task.abs_half_w;
                            T max_x = task.x + task.abs_half_w;
                            T min_y = task.y - task.abs_half_h;
                            T max_y = task.y + task.abs_half_h;

                            T v00 = evaluate_rpn_scalar_local<T>(prog, min_x, min_y);
                            T v10 = evaluate_rpn_scalar_local<T>(prog, max_x, min_y);
                            T v01 = evaluate_rpn_scalar_local<T>(prog, min_x, max_y);
                            T v11 = evaluate_rpn_scalar_local<T>(prog, max_x, max_y);

                            if (is_val_finite(v00) && is_val_finite(v10) &&
                                is_val_finite(v01) && is_val_finite(v11))
                            {
                                auto sign = [](const T& v) { return (v > T(0)) - (v < T(0)); };
                                int s00 = sign(v00), s10 = sign(v10), s01 = sign(v01), s11 = sign(v11);

                                if (!(s00 == s10 && s10 == s01 && s01 == s11)) {
                                    auto try_lerp = [&](T val_a, T val_b, T coord_a, T coord_b, bool is_x_edge, T other_coord) {
                                        if (val_a * val_b <= T(0)) {
                                            T t = (val_a == val_b) ? T(0.5) : val_a / (val_a - val_b);
                                            T interp = coord_a + t * (coord_b - coord_a);

                                            PointData pt;
                                            if (is_x_edge) {
                                                pt.position.x = to_double(interp) - offset_x;
                                                pt.position.y = to_double(other_coord) - offset_y;
                                            } else {
                                                pt.position.x = to_double(other_coord) - offset_x;
                                                pt.position.y = to_double(interp) - offset_y;
                                            }
                                            pt.function_index = func_idx;
                                            local_points.push_back(pt);
                                            interpolated = true;
                                        }
                                    };

                                    try_lerp(v00, v10, min_x, max_x, true, min_y);
                                    try_lerp(v01, v11, min_x, max_x, true, max_y);
                                    try_lerp(v00, v01, min_y, max_y, false, min_x);
                                    try_lerp(v10, v11, min_y, max_y, false, max_x);
                                }
                            }

                            if (!interpolated) {
                                PointData pt;
                                pt.position.x = cx_dbl - offset_x;
                                pt.position.y = cy_dbl - offset_y;
                                pt.function_index = func_idx;
                                local_points.push_back(pt);
                            }

                            if (task.abs_half_w * 2.0 > termination_w) {
                                T new_half_w = task.abs_half_w * 0.5;
                                T new_half_h = task.abs_half_h * 0.5;

                                local_next_tasks.push_back({task.x - new_half_w, task.y - new_half_h, new_half_w, new_half_h});
                                local_next_tasks.push_back({task.x + new_half_w, task.y - new_half_h, new_half_w, new_half_h});
                                local_next_tasks.push_back({task.x - new_half_w, task.y + new_half_h, new_half_w, new_half_h});
                                local_next_tasks.push_back({task.x + new_half_w, task.y + new_half_h, new_half_w, new_half_h});
                            }
                        }
                    }
                },
                task_ctx // ★★★ 传递上下文 ★★★
            );

            // 再次检查取消，防止在汇总阶段浪费时间
            if (task_ctx.is_group_execution_cancelled()) return;

            // --- 阶段汇总 ---
            final_points.clear();

            size_t total_points_this_stage = 0;
            for (auto& local_vec : ets_new_points) total_points_this_stage += local_vec.size();
            final_points.reserve(total_points_this_stage);

            for (auto& local_vec : ets_new_points) {
                if (!local_vec.empty()) {
                    final_points.insert(
                        final_points.end(),
                        std::make_move_iterator(local_vec.begin()),
                        std::make_move_iterator(local_vec.end())
                    );
                    local_vec.clear();
                }
            }

            if (final_points.size() > wasm_final_contiguous_buffer.capacity()) {
                wasm_final_contiguous_buffer.reserve(final_points.size() * 2);
            }
            wasm_final_contiguous_buffer.assign(final_points.begin(), final_points.end());

            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            wasm_function_ranges_buffer[func_idx] = {0, (uint32_t)final_points.size()};

            g_industry_stage_version.fetch_add(1, std::memory_order_release);

            current_tasks.clear();
            size_t total_next = 0;
            for (const auto& local : ets_next_tasks) total_next += local.size();

            if (total_next > 0) {
                current_tasks.reserve(total_next);
                for (auto& local : ets_next_tasks) {
                    current_tasks.insert(current_tasks.end(), local.begin(), local.end());
                }
            }
        }

        if (!task_ctx.is_group_execution_cancelled()) {
            results_queue->push({func_idx, std::move(final_points)});
        }
    }

} // namespace anonymous

// =========================================================
// 外部接口实现
// =========================================================

// 【实现】取消功能
void cancel_industry_calculation() {
    auto* ctx = g_active_context.load();
    if (ctx) {
        ctx->cancel_group_execution();
    }
}

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
) {
    ParsedConfig config = parse_config_simple(industry_rpn);

    if (config.precision == 0) {
        execute_native_solver<double>(
            results_queue, config.rpn_str, func_idx,
            world_origin, wppx, wppy, screen_width, screen_height,
            offset_x, offset_y, 53
        );
    } else {
        hp_float::default_precision(config.precision);
        execute_native_solver<hp_float>(
            results_queue, config.rpn_str, func_idx,
            world_origin, wppx, wppy, screen_width, screen_height,
            offset_x, offset_y, config.precision
        );
    }
}