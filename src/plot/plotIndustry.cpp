// --- 文件路径: src/plot/plotIndustry.cpp ---

#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include "../../include/interval/MultiInterval.h"
#include "../../include/CAS/RPN/MultiIntervalRPN.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/parallel_reduce.h>

#include <vector>
#include <stack>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <array>
#include <chrono>
#include <cstring> // for memset

// --- WASM 特定定义 ---
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <atomic>

// 引用 main.cpp 中定义的全局变量
extern AlignedVector<PointData> wasm_final_contiguous_buffer;
extern AlignedVector<FunctionRange> wasm_function_ranges_buffer;
extern std::atomic<int> g_industry_stage_version;
#endif

// 跨平台原子操作宏，用于裸指针 (用于像素计数)
#if defined(_MSC_VER)
    #include <windows.h>
    #define ATOMIC_INC_U8(ptr) (_InterlockedExchangeAdd8((char*)(ptr), 1) + 1)
#else
    // GCC/Clang built-in
    #define ATOMIC_INC_U8(ptr) (__atomic_add_fetch((ptr), 1, __ATOMIC_RELAXED))
#endif

namespace {

// =========================================================
//              辅助结构
// =========================================================

template<typename T>
struct IndustryQuadtreeTaskT {
    T world_x, world_y;
    T world_w, world_h;
};

    template<typename T>
    inline double convert_to_double(const T& val) {
        return static_cast<double>(val);
    }

// =========================================================
//        ↓↓↓ 4路批量计算结构 (Batch-4) ↓↓↓
// =========================================================

template<typename T>
struct MultiIntervalBundle4 {
    MultiInterval<T> v[4];

    MultiIntervalBundle4() {}
    MultiIntervalBundle4(T val) {
        MultiInterval<T> m(val);
        v[0] = m; v[1] = m; v[2] = m; v[3] = m;
    }
    MultiIntervalBundle4(const Interval<T>& i) {
        MultiInterval<T> m(i);
        v[0] = m; v[1] = m; v[2] = m; v[3] = m;
    }
};

#define BUNDLE_OP(OP) \
    MultiIntervalBundle4<T> res; \
    res.v[0] = A.v[0] OP B.v[0]; \
    res.v[1] = A.v[1] OP B.v[1]; \
    res.v[2] = A.v[2] OP B.v[2]; \
    res.v[3] = A.v[3] OP B.v[3]; \
    return res;

#define BUNDLE_FUNC(FUNC) \
    MultiIntervalBundle4<T> res; \
    res.v[0] = FUNC(A.v[0]); \
    res.v[1] = FUNC(A.v[1]); \
    res.v[2] = FUNC(A.v[2]); \
    res.v[3] = FUNC(A.v[3]); \
    return res;

template<typename T> MultiIntervalBundle4<T> operator+(const MultiIntervalBundle4<T>& A, const MultiIntervalBundle4<T>& B) { BUNDLE_OP(+) }
template<typename T> MultiIntervalBundle4<T> operator-(const MultiIntervalBundle4<T>& A, const MultiIntervalBundle4<T>& B) { BUNDLE_OP(-) }
template<typename T> MultiIntervalBundle4<T> operator*(const MultiIntervalBundle4<T>& A, const MultiIntervalBundle4<T>& B) { BUNDLE_OP(*) }
template<typename T> MultiIntervalBundle4<T> operator/(const MultiIntervalBundle4<T>& A, const MultiIntervalBundle4<T>& B) { BUNDLE_OP(/) }

template<typename T> MultiIntervalBundle4<T> batch_pow(const MultiIntervalBundle4<T>& A, const MultiIntervalBundle4<T>& B) {
    MultiIntervalBundle4<T> res;
    res.v[0] = multi_pow(A.v[0], B.v[0]);
    res.v[1] = multi_pow(A.v[1], B.v[1]);
    res.v[2] = multi_pow(A.v[2], B.v[2]);
    res.v[3] = multi_pow(A.v[3], B.v[3]);
    return res;
}

template<typename T> MultiIntervalBundle4<T> batch_sin(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_sin) }
template<typename T> MultiIntervalBundle4<T> batch_cos(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_cos) }
template<typename T> MultiIntervalBundle4<T> batch_tan(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_tan) }
template<typename T> MultiIntervalBundle4<T> batch_exp(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_exp) }
template<typename T> MultiIntervalBundle4<T> batch_ln(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_ln) }
template<typename T> MultiIntervalBundle4<T> batch_abs(const MultiIntervalBundle4<T>& A) { BUNDLE_FUNC(multi_abs) }

