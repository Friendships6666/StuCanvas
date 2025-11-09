#include "pch.h" // 如果您使用预编译头文件

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <optional>
#include <array>
#include <limits>
#include <utility>
#include <thread>
#include <chrono>
#include <stack>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cstdint> // For uint32_t

#include <xsimd/xsimd.hpp>
#include "oneapi/tbb/concurrent_vector.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/combinable.h"

// ===============================================================
// === 命名空间、核心数据结构与宏定义 ===
// ===============================================================

namespace xs = xsimd;

template <typename T>
using AlignedVector = std::vector<T, xsimd::aligned_allocator<T>>;

struct Vec2 { double x; double y; };
struct PointData { Vec2 position; unsigned int function_index; };
static_assert(sizeof(PointData) == 24, "PointData size/padding mismatch! Expected 24 bytes.");

// [NEW] 结构体，用于描述每个函数在最终数组中的数据区间
struct FunctionRange {
    uint32_t start_index;
    uint32_t point_count;
};
// 编译时检查，确保C++和JS对结构体大小的认知一致
static_assert(sizeof(FunctionRange) == 8, "FunctionRange size mismatch! Expected 8 bytes.");

struct Uniforms { Vec2 screen_dimensions; double zoom; Vec2 offset; };

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline))
#endif

using batch_type = xs::batch<double>;
constexpr size_t RPN_MAX_STACK_DEPTH = 16;
struct ParametricSubdivisionTask {
    double t1; Vec2 p1; double t2; Vec2 p2; int depth;
};
struct SubdivisionTask { Vec2 p1; Vec2 p2; int depth; };


// ===============================================================
// === 安全的数学辅助函数 (标量 & SIMD) ===
// ===============================================================

FORCE_INLINE double safe_exp_scalar(double x) {
    if (x >= 1) return 1e270;
    if (x <= -100) return 1e-270;
    return std::exp(x);
}

FORCE_INLINE batch_type safe_exp_batch(const batch_type& x) {
    auto is_large_mask = x >= batch_type(1);
    auto is_small_mask = x <= batch_type(-100);
    batch_type normal_result = xs::exp(x);
    batch_type intermediate_result = xs::select(is_small_mask, batch_type(1e-270), normal_result);
    return xs::select(is_large_mask, batch_type(1e270), intermediate_result);
}

FORCE_INLINE double check_ln_scalar(double x) {
    if (x <= 0.0) return std::numeric_limits<double>::quiet_NaN();
    return std::log(x);
}
FORCE_INLINE batch_type check_ln_batch(const batch_type& x) {
    auto is_positive_mask = x > batch_type(0.0);
    batch_type log_result = xs::log(x);
    return xs::select(is_positive_mask, log_result, batch_type(std::numeric_limits<double>::quiet_NaN()));
}
FORCE_INLINE double safe_ln_scalar(double x) {
    if (x > 0.0) return std::log(x);
    return -1e270;
}

FORCE_INLINE batch_type safe_ln_batch(const batch_type& x) {
    auto is_positive_mask = x > batch_type(0.0);
    batch_type log_result = xs::log(x);
    return xs::select(is_positive_mask, log_result, batch_type(-1e270));
}

FORCE_INLINE const xs::batch<double>& get_index_vec() {
    using batch_type = xs::batch<double>;
    static const auto index_vec = [] {
        constexpr std::size_t batch_size = batch_type::size;
        alignas(batch_type::arch_type::alignment()) std::array<double, batch_size> indices{};
        for (std::size_t i = 0; i < batch_size; ++i) {
            indices[i] = static_cast<double>(i);
        }
        return xs::load_aligned(indices.data());
    }();
    return index_vec;
}

// ===============================================================
// === 逆波兰表达式 (RPN) 引擎 ===
// ===============================================================

enum class RPNTokenType { PUSH_CONST, PUSH_X, PUSH_Y,PUSH_T, ADD, SUB, MUL, DIV, SIN, COS, EXP, POW, SIGN, ABS, SAFE_LN, SAFE_EXP, CHECK_LN,TAN,LN };
struct RPNToken { RPNTokenType type; double value = 0.0; };

