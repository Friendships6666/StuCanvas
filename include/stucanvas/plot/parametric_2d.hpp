/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/
#pragma once
#include "../types/point.hpp"
#include "../utils/interval.hpp"
#include "../utils/parallel_for.hpp"

namespace StuCanvas
{
    template <typename T, typename TFuncX, typename TFuncY>
    struct ParametricPlot2DDescriptor
    {
        using Scalar = T;
        TFuncX x_func;
        TFuncY y_func;
        std::vector<Point2D<T>>* result;

        uint64_t cpu_threads = 0;
        Scalar x_min = -10, x_max = 10;
        Scalar y_min = -10, y_max = 10;
        Scalar t_min = 0, t_max = 6.28;

        uint64_t max_recursion_depth = 20;
        Scalar point_spacing = 0.05;
    };

    namespace utils
    {
        template <typename T>
        bool box_intersects_viewport(const IntervalSet<T>& ix, const IntervalSet<T>& iy,
                                     T vx1, T vx2, T vy1, T vy2)
        {
            if (ix.is_poisoned() || iy.is_poisoned()) return false;
            auto check = [](const auto& set, T v1, T v2)
            {
                for (const auto& iv : set.intervals)
                {
                    if (!(iv.lower > v2 || iv.upper < v1)) return true;
                }
                return false;
            };
            return check(ix, vx1, vx2) && check(iy, vy1, vy2);
        }
    }


    template <typename T, typename TFuncX, typename TFuncY>
    void plot_parametric_2D(const ParametricPlot2DDescriptor<T, TFuncX, TFuncY>& desc)
    {
        if (!desc.result) return;
        desc.result->clear();

        struct TTask
        {
            T t0, t1;
            uint64_t depth;
        };
        std::vector<TTask> leaf_tasks;


        std::stack<TTask> stack;
        stack.push({desc.t_min, desc.t_max, 0});

        while (!stack.empty())
        {
            TTask cur = stack.top();
            stack.pop();

            auto ix = desc.x_func(IntervalSet<T>(Interval<T>(cur.t0, cur.t1)));
            auto iy = desc.y_func(IntervalSet<T>(Interval<T>(cur.t0, cur.t1)));

            if (!utils::box_intersects_viewport(ix, iy, desc.x_min, desc.x_max, desc.y_min, desc.y_max))
            {
                continue;
            }

            if (cur.depth < desc.max_recursion_depth)
            {
                T mid = (cur.t0 + cur.t1) * static_cast<T>(0.5);
                stack.push({cur.t0, mid, cur.depth + 1});
                stack.push({mid, cur.t1, cur.depth + 1});
            }
            else
            {
                leaf_tasks.push_back(cur);
            }
        }


        std::mutex result_mutex;

        utils::parallel_for(static_cast<size_t>(0), leaf_tasks.size(), [&](size_t start, size_t end)
        {
            std::vector<Point2D<T>> local_points;

            for (size_t i = start; i < end; ++i)
            {
                const auto& task = leaf_tasks[i];

                auto ix = desc.x_func(IntervalSet<T>(Interval<T>(task.t0, task.t1)));
                auto iy = desc.y_func(IntervalSet<T>(Interval<T>(task.t0, task.t1)));

                if (utils::is_unbounded(ix) || utils::is_unbounded(iy))
                {
                    continue;
                }

                auto intersect_x = ix.intersect(Interval<T>(desc.x_min, desc.x_max));
                auto intersect_y = iy.intersect(Interval<T>(desc.y_min, desc.y_max));

                if (intersect_x.intervals.empty() || intersect_y.intervals.empty()) continue;

                for (const auto& box_x : intersect_x.intervals)
                {
                    for (const auto& box_y : intersect_y.intervals)
                    {
                        T start_idx_x = ceil(box_x.lower / desc.point_spacing);
                        T end_idx_x = floor(box_x.upper / desc.point_spacing);
                        T start_idx_y = ceil(box_y.lower / desc.point_spacing);
                        T end_idx_y = floor(box_y.upper / desc.point_spacing);


                        if (start_idx_x > end_idx_x)
                        {
                            start_idx_x = end_idx_x = floor(box_x.center() / desc.point_spacing);
                        }
                        if (start_idx_y > end_idx_y)
                        {
                            start_idx_y = end_idx_y = floor(box_y.center() / desc.point_spacing);
                        }

                        for (T ix_idx = start_idx_x; ix_idx <= end_idx_x; ix_idx += static_cast<T>(1.0))
                        {
                            for (T iy_idx = start_idx_y; iy_idx <= end_idx_y; iy_idx += static_cast<T>(1.0))
                            {
                                local_points.push_back({ix_idx * desc.point_spacing, iy_idx * desc.point_spacing});
                            }
                        }
                    }
                }
            }

            if (!local_points.empty())
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                desc.result->insert(desc.result->end(),
                                    std::make_move_iterator(local_points.begin()),
                                    std::make_move_iterator(local_points.end()));
            }
        }, static_cast<uint32_t>(desc.cpu_threads));


        if (!desc.result->empty())
        {
            auto& res = *(desc.result);
            std::sort(res.begin(), res.end(), [](const auto& a, const auto& b)
            {
                if (a.x != b.x) return a.x < b.x;
                return a.y < b.y;
            });

            res.erase(std::unique(res.begin(), res.end(), [](const auto& a, const auto& b)
            {
                return a.x == b.x && a.y == b.y;
            }), res.end());
        }
    }

    template <typename TFuncX, typename TFuncY, typename T, typename... Args>
    ParametricPlot2DDescriptor(TFuncX, TFuncY, std::vector<Point2D<T>>*, Args...)
        -> ParametricPlot2DDescriptor<T, TFuncX, TFuncY>;
}
