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
#include <stack>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <array>

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
    if constexpr (std::is_same_v<T, hp_float>) return val.template convert_to<double>();
    else return val;
}

// =========================================================
//        ↓↓↓ 核心优化：4路批量计算结构 (Batch-4) ↓↓↓
// =========================================================

template<typename T>
struct MultiIntervalBundle4 {
    MultiInterval<T> v[4];

    MultiIntervalBundle4() {}

    // 广播构造
    MultiIntervalBundle4(T val) {
        MultiInterval<T> m(val);
        v[0] = m; v[1] = m; v[2] = m; v[3] = m;
    }

    MultiIntervalBundle4(const Interval<T>& i) {
        MultiInterval<T> m(i);
        v[0] = m; v[1] = m; v[2] = m; v[3] = m;
    }
};

// 批量运算宏
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
// Gamma 已移除

// 批量 RPN 求值器
template<typename T>
MultiIntervalBundle4<T> evaluate_rpn_batch4(
    const AlignedVector<RPNToken>& prog,
    const MultiIntervalBundle4<T>& x_bundle,
    const MultiIntervalBundle4<T>& y_bundle,
    const MultiIntervalBundle4<T>& t_bundle
) {
    MultiIntervalBundle4<T> stack[64];
    int sp = 0;

    for (const auto& token : prog) {
        switch (token.type) {
            case RPNTokenType::PUSH_CONST:
                stack[sp++] = MultiIntervalBundle4<T>(T(token.value));
                break;
            case RPNTokenType::PUSH_X: stack[sp++] = x_bundle; break;
            case RPNTokenType::PUSH_Y: stack[sp++] = y_bundle; break;
            case RPNTokenType::PUSH_T: stack[sp++] = t_bundle; break;

            case RPNTokenType::ADD: --sp; stack[sp-1] = stack[sp-1] + stack[sp]; break;
            case RPNTokenType::SUB: --sp; stack[sp-1] = stack[sp-1] - stack[sp]; break;
            case RPNTokenType::MUL: --sp; stack[sp-1] = stack[sp-1] * stack[sp]; break;
            case RPNTokenType::DIV: --sp; stack[sp-1] = stack[sp-1] / stack[sp]; break;
            case RPNTokenType::POW: --sp; stack[sp-1] = batch_pow(stack[sp-1], stack[sp]); break;

            case RPNTokenType::SIN: stack[sp-1] = batch_sin(stack[sp-1]); break;
            case RPNTokenType::COS: stack[sp-1] = batch_cos(stack[sp-1]); break;
            case RPNTokenType::TAN: stack[sp-1] = batch_tan(stack[sp-1]); break;
            case RPNTokenType::EXP: stack[sp-1] = batch_exp(stack[sp-1]); break;
            case RPNTokenType::LN:  stack[sp-1] = batch_ln(stack[sp-1]); break;
            case RPNTokenType::ABS: stack[sp-1] = batch_abs(stack[sp-1]); break;

            default: break;
        }
    }
    if (sp == 0) return MultiIntervalBundle4<T>();
    return stack[0];
}