AlignedVector<RPNToken> parse_rpn(const std::string& rpn_string) {
    AlignedVector<RPNToken> tokens;
    std::stringstream ss(rpn_string);
    std::string token_str;
    while (ss >> token_str) {
        if (token_str == "x") tokens.push_back({RPNTokenType::PUSH_X});
        else if (token_str == "y") tokens.push_back({RPNTokenType::PUSH_Y});
        else if (token_str == "+") tokens.push_back({RPNTokenType::ADD});
        else if (token_str == "-") tokens.push_back({RPNTokenType::SUB});
        else if (token_str == "*") tokens.push_back({RPNTokenType::MUL});
        else if (token_str == "/") tokens.push_back({RPNTokenType::DIV});
        else if (token_str == "sin") tokens.push_back({RPNTokenType::SIN});
        else if (token_str == "cos") tokens.push_back({RPNTokenType::COS});
        else if (token_str == "exp") tokens.push_back({RPNTokenType::EXP});
        else if (token_str == "tan") tokens.push_back({RPNTokenType::TAN});
        else if (token_str == "pow") tokens.push_back({RPNTokenType::POW});
        else if (token_str == "sign") tokens.push_back({RPNTokenType::SIGN});
        else if (token_str == "abs") tokens.push_back({RPNTokenType::ABS});
        else if (token_str == "_t_") tokens.push_back({RPNTokenType::PUSH_T});
        else if (token_str == "safeln") tokens.push_back({RPNTokenType::SAFE_LN});
        else if (token_str == "ln") tokens.push_back({RPNTokenType::LN});
        else if (token_str == "safeexp") tokens.push_back({RPNTokenType::SAFE_EXP});
        else if (token_str == "check_ln") tokens.push_back({RPNTokenType::CHECK_LN});
        else {
            try { tokens.push_back({RPNTokenType::PUSH_CONST, std::stod(token_str)}); }
            catch (const std::invalid_argument&) { throw std::runtime_error("无效的RPN指令: " + token_str); }
        }
    }
    return tokens;
}

FORCE_INLINE double evaluate_rpn(const AlignedVector<RPNToken>& p, std::optional<double> x = std::nullopt, std::optional<double> y = std::nullopt, std::optional<double> t_param = std::nullopt) {
    std::array<double, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST: s[sp++] = t.value; break;
            case RPNTokenType::PUSH_X: s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y: s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T: s[sp++] = t_param.value(); break;
            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;
            case RPNTokenType::DIV:      --sp; s[sp - 1] /= s[sp]; break;
            case RPNTokenType::SIN:      s[sp - 1] = std::sin(s[sp - 1]); break;
            case RPNTokenType::COS:      s[sp - 1] = std::cos(s[sp - 1]); break;
            case RPNTokenType::TAN:      s[sp - 1] = std::tan(s[sp - 1]); break;
            case RPNTokenType::LN:       s[sp - 1] = std::log(s[sp - 1]); break;
            case RPNTokenType::EXP:      s[sp - 1] = std::exp(s[sp - 1]); break;
            case RPNTokenType::POW:      --sp; s[sp - 1] = std::pow(s[sp - 1], s[sp]); break;
            case RPNTokenType::SIGN:     s[sp - 1] = (s[sp - 1] > 0.0) - (s[sp - 1] < 0.0); break;
            case RPNTokenType::ABS:      s[sp - 1] = std::abs(s[sp - 1]); break;
            case RPNTokenType::SAFE_LN:  s[sp - 1] = safe_ln_scalar(s[sp - 1]); break;
            case RPNTokenType::CHECK_LN: s[sp - 1] = check_ln_scalar(s[sp - 1]); break;
            case RPNTokenType::SAFE_EXP: s[sp - 1] = safe_exp_scalar(s[sp - 1]); break;
        }
    }
    return s[0];
}

FORCE_INLINE batch_type evaluate_rpn_batch(const AlignedVector<RPNToken>& p, std::optional<batch_type> x = std::nullopt, std::optional<batch_type> y = std::nullopt, std::optional<batch_type> t_param = std::nullopt) {
    std::array<batch_type, RPN_MAX_STACK_DEPTH> s{};
    int sp = 0;
    for (const auto& t : p) {
        switch (t.type) {
            case RPNTokenType::PUSH_CONST: s[sp++] = batch_type(t.value); break;
            case RPNTokenType::PUSH_X: s[sp++] = x.value(); break;
            case RPNTokenType::PUSH_Y: s[sp++] = y.value(); break;
            case RPNTokenType::PUSH_T: s[sp++] = t_param.value(); break;
            case RPNTokenType::ADD:      --sp; s[sp - 1] += s[sp]; break;
            case RPNTokenType::SUB:      --sp; s[sp - 1] -= s[sp]; break;
            case RPNTokenType::MUL:      --sp; s[sp - 1] *= s[sp]; break;
            case RPNTokenType::DIV:      --sp; s[sp - 1] /= s[sp]; break;
            case RPNTokenType::SIN:      s[sp - 1] = xs::sin(s[sp - 1]); break;
            case RPNTokenType::COS:      s[sp - 1] = xs::cos(s[sp - 1]); break;
            case RPNTokenType::TAN:      s[sp - 1] = xs::tan(s[sp - 1]); break;
            case RPNTokenType::LN:       s[sp - 1] = xs::log(s[sp - 1]); break;
            case RPNTokenType::EXP:      s[sp - 1] = xs::exp(s[sp - 1]); break;
            case RPNTokenType::POW:      --sp; s[sp - 1] = xs::pow(s[sp - 1], s[sp]); break;
            case RPNTokenType::SIGN:     s[sp - 1] = xs::sign(s[sp - 1]); break;
            case RPNTokenType::ABS:      s[sp - 1] = xs::abs(s[sp - 1]); break;
            case RPNTokenType::SAFE_LN:  s[sp - 1] = safe_ln_batch(s[sp - 1]); break;
            case RPNTokenType::SAFE_EXP: s[sp - 1] = safe_exp_batch(s[sp - 1]); break;
            case RPNTokenType::CHECK_LN: s[sp - 1] = check_ln_batch(s[sp - 1]); break;
        }
    }
    return s[0];
}

