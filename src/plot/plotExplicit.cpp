// --- 文件路径: src/plot/plotExplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotExplicit.h"

struct SubdivisionTask { Vec2 p1; Vec2 p2; int depth; };
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

    // 更新调用
    double y_start = evaluate_rpn<double>(rpn_program, x_start);
    double y_end = evaluate_rpn<double>(rpn_program, x_end);

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
                // 更新调用
                const batch_type y_mid_b = evaluate_rpn<batch_type>(rpn_program, x_mid_b);
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
                    // 更新调用
                    double y_mid = evaluate_rpn<double>(rpn_program, x_mid);

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