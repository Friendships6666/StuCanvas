// --- 文件路径: src/plot/plotParametric.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotParametric.h"

struct ParametricSubdivisionTask { double t1; Vec2 p1; double t2; Vec2 p2; int depth; };
constexpr std::size_t BATCH_SIZE = batch_type::size;

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

    // 更新调用
    Vec2 p_start = { evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t_start),
                     evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t_start) };

    if (std::isfinite(p_start.x) && std::isfinite(p_start.y)) {
        all_points.emplace_back(PointData{ p_start, func_idx });
        // 更新调用
        Vec2 p_end = { evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t_end),
                       evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t_end) };
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
                // 更新调用
                const batch_type x_mid_b = evaluate_rpn<batch_type>(rpn_x, std::nullopt, std::nullopt, t_mid_b);
                const batch_type y_mid_b = evaluate_rpn<batch_type>(rpn_y, std::nullopt, std::nullopt, t_mid_b);
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
                    // 更新调用
                    Vec2 p_mid = { evaluate_rpn<double>(rpn_x, std::nullopt, std::nullopt, t_mid),
                                   evaluate_rpn<double>(rpn_y, std::nullopt, std::nullopt, t_mid) };

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