// ===============================================================
// === 坐标转换 ===
// ===============================================================

FORCE_INLINE Vec2 screen_to_world_inline(const Vec2& scr, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + scr.x * wppx, origin.y + scr.y * wppy };
}
FORCE_INLINE std::pair<batch_type, batch_type> screen_to_world_batch(const batch_type& sx, double sy, const Vec2& origin, double wppx, double wppy) {
    return { origin.x + sx * wppx, origin.y + sy * wppy };
}

// ===============================================================
// === 隐函数 f(x, y) = 0 算法核心 ===
// ===============================================================

constexpr unsigned int TILE_W = 512;
constexpr unsigned int TILE_H = 512;

struct ThreadCacheForTiling {
    AlignedVector<double> top_row_vals;
    AlignedVector<double> bot_row_vals;
    AlignedVector<PointData> point_buffer;
    ThreadCacheForTiling() { top_row_vals.resize(TILE_W + 1); bot_row_vals.resize(TILE_W + 1); point_buffer.reserve(3000); }
};

FORCE_INLINE bool try_get_intersection_point(Vec2& out, const Vec2& p1, const Vec2& p2, double v1, double v2, const AlignedVector<RPNToken>& prog_check) {
    if ((v1 * v2 > 0.0) && (std::signbit(v1) == std::signbit(v2))) return false;
    if (std::abs(v1) >= 1e268 && std::abs(v2) >= 1e268) return false;
    double t = -v1 / (v2 - v1);
    out = {p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
    double check_val = evaluate_rpn(prog_check, out.x, out.y);
    return std::isfinite(check_val) && std::abs(check_val) < 1e200;
}

void process_tile(const Vec2& world_origin, double wppx, double wppy,
                  const AlignedVector<RPNToken>& rpn_program, const AlignedVector<RPNToken>& rpn_program_check,
                  unsigned int func_idx, unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
                  ThreadCacheForTiling& cache, oneapi::tbb::concurrent_vector<PointData> & all_points) {
    const unsigned int tile_w = x_end - x_start;
    auto& top_row_vals = cache.top_row_vals; auto& bot_row_vals = cache.bot_row_vals; auto& point_buffer = cache.point_buffer;
    for (unsigned int x = x_start; x <= x_end; ++x) {
        top_row_vals[x - x_start] = evaluate_rpn(rpn_program, world_origin.x + x * wppx, world_origin.y + y_start * wppy);
    }
    for (unsigned int y = y_start; y < y_end; ++y) {
        const std::size_t vec_end = tile_w - (tile_w % batch_type::size);
        for (std::size_t x_off = 0; x_off < vec_end; x_off += batch_type::size) {
            batch_type sx = get_index_vec() + static_cast<double>(x_start + x_off);
            auto [wx, wy] = screen_to_world_batch(sx, (double)y + 1.0, world_origin, wppx, wppy);
            xs::store_aligned(&bot_row_vals[x_off], evaluate_rpn_batch(rpn_program, wx, wy));
        }
        for (std::size_t x_off = vec_end; x_off <= tile_w; ++x_off) {
            Vec2 world_pos = screen_to_world_inline({(double)(x_start + x_off), (double)y + 1.0}, world_origin, wppx, wppy);
            bot_row_vals[x_off] = evaluate_rpn(rpn_program, world_pos.x, world_pos.y);
        }
        point_buffer.clear();
        for (unsigned int x_off = 0; x_off < tile_w; ++x_off) {
            double tl = top_row_vals[x_off], tr = top_row_vals[x_off+1], bl = bot_row_vals[x_off];
            if (!std::isfinite(tl) || !std::isfinite(tr) || !std::isfinite(bl)) continue;
            double sign_tl = (tl > 0.0) - (tl < 0.0);
            if (((tr > 0.0) - (tr < 0.0)) == sign_tl && ((bl > 0.0) - (bl < 0.0)) == sign_tl) continue;
            constexpr double step = 0.5; Vec2 intersection;
            for (int ly = 0; ly < 2; ++ly) for (int lx = 0; lx < 2; ++lx) {
                Vec2 s_tl_scr = {(double)(x_start + x_off) + lx*step, (double)y + ly*step};
                Vec2 p_tl = screen_to_world_inline(s_tl_scr, world_origin, wppx, wppy);
                Vec2 p_tr = screen_to_world_inline({s_tl_scr.x + step, s_tl_scr.y}, world_origin, wppx, wppy);
                Vec2 p_bl = screen_to_world_inline({s_tl_scr.x, s_tl_scr.y + step}, world_origin, wppx, wppy);
                double v_tl=evaluate_rpn(rpn_program, p_tl.x, p_tl.y), v_tr=evaluate_rpn(rpn_program,p_tr.x,p_tr.y), v_bl=evaluate_rpn(rpn_program,p_bl.x,p_bl.y);
                if (!std::isfinite(v_tl) || !std::isfinite(v_tr) || !std::isfinite(v_bl)) continue;
                if (try_get_intersection_point(intersection, p_tl, p_tr, v_tl, v_tr, rpn_program_check)) point_buffer.emplace_back(PointData{intersection, func_idx});
                if (try_get_intersection_point(intersection, p_tl, p_bl, v_tl, v_bl, rpn_program_check)) point_buffer.emplace_back(PointData{intersection, func_idx});
            }
        }
        if (!point_buffer.empty()) { for(const auto& p : point_buffer) all_points.emplace_back(p); }
        std::swap(top_row_vals, bot_row_vals);
    }
}

// ===============================================================
// === 显函数 y = f(x) 算法核心 ===
// ===============================================================

constexpr std::size_t BATCH_SIZE = batch_type::size;

void process_explicit_chunk(
    double y_min_world, double y_max_world,
    double x_start, double x_end, const AlignedVector<RPNToken>& rpn_program,
    double max_dist_sq, int max_depth,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx)
{
    AlignedVector<SubdivisionTask> tasks_container;
    tasks_container.reserve(max_depth * 2);
    std::stack<SubdivisionTask, AlignedVector<SubdivisionTask>> tasks(std::move(tasks_container));

    AlignedVector<SubdivisionTask> active_tasks;
    active_tasks.reserve(BATCH_SIZE);

    auto is_culled = [&](double y1, double y2) {
        if (y1 > y_max_world && y2 > y_max_world) return true;
        if (y1 < y_min_world && y2 < y_min_world) return true;
        return false;
    };

    double y_start = evaluate_rpn(rpn_program, x_start);
    double y_end = evaluate_rpn(rpn_program, x_end);

    if (std::isfinite(y_start) && std::isfinite(y_end)) {
        if (!is_culled(y_start, y_end)) {
            all_points.emplace_back(PointData{ {x_start, y_start}, func_idx });
            tasks.push({ {x_start, y_start}, {x_end, y_end}, 0 });
        }
    } else if (std::isfinite(y_start)) {
        all_points.emplace_back(PointData{ {x_start, y_start}, func_idx });
    }

    while (true) {
        while (active_tasks.size() < BATCH_SIZE && !tasks.empty()) {
            active_tasks.push_back(tasks.top());
            tasks.pop();
        }
        if (active_tasks.empty()) break;

        if (active_tasks.size() == BATCH_SIZE) {
            alignas(batch_type::arch_type::alignment()) std::array<double, BATCH_SIZE> x1, y1, x2, y2, depth;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                x1[i] = active_tasks[i].p1.x; y1[i] = active_tasks[i].p1.y;
                x2[i] = active_tasks[i].p2.x; y2[i] = active_tasks[i].p2.y;
                depth[i] = static_cast<double>(active_tasks[i].depth);
            }

            const batch_type x1_b = xs::load_aligned(x1.data());
            const batch_type y1_b = xs::load_aligned(y1.data());
            const batch_type x2_b = xs::load_aligned(x2.data());
            const batch_type y2_b = xs::load_aligned(y2.data());
            const batch_type depth_b = xs::load_aligned(depth.data());

            const batch_type dx_b = x2_b - x1_b;
            const batch_type dy_b = y2_b - y1_b;
            const batch_type dist_sq_b = dx_b * dx_b + dy_b * dy_b;

            const auto subdivide_mask = (dist_sq_b > batch_type(max_dist_sq)) & (depth_b < batch_type(max_depth));

            if (xs::none(subdivide_mask)) {
                for (const auto& task : active_tasks) {
                    all_points.emplace_back(PointData{task.p2, func_idx});
                }
            } else {
                const batch_type x_mid_b = x1_b + dx_b * 0.5;
                const batch_type y_mid_b = evaluate_rpn_batch(rpn_program, x_mid_b);
                const auto is_finite_mask = !xs::isinf(y_mid_b);

                for (size_t i = 0; i < BATCH_SIZE; ++i) {
                    if (subdivide_mask.get(i) && is_finite_mask.get(i)) {
                        Vec2 p_mid = {x_mid_b.get(i), y_mid_b.get(i)};
                        if (!is_culled(active_tasks[i].p1.y, p_mid.y)) {
                            tasks.push({ active_tasks[i].p1, p_mid, active_tasks[i].depth + 1 });
                        }
                        if (!is_culled(p_mid.y, active_tasks[i].p2.y)) {
                            tasks.push({ p_mid, active_tasks[i].p2, active_tasks[i].depth + 1 });
                        }
                    } else {
                        all_points.emplace_back(PointData{active_tasks[i].p2, func_idx});
                    }
                }
            }
        } else {
            for (const auto& task : active_tasks) {
                double dx = task.p2.x - task.p1.x, dy = task.p2.y - task.p1.y;
                double dist_sq = dx * dx + dy * dy;

                if (dist_sq > max_dist_sq && task.depth < max_depth) {
                    double x_mid = task.p1.x + dx / 2.0;
                    double y_mid = evaluate_rpn(rpn_program, x_mid);

                    if (!std::isfinite(y_mid)) {
                        all_points.emplace_back(PointData{task.p2, func_idx});
                        continue;
                    }

                    Vec2 p_mid = {x_mid, y_mid};
                    if (!is_culled(task.p1.y, p_mid.y)) {
                        tasks.push({ task.p1, p_mid, task.depth + 1 });
                    }
                    if (!is_culled(p_mid.y, task.p2.y)) {
                        tasks.push({ p_mid, task.p2, task.depth + 1 });
                    }
                } else {
                    all_points.emplace_back(PointData{task.p2, func_idx});
                }
            }
        }
        active_tasks.clear();
    }
}