// =========================================================
//  TLS RPN 栈 (Zero Allocation)
// =========================================================
template<typename T>
struct FixedStack {
    MultiIntervalBundle4<T> data[64];
    int size = 0;

    FORCE_INLINE void push(const MultiIntervalBundle4<T>& val) { data[size++] = val; }
    FORCE_INLINE void emplace_scalar(T val) { data[size++] = MultiIntervalBundle4<T>(val); }
    FORCE_INLINE MultiIntervalBundle4<T> pop() { return data[--size]; }
    FORCE_INLINE MultiIntervalBundle4<T>& top() { return data[size-1]; }
    FORCE_INLINE bool empty() const { return size == 0; }
    FORCE_INLINE void clear() { size = 0; }
};

template<typename T>
FixedStack<T>& get_tls_fixed_stack() {
    static thread_local FixedStack<T> stack;
    stack.clear();
    return stack;
}

template<typename T>
MultiIntervalBundle4<T> evaluate_rpn_batch4_optimized(
    const AlignedVector<RPNToken>& prog,
    const MultiIntervalBundle4<T>& x_bundle,
    const MultiIntervalBundle4<T>& y_bundle,
    const MultiIntervalBundle4<T>& t_bundle
) {
    FixedStack<T>& stack = get_tls_fixed_stack<T>();

    for (const auto& token : prog) {
        switch (token.type) {
            case RPNTokenType::PUSH_CONST: stack.emplace_scalar(T(token.value)); break;
            case RPNTokenType::PUSH_X: stack.push(x_bundle); break;
            case RPNTokenType::PUSH_Y: stack.push(y_bundle); break;
            case RPNTokenType::PUSH_T: stack.push(t_bundle); break;

            case RPNTokenType::ADD: { auto b = stack.pop(); stack.top() = stack.top() + b; break; }
            case RPNTokenType::SUB: { auto b = stack.pop(); stack.top() = stack.top() - b; break; }
            case RPNTokenType::MUL: { auto b = stack.pop(); stack.top() = stack.top() * b; break; }
            case RPNTokenType::DIV: { auto b = stack.pop(); stack.top() = stack.top() / b; break; }
            case RPNTokenType::POW: { auto b = stack.pop(); stack.top() = batch_pow(stack.top(), b); break; }
            case RPNTokenType::SIN: stack.top() = batch_sin(stack.top()); break;
            case RPNTokenType::COS: stack.top() = batch_cos(stack.top()); break;
            case RPNTokenType::TAN: stack.top() = batch_tan(stack.top()); break;
            case RPNTokenType::EXP: stack.top() = batch_exp(stack.top()); break;
            case RPNTokenType::LN:  stack.top() = batch_ln(stack.top()); break;
            case RPNTokenType::ABS: stack.top() = batch_abs(stack.top()); break;
            default: break;
        }
    }
    if (stack.empty()) return MultiIntervalBundle4<T>(T(0));
    return stack.top();
}

// =========================================================
//              分阶段核心处理逻辑 (Staged Processing)
// =========================================================

template<typename T>
struct StageResult {
    std::vector<IndustryQuadtreeTaskT<T>> next_tasks;
    std::vector<PointData> points;
};

