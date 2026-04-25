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
    template <typename T, typename TFuncX, typename TFuncY, typename TFuncZ>
    struct ParametricPlot3DDescriptor
    {
        using Scalar = T;
        TFuncX x_func;
        TFuncY y_func;
        TFuncZ z_func;
        std::vector<Point3D<T>>* result;

        uint64_t cpu_threads = 0;
        Scalar x_min = -10, x_max = 10;
        Scalar y_min = -10, y_max = 10;
        Scalar z_min = -10, z_max = 10;

        Scalar u_min = 0, u_max = 6.28;
        Scalar v_min = 0, v_max = 6.28;

        uint64_t max_recursion_depth = 10;
        Scalar point_spacing = 0.1;
    };

    namespace utils
    {
        template <typename T>
        bool box_intersects_viewport_3d(const IntervalSet<T>& ix, const IntervalSet<T>& iy,
                                        const IntervalSet<T>& iz,
                                        T vx1, T vx2, T vy1, T vy2, T vz1, T vz2)
        {
            if (ix.is_poisoned() || iy.is_poisoned() || iz.is_poisoned()) return false;
            auto check = [](const auto& set, T v1, T v2)
            {
                for (const auto& iv : set.intervals)
                {
                    if (!(iv.lower > v2 || iv.upper < v1)) return true;
                }
                return false;
            };
            return check(ix, vx1, vx2) && check(iy, vy1, vy2) && check(iz, vz1, vz2);
        }
    }


    template <typename T, typename TFuncX, typename TFuncY, typename TFuncZ>
    void plot_parametric_3D(const ParametricPlot3DDescriptor<T, TFuncX, TFuncY, TFuncZ>& desc)
    {
        if (!desc.result) return;
        desc.result->clear();


        struct UVTask
        {
            T u0, u1, v0, v1;
            uint64_t depth;
        };
        std::vector<UVTask> leaf_tasks;


        std::stack<UVTask> stack;
        stack.push({desc.u_min, desc.u_max, desc.v_min, desc.v_max, 0});

        while (!stack.empty())
        {
            UVTask cur = stack.top();
            stack.pop();


            auto iu = IntervalSet<T>(Interval<T>(cur.u0, cur.u1));
            auto iv = IntervalSet<T>(Interval<T>(cur.v0, cur.v1));

            auto ix = desc.x_func(iu, iv);
            auto iy = desc.y_func(iu, iv);
            auto iz = desc.z_func(iu, iv);


            if (!utils::box_intersects_viewport_3d(ix, iy, iz,
                                                    desc.x_min, desc.x_max, desc.y_min, desc.y_max, desc.z_min,
                                                    desc.z_max))
            {
                continue;
            }

            if (cur.depth < desc.max_recursion_depth)
            {
                T mu = (cur.u0 + cur.u1) * static_cast<T>(0.5);
                T mv = (cur.v0 + cur.v1) * static_cast<T>(0.5);
                uint64_t nd = cur.depth + 1;

                stack.push({cur.u0, mu, cur.v0, mv, nd});
                stack.push({mu, cur.u1, cur.v0, mv, nd});
                stack.push({cur.u0, mu, mv, cur.v1, nd});
                stack.push({mu, cur.u1, mv, cur.v1, nd});
            }
            else
            {
                leaf_tasks.push_back(cur);
            }
        }


        std::mutex result_mutex;

        utils::parallel_for(static_cast<size_t>(0), leaf_tasks.size(), [&](size_t start, size_t end)
        {
            std::vector<Point3D<T>> local_points;

            for (size_t i = start; i < end; ++i)
            {
                const auto& task = leaf_tasks[i];

                auto iu = IntervalSet<T>(Interval<T>(task.u0, task.u1));
                auto iv = IntervalSet<T>(Interval<T>(task.v0, task.v1));
                auto ix = desc.x_func(iu, iv);
                auto iy = desc.y_func(iu, iv);
                auto iz = desc.z_func(iu, iv);


                if (utils::is_unbounded(ix) || utils::is_unbounded(iy) || utils::is_unbounded(iz))
                {
                    continue;
                }


                auto int_x = ix.intersect(Interval<T>(desc.x_min, desc.x_max));
                auto int_y = iy.intersect(Interval<T>(desc.y_min, desc.y_max));
                auto int_z = iz.intersect(Interval<T>(desc.z_min, desc.z_max));

                if (int_x.intervals.empty() || int_y.intervals.empty() || int_z.intervals.empty()) continue;


                for (const auto& bx : int_x.intervals)
                {
                    for (const auto& by : int_y.intervals)
                    {
                        for (const auto& bz : int_z.intervals)
                        {
                            using std::ceil;
                            using std::floor;
                            T s_ix = ceil(bx.lower / desc.point_spacing);
                            T e_ix = floor(bx.upper / desc.point_spacing);
                            T s_iy = ceil(by.lower / desc.point_spacing);
                            T e_iy = floor(by.upper / desc.point_spacing);
                            T s_iz = ceil(bz.lower / desc.point_spacing);
                            T e_iz = floor(bz.upper / desc.point_spacing);


                            if (s_ix > e_ix) s_ix = e_ix = floor(bx.center() / desc.point_spacing);
                            if (s_iy > e_iy) s_iy = e_iy = floor(by.center() / desc.point_spacing);
                            if (s_iz > e_iz) s_iz = e_iz = floor(bz.center() / desc.point_spacing);

                            for (T idx = s_ix; idx <= e_ix; idx += 1.0)
                            {
                                for (T idy = s_iy; idy <= e_iy; idy += 1.0)
                                {
                                    for (T idz = s_iz; idz <= e_iz; idz += 1.0)
                                    {
                                        local_points.push_back({
                                            idx * desc.point_spacing,
                                            idy * desc.point_spacing,
                                            idz * desc.point_spacing
                                        });
                                    }
                                }
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
                if (a.y != b.y) return a.y < b.y;
                return a.z < b.z;
            });
            res.erase(std::unique(res.begin(), res.end(), [](const auto& a, const auto& b)
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }), res.end());
        }
    }


    template <typename TFuncX, typename TFuncY, typename TFuncZ, typename T, typename... Args>
    ParametricPlot3DDescriptor(TFuncX, TFuncY, TFuncZ, std::vector<Point3D<T>>*, Args...)
        -> ParametricPlot3DDescriptor<T, TFuncX, TFuncY, TFuncZ>;
}