// ===============================================================
// === 参数方程 y = g(t), x = f(t) 算法核心 ===
// ===============================================================
void process_parametric_chunk(
    double y_min_world, double y_max_world, double x_min_world, double x_max_world,
    double t_start, double t_end,
    const AlignedVector<RPNToken>& rpn_x, const AlignedVector<RPNToken>& rpn_y,
    double max_dist_sq, int max_depth,
    oneapi::tbb::concurrent_vector<PointData>& all_points,
    unsigned int func_idx)
{
    AlignedVector<ParametricSubdivisionTask> tasks_container;
    tasks_container.reserve(max_depth * 2);
    std::stack<ParametricSubdivisionTask, AlignedVector<ParametricSubdivisionTask>> tasks(std::move(tasks_container));

    AlignedVector<ParametricSubdivisionTask> active_tasks;
    active_tasks.reserve(BATCH_SIZE);

    auto is_culled = [&](const Vec2& p1, const Vec2& p2) {
        if ((p1.y > y_max_world && p2.y > y_max_world) || (p1.y < y_min_world && p2.y < y_min_world)) return true;
        if ((p1.x > x_max_world && p2.x > x_max_world) || (p1.x < x_min_world && p2.x < x_min_world)) return true;
        return false;
    };

    Vec2 p_start = { evaluate_rpn(rpn_x, std::nullopt, std::nullopt, t_start),
                     evaluate_rpn(rpn_y, std::nullopt, std::nullopt, t_start) };

    if (std::isfinite(p_start.x) && std::isfinite(p_start.y)) {
        all_points.emplace_back(PointData{ p_start, func_idx });
        Vec2 p_end = { evaluate_rpn(rpn_x, std::nullopt, std::nullopt, t_end),
                       evaluate_rpn(rpn_y, std::nullopt, std::nullopt, t_end) };
        if (std::isfinite(p_end.x) && std::isfinite(p_end.y) && !is_culled(p_start, p_end)) {
            tasks.push({ t_start, p_start, t_end, p_end, 0 });
        }
    }

    while (true) {
        while (active_tasks.size() < BATCH_SIZE && !tasks.empty()) {
            active_tasks.push_back(tasks.top());
            tasks.pop();
        }
        if (active_tasks.empty()) break;

        if (active_tasks.size() == BATCH_SIZE) {
            alignas(batch_type::arch_type::alignment()) std::array<double, BATCH_SIZE> t1, x1, y1, t2, x2, y2, depth;
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                const auto& task = active_tasks[i];
                t1[i] = task.t1; x1[i] = task.p1.x; y1[i] = task.p1.y;
                t2[i] = task.t2; x2[i] = task.p2.x; y2[i] = task.p2.y;
                depth[i] = static_cast<double>(task.depth);
            }

            const batch_type t1_b = xs::load_aligned(t1.data()), x1_b = xs::load_aligned(x1.data()), y1_b = xs::load_aligned(y1.data());
            const batch_type t2_b = xs::load_aligned(t2.data()), x2_b = xs::load_aligned(x2.data()), y2_b = xs::load_aligned(y2.data());
            const batch_type depth_b = xs::load_aligned(depth.data());

            const batch_type dx_b = x2_b - x1_b, dy_b = y2_b - y1_b;
            const batch_type dist_sq_b = dx_b * dx_b + dy_b * dy_b;

            const auto subdivide_mask = (dist_sq_b > batch_type(max_dist_sq)) & (depth_b < batch_type(max_depth));

            if (xs::none(subdivide_mask)) {
                for (const auto& task : active_tasks) all_points.emplace_back(PointData{ task.p2, func_idx });
            } else {
                const batch_type t_mid_b = t1_b + (t2_b - t1_b) * 0.5;
                const batch_type x_mid_b = evaluate_rpn_batch(rpn_x, std::nullopt, std::nullopt, t_mid_b);
                const batch_type y_mid_b = evaluate_rpn_batch(rpn_y, std::nullopt, std::nullopt, t_mid_b);
                const auto is_finite_mask = !xs::isinf(x_mid_b) & !xs::isinf(y_mid_b);

                for (size_t i = 0; i < BATCH_SIZE; ++i) {
                    if (subdivide_mask.get(i) && is_finite_mask.get(i)) {
                        const auto& task = active_tasks[i];
                        Vec2 p_mid = { x_mid_b.get(i), y_mid_b.get(i) };
                        if (!is_culled(task.p1, p_mid)) tasks.push({ task.t1, task.p1, t_mid_b.get(i), p_mid, task.depth + 1 });
                        if (!is_culled(p_mid, task.p2)) tasks.push({ t_mid_b.get(i), p_mid, task.t2, task.p2, task.depth + 1 });
                    } else {
                        all_points.emplace_back(PointData{ active_tasks[i].p2, func_idx });
                    }
                }
            }
        } else {
            for (const auto& task : active_tasks) {
                double dx = task.p2.x - task.p1.x, dy = task.p2.y - task.p1.y;
                double dist_sq = dx * dx + dy * dy;

                if (dist_sq > max_dist_sq && task.depth < max_depth) {
                    double t_mid = task.t1 + (task.t2 - task.t1) / 2.0;
                    Vec2 p_mid = { evaluate_rpn(rpn_x, std::nullopt, std::nullopt, t_mid),
                                   evaluate_rpn(rpn_y, std::nullopt, std::nullopt, t_mid) };

                    if (!std::isfinite(p_mid.x) || !std::isfinite(p_mid.y)) {
                        all_points.emplace_back(PointData{ task.p2, func_idx }); continue;
                    }
                    if (!is_culled(task.p1, p_mid)) tasks.push({ task.t1, task.p1, t_mid, p_mid, task.depth + 1 });
                    if (!is_culled(p_mid, task.p2)) tasks.push({ t_mid, p_mid, task.t2, task.p2, task.depth + 1 });
                } else {
                    all_points.emplace_back(PointData{ task.p2, func_idx });
                }
            }
        }
        active_tasks.clear();
    }
}