// =========================================================
//              核心处理逻辑
// =========================================================
template<typename T>
void execute_industry_processing(
    std::vector<PointData>& out_points,
    const IndustrialRPN& rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height
) {
    // 1. 全局剔除
    {
        T screen_w = T(screen_width * wppx);
        T screen_h = T(screen_height * wppy);
        T x1 = T(world_origin.x); T x2 = x1 + screen_w;
        T y1 = T(world_origin.y) + screen_h; T y2 = T(world_origin.y);

        // [修复] 使用三元运算符代替 std::min/max
        Interval<T> full_x(x1, x2);
        Interval<T> full_y((y1 < y2 ? y1 : y2), (y1 > y2 ? y1 : y2));
        Interval<T> t_param(T(0));

        Interval<T> fast_check = evaluate_rpn<Interval<T>>(rpn.program, full_x, full_y, t_param, rpn.precision_bits);
        if (fast_check.min > T(0) || fast_check.max < T(0)) return;
        if (std::isinf(convert_to_double(fast_check.min))) {
            MultiInterval<T> multi_check = evaluate_rpn_multi<T>(rpn.program, full_x, full_y, t_param, rpn.precision_bits);
            if (!multi_check.contains_zero()) return;
        }
    }

    // 2. TLS
    using LocalPointBuffer = std::vector<PointData>;
    oneapi::tbb::enumerable_thread_specific<LocalPointBuffer> tls_buffers;

    // 3. 任务分块 (16x16)
    const int num_tiles_x = 2;
    const int num_tiles_y = 2;
    const int total_tiles = num_tiles_x * num_tiles_y;

    T tile_w = T(screen_width * wppx) / T(num_tiles_x);
    T tile_h = T(screen_height * wppy) / T(num_tiles_y);

    oneapi::tbb::parallel_for(
        oneapi::tbb::blocked_range<int>(0, total_tiles),
        [&](const oneapi::tbb::blocked_range<int>& range) {

            LocalPointBuffer& local_buffer = tls_buffers.local();
            if (local_buffer.capacity() == 0) local_buffer.reserve(4096);

            std::stack<IndustryQuadtreeTaskT<T>> stack;
            const double pixel_threshold = std::abs(6 * wppx);
            Interval<T> t_param(T(0));
            MultiIntervalBundle4<T> t_bundle(t_param);

            for (int i = range.begin(); i != range.end(); ++i) {
                int tx = i % num_tiles_x;
                int ty = i / num_tiles_x;
                T tx_start = T(world_origin.x) + T(tx) * tile_w;
                T ty_start = T(world_origin.y) + T(ty) * tile_h;

                stack.push({tx_start, ty_start, tile_w, tile_h});

                while (!stack.empty()) {
                    IndustryQuadtreeTaskT<T> task = stack.top();
                    stack.pop();

                    // 检查是否需要细分
                    if (std::abs(convert_to_double(task.world_w)) <= pixel_threshold) {
                        // === 叶子节点 (单次计算) ===
                        T cx = task.world_x + task.world_w * 0.5;
                        T cy = task.world_y + task.world_h * 0.5;

                        // [修复] 使用三元运算符代替 std::min/max
                        T y1 = task.world_y;
                        T y2 = task.world_y + task.world_h;
                        Interval<T> x_int(task.world_x, task.world_x + task.world_w);
                        Interval<T> y_int((y1 < y2 ? y1 : y2), (y1 > y2 ? y1 : y2));

                        // 快速筛选
                        Interval<T> fast_res = evaluate_rpn<Interval<T>>(rpn.program, x_int, y_int, t_param, rpn.precision_bits);
                        if (fast_res.min > T(0) || fast_res.max < T(0)) continue;

                        // 精确筛选
                        MultiInterval<T> multi_res = evaluate_rpn_multi<T>(rpn.program, x_int, y_int, t_param, rpn.precision_bits);

                        if (multi_res.contains_zero()) {



                            double sx = convert_to_double((cx - T(world_origin.x)) / T(wppx));
                            double sy = convert_to_double((cy - T(world_origin.y)) / T(wppy));
                            local_buffer.push_back(PointData{ {sx, sy}, func_idx });

                        }
                        continue;
                    }

                    // =========================================================
                    // 【核心优化】批量处理 4 个子节点 (Batch Processing)
                    // =========================================================
                    T w2 = task.world_w * 0.5;
                    T h2 = task.world_h * 0.5;
                    T xm = task.world_x + w2;
                    T ym = task.world_y + h2;

                    MultiIntervalBundle4<T> x_bundle, y_bundle;

                    // X Bundle
                    x_bundle.v[0] = Interval<T>(task.world_x, xm);
                    x_bundle.v[1] = Interval<T>(xm, task.world_x + task.world_w);
                    x_bundle.v[2] = x_bundle.v[0];
                    x_bundle.v[3] = x_bundle.v[1];

                    // Y Bundle
                    // [修复] 使用三元运算符代替 std::min/max
                    T y_min_top = (task.world_y < ym ? task.world_y : ym);
                    T y_max_top = (task.world_y > ym ? task.world_y : ym);

                    T y_end = task.world_y + task.world_h;
                    T y_min_bot = (ym < y_end ? ym : y_end);
                    T y_max_bot = (ym > y_end ? ym : y_end);

                    y_bundle.v[0] = Interval<T>(y_min_top, y_max_top);
                    y_bundle.v[1] = y_bundle.v[0];
                    y_bundle.v[2] = Interval<T>(y_min_bot, y_max_bot);
                    y_bundle.v[3] = y_bundle.v[2];

                    // 批量求值
                    MultiIntervalBundle4<T> res_bundle = evaluate_rpn_batch4<T>(
                        rpn.program, x_bundle, y_bundle, t_bundle
                    );

                    // 检查结果并入栈 (倒序)
                    if (res_bundle.v[3].contains_zero()) stack.push({xm, ym, w2, h2});
                    if (res_bundle.v[2].contains_zero()) stack.push({task.world_x, ym, w2, h2});
                    if (res_bundle.v[1].contains_zero()) stack.push({xm, task.world_y, w2, h2});
                    if (res_bundle.v[0].contains_zero()) stack.push({task.world_x, task.world_y, w2, h2});
                }
            }
        }
    );

    // 4. 合并结果
    size_t total_points = 0;
    for (const auto& buf : tls_buffers) total_points += buf.size();
    out_points.reserve(total_points);
    for (const auto& buf : tls_buffers) out_points.insert(out_points.end(), buf.begin(), buf.end());
}

} // namespace

// =========================================================
//              对外接口
// =========================================================

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
            execute_industry_processing<double>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        } else {
            hp_float::default_precision(rpn.precision_bits);
            execute_industry_processing<hp_float>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing industry RPN: " << e.what() << std::endl;
    }

    results_queue->push({func_idx, std::move(local_points)});
}