template<typename T>
void execute_industry_processing_staged(
    std::vector<PointData>& out_points,
    const IndustrialRPN& rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    T world_w_total = T(screen_width * wppx);
    T world_h_total = T(screen_height * wppy);
    T x_origin = T(world_origin.x);
    T y_origin = T(world_origin.y);

    std::vector<IndustryQuadtreeTaskT<T>> active_tasks;
    active_tasks.reserve(10000);
    active_tasks.push_back({x_origin, y_origin, world_w_total, world_h_total});

    double current_pixel_threshold = rpn.start_pixel_threshold;
    double min_pixel_threshold = rpn.min_pixel_threshold;
    double step_factor = rpn.step_factor;
    if (step_factor <= 1.001) step_factor = 2.0;

    int stage_counter = 0;
    MultiIntervalBundle4<T> t_bundle(Interval<T>(T(0)));

    oneapi::tbb::enumerable_thread_specific<StageResult<T>> tls_stage_results;

    // 【优化】位图分配移到循环外
    int sw = (int)screen_width + 1;
    int sh = (int)screen_height + 1;
    std::vector<uint8_t> pixel_counts(sw * sh);

    while (true) {
        auto stage_start_time = std::chrono::high_resolution_clock::now();

        // 【优化】极速重置位图
        std::memset(pixel_counts.data(), 0, pixel_counts.size());

        double current_world_threshold_dbl = std::abs(current_pixel_threshold * wppx);
        T current_world_threshold = T(current_world_threshold_dbl);

        // 清空 TLS (保留 capacity)
        for (auto& local : tls_stage_results) {
            local.next_tasks.clear();
            local.points.clear();
        }

        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>(0, active_tasks.size()),
            [&](const oneapi::tbb::blocked_range<size_t>& range) {

                StageResult<T>& local_res = tls_stage_results.local();
                std::vector<IndustryQuadtreeTaskT<T>> local_stack;
                local_stack.reserve(64);
                uint8_t* p_counts = pixel_counts.data(); // 指针加速访问

                for (size_t i = range.begin(); i != range.end(); ++i) {
                    local_stack.push_back(active_tasks[i]);

                    while (!local_stack.empty()) {
                        IndustryQuadtreeTaskT<T> task = local_stack.back();
                        local_stack.pop_back();

                        // 1. 判断是否达到当前阶段的细分目标
                        bool reached_threshold = (std::abs(convert_to_double(task.world_w)) <= current_world_threshold_dbl);

                        if (reached_threshold) {
                            // A. 达到本阶段目标

                            // 屏幕坐标映射
                            double cx_dbl = convert_to_double(task.world_x + task.world_w * 0.5);
                            double cy_dbl = convert_to_double(task.world_y + task.world_h * 0.5);

                            int px = (int)((cx_dbl - convert_to_double(x_origin)) / wppx);
                            int py = (int)((cy_dbl - convert_to_double(y_origin)) / wppy);

                            bool pixel_is_full = false;

                            // 剔除检查 (Culling Check)
                            if (px >= 0 && px < sw && py >= 0 && py < sh) {
                                size_t idx = py * sw + px;
                                // 原子加1，返回旧值。如果旧值 >= 2，说明满了。
                                if (ATOMIC_INC_U8(&p_counts[idx]) <= 2) {
                                    double wx_offset = convert_to_double(cx_dbl - offset_x);
                                    double wy_offset = convert_to_double(cy_dbl - offset_y);
                                    local_res.points.push_back(PointData{ {wx_offset, wy_offset}, func_idx });
                                } else {
                                    pixel_is_full = true;
                                }
                            }

                            // 【核心优化：智能剪枝】
                            bool prune_branch = (current_pixel_threshold <= 1.0) && pixel_is_full;

                            if (!prune_branch) {
                                local_res.next_tasks.push_back(task);
                            }
                        }
                        else {
                            // B. 未达到精度，继续分裂
                            T w2 = task.world_w * 0.5;
                            T h2 = task.world_h * 0.5;
                            T xm = task.world_x + w2;
                            T ym = task.world_y + h2;

                            MultiIntervalBundle4<T> x_bundle, y_bundle;
                            x_bundle.v[0] = Interval<T>(task.world_x, xm);
                            x_bundle.v[1] = Interval<T>(xm, task.world_x + task.world_w);
                            x_bundle.v[2] = x_bundle.v[0];
                            x_bundle.v[3] = x_bundle.v[1];

                            T y_min_top = (task.world_y < ym ? task.world_y : ym);
                            T y_max_top = (task.world_y > ym ? task.world_y : ym);
                            T y_end = task.world_y + task.world_h;
                            T y_min_bot = (ym < y_end ? ym : y_end);
                            T y_max_bot = (ym > y_end ? ym : y_end);

                            y_bundle.v[0] = Interval<T>(y_min_top, y_max_top);
                            y_bundle.v[1] = y_bundle.v[0];
                            y_bundle.v[2] = Interval<T>(y_min_bot, y_max_bot);
                            y_bundle.v[3] = y_bundle.v[2];

                            MultiIntervalBundle4<T> res_bundle = evaluate_rpn_batch4_optimized<T>(
                                rpn.program, x_bundle, y_bundle, t_bundle
                            );

                            if (res_bundle.v[3].contains_zero()) local_stack.push_back({xm, ym, w2, h2});
                            if (res_bundle.v[2].contains_zero()) local_stack.push_back({task.world_x, ym, w2, h2});
                            if (res_bundle.v[1].contains_zero()) local_stack.push_back({xm, task.world_y, w2, h2});
                            if (res_bundle.v[0].contains_zero()) local_stack.push_back({task.world_x, task.world_y, w2, h2});
                        }
                    }
                }
            }
        );

        // --- 阶段汇总 ---
        out_points.clear(); // 仅保留当前阶段的最优解

        size_t total_points = 0;
        size_t total_next_tasks = 0;
        for (const auto& local : tls_stage_results) {
            total_points += local.points.size();
            total_next_tasks += local.next_tasks.size();
        }

        out_points.reserve(total_points);
        std::vector<IndustryQuadtreeTaskT<T>> next_active_tasks;
        next_active_tasks.reserve(total_next_tasks);

        for (const auto& local : tls_stage_results) {
            out_points.insert(out_points.end(), local.points.begin(), local.points.end());
            next_active_tasks.insert(next_active_tasks.end(), local.next_tasks.begin(), local.next_tasks.end());
        }

        auto stage_end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stage_end_time - stage_start_time).count();

        std::cout << "[Industry Stage " << stage_counter << "]"
                  << " Threshold: " << current_pixel_threshold << " px"
                  << " | Points: " << total_points
                  << " | Tasks: " << total_next_tasks
                  << " | Time: " << duration << " ms"
                  << std::endl;

        // ====================================================================
        //          ↓↓↓ WASM 渐进式渲染核心逻辑 (修复崩溃版) ↓↓↓
        // ====================================================================
        #ifdef __EMSCRIPTEN__
        if (!out_points.empty()) {
            // 1. 将当前阶段的计算结果直接覆盖到全局 WASM 缓冲区
            // 注意：这假设当前场景下主要关注工业函数的渲染，或者单函数调试
            wasm_final_contiguous_buffer.assign(out_points.begin(), out_points.end());

            // 更新函数范围信息 (FunctionRange)
            if (wasm_function_ranges_buffer.size() <= func_idx) {
                wasm_function_ranges_buffer.resize(func_idx + 1);
            }
            wasm_function_ranges_buffer[func_idx] = {0, (uint32_t)out_points.size()};

            // 2. 原子操作：增加版本号，告诉 JS "有新数据了"
            // 使用 release 内存序确保数据写入先于版本号更新
            g_industry_stage_version.fetch_add(1, std::memory_order_release);
        }
        #endif
        // ====================================================================

        active_tasks = std::move(next_active_tasks);

        if (current_pixel_threshold <= min_pixel_threshold) break;

        double next_threshold = current_pixel_threshold / step_factor;
        if (next_threshold < min_pixel_threshold) next_threshold = min_pixel_threshold;
        if (next_threshold >= current_pixel_threshold) break;
        current_pixel_threshold = next_threshold;
        stage_counter++;
    }
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
    std::vector<PointData> local_points;

    try {
        IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);

        if (rpn.precision_bits == 0) {
            execute_industry_processing_staged<double>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, zoom
            );
        } else {
            hp_float::default_precision(rpn.precision_bits);
            execute_industry_processing_staged<hp_float>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height, offset_x, offset_y, zoom
            );
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing industry RPN: " << e.what() << std::endl;
    }

    results_queue->push({func_idx, std::move(local_points)});
}