// ===============================================================
// === 主函数与接口层 ===
// ===============================================================

struct ExplicitFunction {
    AlignedVector<RPNToken> rpn;
};

struct ParametricFunction {
    AlignedVector<RPNToken> rpn_x;
    AlignedVector<RPNToken> rpn_y;
    double t_min;
    double t_max;
};

ParametricFunction parse_parametric_string(const std::string& str) {
    std::vector<std::string> parts;
    std::string current_part;
    std::stringstream ss(str);
    while (std::getline(ss, current_part, ';')) {
        parts.push_back(current_part);
    }

    if (parts.size() != 4) {
        throw std::runtime_error("参数方程格式错误，必须是 'x_rpn;y_rpn;t_min;t_max'。收到: " + str);
    }

    try {
        return ParametricFunction{
            parse_rpn(parts[0]),
            parse_rpn(parts[1]),
            std::stod(parts[2]),
            std::stod(parts[3])
        };
    } catch (const std::exception& e) {
        throw std::runtime_error("解析参数方程时出错 (" + str + "): " + e.what());
    }
}


// 核心计算逻辑，现在同时输出点和它们的区间
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges, // 新增的输出参数
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    try {
        out_points.clear();
        out_ranges.clear();

        const auto thread_count = std::thread::hardware_concurrency();
        oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

        Uniforms uniforms = {{screen_width, screen_height}, zoom, {offset_x, offset_y}};
        double aspect_ratio = uniforms.screen_dimensions.x / uniforms.screen_dimensions.y;
        double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
        double centered_y_0 = -(0.0 * 2.0 - 1.0);
        Vec2 world_origin = { (centered_x_0 / uniforms.zoom) + uniforms.offset.x, (centered_y_0 / uniforms.zoom) + uniforms.offset.y };
        double world_per_pixel_x = (2.0 * aspect_ratio) / (uniforms.zoom * uniforms.screen_dimensions.x);
        double world_per_pixel_y = -2.0 / (uniforms.zoom * uniforms.screen_dimensions.y);

        oneapi::tbb::task_group task_group;
        oneapi::tbb::combinable<ThreadCacheForTiling> thread_local_caches;

        std::vector<AlignedVector<RPNToken>> implicit_programs, implicit_programs_for_check;
        AlignedVector<ExplicitFunction> explicit_programs;
        AlignedVector<ParametricFunction> parametric_programs;

        for(const auto& str : implicit_rpn_list) {
            auto prog = parse_rpn(str);
            implicit_programs.push_back(prog);
            for(auto& t : prog) if(t.type == RPNTokenType::SAFE_LN) t.type = RPNTokenType::CHECK_LN;
            implicit_programs_for_check.push_back(prog);
        }
        for(const auto& str : explicit_rpn_list) {
            explicit_programs.push_back({parse_rpn(str)});
        }
        for(const auto& str : parametric_rpn_list) {
            parametric_programs.push_back(parse_parametric_string(str));
        }

        // 在运行时根据输入动态确定总函数数量
        const size_t total_functions = implicit_programs.size() + explicit_programs.size() + parametric_programs.size();
        // 创建一个大小在运行时决定的“桶”的集合
        std::vector<oneapi::tbb::concurrent_vector<PointData>> per_function_buffers(total_functions);

        const double world_x_start = world_origin.x;
        const double world_x_end = world_origin.x + screen_width * world_per_pixel_x;
        const double world_y_max = world_origin.y;
        const double world_y_min = world_origin.y + screen_height * world_per_pixel_y;
        const double max_dist_sq = std::pow(world_per_pixel_x, 2);
        const int max_depth = 15;
        const auto num_chunks = thread_count * 16;

        const unsigned int num_tiles_w = (unsigned int)(screen_width + TILE_W - 1) / TILE_W;
        const unsigned int num_tiles_h = (unsigned int)(screen_height + TILE_H - 1) / TILE_H;
        for (size_t func_idx = 0; func_idx < implicit_programs.size(); ++func_idx) {
            for (unsigned int tile_idx = 0; tile_idx < num_tiles_w * num_tiles_h; ++tile_idx) {
                task_group.run([=, &per_function_buffers, &thread_local_caches, &implicit_programs, &implicit_programs_for_check] {
                    ThreadCacheForTiling& cache = thread_local_caches.local();
                    unsigned int tile_y = tile_idx / num_tiles_w;
                    unsigned int tile_x = tile_idx % num_tiles_w;
                    unsigned int x_start = tile_x * TILE_W;
                    unsigned int y_start = tile_y * TILE_H;
                    unsigned int x_end = std::min(x_start + TILE_W, (unsigned int)screen_width);
                    unsigned int y_end = std::min(y_start + TILE_H, (unsigned int)screen_height);
                    process_tile(world_origin, world_per_pixel_x, world_per_pixel_y,
                                 implicit_programs[func_idx], implicit_programs_for_check[func_idx], (unsigned int)func_idx,
                                 x_start, x_end, y_start, y_end, cache, per_function_buffers[func_idx]);
                });
            }
        }

        const unsigned int explicit_index_offset = (unsigned int)implicit_programs.size();
        for (unsigned int func_idx = 0; func_idx < explicit_programs.size(); ++func_idx) {
            const auto& fn_data = explicit_programs[func_idx];
            const double chunk_width = (world_x_end - world_x_start) / num_chunks;
            for (int i = 0; i < num_chunks; ++i) {
                task_group.run([=, &per_function_buffers] {
                    const unsigned int final_func_idx = func_idx + explicit_index_offset;
                    process_explicit_chunk(world_y_min, world_y_max, world_x_start + i * chunk_width, world_x_start + (i + 1) * chunk_width,
                                           fn_data.rpn, max_dist_sq, max_depth,
                                           per_function_buffers[final_func_idx], final_func_idx);
                });
            }
        }

        const unsigned int parametric_index_offset = explicit_index_offset + (unsigned int)explicit_programs.size();
        for (unsigned int func_idx = 0; func_idx < parametric_programs.size(); ++func_idx) {
            const auto& fn_data = parametric_programs[func_idx];
            const double t_chunk_width = (fn_data.t_max - fn_data.t_min) / num_chunks;
            for (int i = 0; i < num_chunks; ++i) {
                task_group.run([=, &per_function_buffers] {
                    const unsigned int final_func_idx = func_idx + parametric_index_offset;
                    process_parametric_chunk(world_y_min, world_y_max, world_x_start, world_x_end,
                                             fn_data.t_min + i * t_chunk_width, fn_data.t_min + (i + 1) * t_chunk_width,
                                             fn_data.rpn_x, fn_data.rpn_y,
                                             max_dist_sq, max_depth,
                                             per_function_buffers[final_func_idx], final_func_idx);
                });
            }
        }

        task_group.wait();

        size_t total_points = 0;
        for(const auto& buffer : per_function_buffers) {
            total_points += buffer.size();
        }
        out_points.reserve(total_points);
        out_ranges.reserve(total_functions);

        // 在合并时动态构建区间信息
        uint32_t current_start_index = 0;
        for (const auto& buffer : per_function_buffers) {
            uint32_t count = (uint32_t)buffer.size();
            out_ranges.push_back({current_start_index, count});
            if (count > 0) {
                out_points.insert(out_points.end(), buffer.begin(), buffer.end());
            }
            current_start_index += count;
        }

    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("C++ Calculation Error (std::exception): ") + e.what());
    }
    catch (...) {
        throw std::runtime_error("C++ Calculation Error: An unknown, non-standard exception was caught.");
    }
}
// ===============================================================
// === 平台特定入口点 (WASM vs Native EXE) ===
// ===============================================================

