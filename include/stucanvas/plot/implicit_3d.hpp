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
    template <typename T, typename TFunc>
    struct IntervalPlot3DDescriptor
    {
        using Scalar = T;
        TFunc function;
        std::vector<Point3D<T>>* result;

        uint64_t cpu_threads = 0;
        Scalar x_min = -10, x_max = 10;
        Scalar y_min = -10, y_max = 10;
        Scalar z_min = -10, z_max = 10;

        uint64_t max_recursion_depth = 24;
        Scalar sampling_threshold = 0.1;


        bool use_de_refinement = true;
        Scalar verification_epsilon = 1e-5;
        uint64_t de_population_size = 200;
        uint64_t de_max_generations = 60;
    };

    namespace utils
    {
        /**
         * @brief Differential Evolution (DE) Optimizer
         *
         * Algorithm Theory:
         * Storn, R., Price, K. (1997). "Differential evolution – a simple and efficient
         * heuristic for global optimization over continuous spaces."
         * Journal of Global Optimization, 11, 341-359.
         * Ref: https://www.cp.eng.chula.ac.th/~prabhas//teaching/ec/ec2012/storn_price_de.pdf
         *
         * Implementation Note:
         * This implementation is independently developed for StuPlot, with
         * algorithmic patterns and robustness strategies inspired by the
         * Boost.Math differential_evolution implementation.
         */
        template <typename T, typename TFunc>
        Point3D<T> find_minimum_de_3d(const TFunc& f, T x0, T x1, T y0, T y1, T z0, T z1, uint32_t np,
                                      uint32_t max_gen)
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<double> dist_01(0.0, 1.0);

            struct Agent
            {
                T x, y, z, score;
            };
            std::vector<Agent> pop;
            pop.reserve(np);


            auto evaluate = [&](const T& cx, const T& cy, const T& cz) -> T
            {
                try
                {
                    auto res = f(cx, cy, cz);

                    return abs(res);
                }
                catch (...)
                {
                    return std::numeric_limits<T>::max();
                }
            };


            for (uint32_t i = 0; i < np; ++i)
            {
                T rx = x0 + static_cast<T>(dist_01(gen)) * (x1 - x0);
                T ry = y0 + static_cast<T>(dist_01(gen)) * (y1 - y0);
                T rz = z0 + static_cast<T>(dist_01(gen)) * (z1 - z0);
                T rs = evaluate(rx, ry, rz);
                pop.push_back({std::move(rx), std::move(ry), std::move(rz), std::move(rs)});
            }

            const T F = static_cast<T>(0.6);
            const T CR = static_cast<T>(0.7);


            for (uint32_t g = 0; g < max_gen; ++g)
            {
                for (uint32_t i = 0; i < np; ++i)
                {
                    uint32_t r1, r2, r3;
                    do { r1 = gen() % np; }
                    while (r1 == i);
                    do { r2 = gen() % np; }
                    while (r2 == i || r2 == r1);
                    do { r3 = gen() % np; }
                    while (r3 == i || r3 == r1 || r3 == r2);

                    T trial_x, trial_y, trial_z;


                    auto mutate_axis = [&](const T& current, const T& v1, const T& v2, const T& v3, const T& min_v,
                                           const T& max_v)
                    {
                        if (dist_01(gen) < static_cast<double>(CR))
                        {
                            T v = v1 + F * (v2 - v3);
                            return (v < min_v) ? min_v : (v > max_v ? max_v : v);
                        }
                        return current;
                    };

                    trial_x = mutate_axis(pop[i].x, pop[r1].x, pop[r2].x, pop[r3].x, x0, x1);
                    trial_y = mutate_axis(pop[i].y, pop[r1].y, pop[r2].y, pop[r3].y, y0, y1);
                    trial_z = mutate_axis(pop[i].z, pop[r1].z, pop[r2].z, pop[r3].z, z0, z1);

                    T ns = evaluate(trial_x, trial_y, trial_z);


                    bool current_is_bad = isnan(static_cast<double>(pop[i].score));
                    bool trial_is_good = !isnan(static_cast<double>(ns));

                    if (trial_is_good && (current_is_bad || ns < pop[i].score))
                    {
                        pop[i].x = std::move(trial_x);
                        pop[i].y = std::move(trial_y);
                        pop[i].z = std::move(trial_z);
                        pop[i].score = std::move(ns);
                    }
                }
            }

            auto it = std::min_element(pop.begin(), pop.end(), [](const auto& a, const auto& b)
            {
                return a.score < b.score;
            });
            return {it->x, it->y, it->z};
        }


        template <typename T>
        inline IntervalSet<T> evaluate_ia_3d_internal(const Interval<T>& ix, const Interval<T>& iy,
                                                      const Interval<T>& iz, const auto& f)
        {
            return f(IntervalSet<T>(ix), IntervalSet<T>(iy), IntervalSet<T>(iz));
        }
    }


    template <typename T, typename TFunc>
    void plot_interval_3D(const IntervalPlot3DDescriptor<T, TFunc>& desc)
    {
        if (!desc.result) return;
        desc.result->clear();


        struct Cube
        {
            T x0, x1, y0, y1, z0, z1;
            uint64_t depth;
        };
        std::vector<Cube> leaf_tasks;


        std::stack<Cube> stack;
        stack.push({desc.x_min, desc.x_max, desc.y_min, desc.y_max, desc.z_min, desc.z_max, 0});

        while (!stack.empty())
        {
            Cube t = stack.top();
            stack.pop();


            auto res_ia = utils::evaluate_ia_3d_internal<T>(
                Interval<T>(t.x0, t.x1), Interval<T>(t.y0, t.y1), Interval<T>(t.z0, t.z1), desc.function);

            if (!utils::possible_root(res_ia)) continue;


            if (t.depth < desc.max_recursion_depth &&
                ((t.x1 - t.x0) > desc.sampling_threshold ||
                    (t.y1 - t.y0) > desc.sampling_threshold ||
                    (t.z1 - t.z0) > desc.sampling_threshold))
            {
                T mx = (t.x0 + t.x1) * 0.5;
                T my = (t.y0 + t.y1) * 0.5;
                T mz = (t.z0 + t.z1) * 0.5;
                uint64_t nd = t.depth + 1;


                stack.push({t.x0, mx, t.y0, my, t.z0, mz, nd});
                stack.push({mx, t.x1, t.y0, my, t.z0, mz, nd});
                stack.push({t.x0, mx, my, t.y1, t.z0, mz, nd});
                stack.push({mx, t.x1, my, t.y1, t.z0, mz, nd});
                stack.push({t.x0, mx, t.y0, my, mz, t.z1, nd});
                stack.push({mx, t.x1, t.y0, my, mz, t.z1, nd});
                stack.push({t.x0, mx, my, t.y1, mz, t.z1, nd});
                stack.push({mx, t.x1, my, t.y1, mz, t.z1, nd});
            }
            else
            {
                leaf_tasks.push_back(t);
            }
        }


        std::mutex result_mutex;

        utils::parallel_for(static_cast<size_t>(0), leaf_tasks.size(), [&](size_t start, size_t end)
        {
            std::vector<Point3D<T>> local_points;
            local_points.reserve(end - start);

            for (size_t i = start; i < end; ++i)
            {
                const auto& c = leaf_tasks[i];

                if (desc.use_de_refinement)
                {
                    Point3D<T> best = utils::find_minimum_de_3d(
                        desc.function, c.x0, c.x1, c.y0, c.y1, c.z0, c.z1,
                        static_cast<uint32_t>(desc.de_population_size),
                        static_cast<uint32_t>(desc.de_max_generations)
                    );

                    const T eps = desc.verification_epsilon;

                    auto verify_ia = utils::evaluate_ia_3d_internal<T>(
                        Interval<T>(best.x - eps, best.x + eps),
                        Interval<T>(best.y - eps, best.y + eps),
                        Interval<T>(best.z - eps, best.z + eps),
                        desc.function
                    );

                    if (utils::possible_root(verify_ia) && !utils::is_unbounded(verify_ia))
                    {
                        local_points.push_back(best);
                    }
                }
                else
                {
                    local_points.push_back({(c.x0 + c.x1) * 0.5, (c.y0 + c.y1) * 0.5, (c.z0 + c.z1) * 0.5});
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
    }


    template <typename TFunc, typename T, typename... Args>
    IntervalPlot3DDescriptor(TFunc, std::vector<Point3D<T>>*, Args...)
        -> IntervalPlot3DDescriptor<T, TFunc>;
}