#ifdef __EMSCRIPTEN__

AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer; // WASM的区间缓冲区

void calculate_points_for_wasm(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> ordered_absolute_points;
    // 将全局区间缓冲区传递给核心函数进行填充
    calculate_points_core(
        ordered_absolute_points,
        wasm_function_ranges_buffer,
        implicit_rpn_list, explicit_rpn_list, parametric_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    wasm_final_contiguous_buffer.resize(ordered_absolute_points.size());
    for (size_t i = 0; i < ordered_absolute_points.size(); ++i) {
        const auto& absolute_point = ordered_absolute_points[i];
        auto& local_point = wasm_final_contiguous_buffer[i];

        local_point.position.x = absolute_point.position.x - offset_x;
        local_point.position.y = absolute_point.position.y - offset_y;
        local_point.function_index = absolute_point.function_index;
    }
}

uintptr_t get_points_ptr() {
    return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data());
}
size_t get_points_size() {
    return wasm_final_contiguous_buffer.size();
}

// 导出区间数据的函数
uintptr_t get_function_ranges_ptr() {
    return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data());
}
size_t get_function_ranges_size() {
    return wasm_function_ranges_buffer.size();
}


EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");
    emscripten::value_object<Vec2>("Vec2")
        .field("x", &Vec2::x)
        .field("y", &Vec2::y);
    emscripten::value_object<PointData>("PointData")
        .field("position", &PointData::position)
        .field("function_index", &PointData::function_index);

    // 绑定FunctionRange结构体，以便JS可以理解它
    emscripten::value_object<FunctionRange>("FunctionRange")
        .field("start_index", &FunctionRange::start_index)
        .field("point_count", &FunctionRange::point_count);

    emscripten::function("calculate_points", &calculate_points_for_wasm);
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);

    // 绑定获取区间信息的函数
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else
// --- 版本 2: 普通 EXE 编译版本 ---

// 函数现在返回一个包含点和区间的pair
std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> final_points_aligned;
    AlignedVector<FunctionRange> final_ranges_aligned;
    calculate_points_core(
        final_points_aligned,
        final_ranges_aligned,
        implicit_rpn_list, explicit_rpn_list, parametric_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );
    return {
        std::vector<PointData>(final_points_aligned.begin(), final_points_aligned.end()),
        std::vector<FunctionRange>(final_ranges_aligned.begin(), final_ranges_aligned.end())
    };
}

int main() {
    try {
        std::vector<std::string> implicit_rpn = {
            "x x * y y * + 4 -", // <--- 错误的 RPN 用于测试

        };
        std::vector<std::string> explicit_rpn = {

        };
        std::vector<std::string> parametric_rpn = {

        };

        double offset_x = 0.0;
        double offset_y = 0.0;
        double zoom = 0.1;
        double screen_width = 2560.0;
        double screen_height = 1600.0;

        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        // 接收返回的pair
        auto results = calculate_points_for_native(
            implicit_rpn, explicit_rpn, parametric_rpn,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
        const auto& final_points = results.first;
        const auto& final_ranges = results.second;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---" << std::endl;
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总共生成了 " << final_points.size() << " 个点。" << std::endl;

        // 打印区间信息到控制台
        std::cout << "\n--- 函数点数据区间 ---" << std::endl;
        for (size_t i = 0; i < final_ranges.size(); ++i) {
            const auto& range = final_ranges[i];
            std::cout << "函数 " << i << ": "
                      << "起始索引 " << range.start_index
                      << ", 点数量 " << range.point_count << std::endl;
        }

        std::cout << "\n正在将结果保存到 points.txt (x y index 格式)..." << std::endl;
        std::ofstream output_file("points.txt");
        if (!output_file.is_open()) {
            std::cerr << "错误: 无法打开文件 points.txt 进行写入！" << std::endl;
            return 1;
        }
        output_file << std::fixed << std::setprecision(12);
        for (const auto& p : final_points) {
            output_file << p.position.x << " " << p.position.y << " " << p.function_index << "\n";
        }
        output_file.close();
        std::cout << "保存成功！" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n!!! 程序遇到严重错误 !!!\n";
        std::cerr << "错误详情: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#endif // __EMSCRIPTEN__