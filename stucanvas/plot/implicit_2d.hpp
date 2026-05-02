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
    struct IntervalPlot2DDescriptor
    {
        using Scalar = T;
        TFunc function;
        std::vector<Point2D<T>>* result;
        uint64_t cpu_threads = 0;
        Scalar x_min = -10;
        Scalar x_max = 10;
        Scalar y_min = -10;
        Scalar y_max = 10;
        uint64_t max_recursion_depth = 200;
        Scalar sampling_threshold = 0.01;
        bool use_de_refinement = true;
        Scalar verification_epsilon = 1e-5;
        uint64_t de_population_size = 500;
        uint64_t de_max_generations = 40;
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
        Point2D<T> find_minimum_de(const TFunc& f, T x0, T x1, T y0, T y1, uint32_t np, uint32_t max_gen)
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<double> dist_01(0.0, 1.0);

            struct Agent
            {
                T x, y, score;
            };
            std::vector<Agent> pop;
            pop.reserve(np);

            auto evaluate = [&](const T& cx, const T& cy) -> T
            {
                try
                {
                    auto res = f(cx, cy);
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
                T rs = evaluate(rx, ry);
                pop.push_back({std::move(rx), std::move(ry), std::move(rs)});
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

                    T vx = pop[r1].x + F * (pop[r2].x - pop[r3].x);
                    T vy = pop[r1].y + F * (pop[r2].y - pop[r3].y);

                    T nx = (dist_01(gen) < static_cast<double>(CR)) ? clamp(vx, x0, x1) : pop[i].x;
                    T ny = (dist_01(gen) < static_cast<double>(CR)) ? clamp(vy, y0, y1) : pop[i].y;

                    T ns = evaluate(nx, ny);

                    bool current_is_bad = isnan(static_cast<double>(pop[i].score));
                    bool trial_is_good = !isnan(static_cast<double>(ns));

                    if (trial_is_good && (current_is_bad || ns < pop[i].score))
                    {
                        pop[i] = {std::move(nx), std::move(ny), std::move(ns)};
                    }
                }
            }

            auto it = std::min_element(pop.begin(), pop.end(), [](const auto& a, const auto& b)
            {
                return a.score < b.score;
            });
            return {it->x, it->y};
        }

        template <typename T, typename TFunc>
        IntervalSet<T> evaluate_ia(T x0, T x1, T y0, T y1, const TFunc& f)
        {
            return f(IntervalSet<T>(Interval<T>(x0, x1)),
                     IntervalSet<T>(Interval<T>(y0, y1)));
        }
    }

    /**
     * Theoretical Foundation:
     * Jeff Tupper. "Reliable Two-Dimensional Graphing Methods for Mathematical
     * Formulae with Two Free Variables." SIGGRAPH 2001.
     * University of Toronto.
     *
     * This implementation applies Tupper's interval arithmetic principles to
     * ensure topological correctness and handle mathematical singularities.
     */
    template <typename T, typename TFunc>
    void plot_interval_2D(const IntervalPlot2DDescriptor<T, TFunc>& desc)
    {
        if (!desc.result) return;
        desc.result->clear();

        struct Box
        {
            T x0, x1, y0, y1;
            uint64_t depth;
        };
        std::vector<Box> leaf_tasks;


        std::stack<Box> stack;
        stack.push({desc.x_min, desc.x_max, desc.y_min, desc.y_max, 0});

        while (!stack.empty())
        {
            Box t = stack.top();
            stack.pop();

            auto res_ia = utils::evaluate_ia(t.x0, t.x1, t.y0, t.y1, desc.function);
            if (!utils::possible_root(res_ia)) continue;

            if (t.depth < desc.max_recursion_depth &&
                ((t.x1 - t.x0) > desc.sampling_threshold || (t.y1 - t.y0) > desc.sampling_threshold))
            {
                T mx = (t.x0 + t.x1) * static_cast<T>(0.5);
                T my = (t.y0 + t.y1) * static_cast<T>(0.5);
                uint64_t nd = t.depth + 1;
                stack.push({t.x0, mx, t.y0, my, nd});
                stack.push({mx, t.x1, t.y0, my, nd});
                stack.push({t.x0, mx, my, t.y1, nd});
                stack.push({mx, t.x1, my, t.y1, nd});
            }
            else
            {
                leaf_tasks.push_back(t);
            }
        }


        std::mutex result_mutex;

        utils::parallel_for(static_cast<size_t>(0), leaf_tasks.size(), [&](size_t start, size_t end)
        {
            std::vector<Point2D<T>> local_points;
            local_points.reserve(end - start);

            for (size_t i = start; i < end; ++i)
            {
                const auto& t = leaf_tasks[i];

                if (desc.use_de_refinement)
                {
                    Point2D<T> best = utils::find_minimum_de(
                        desc.function, t.x0, t.x1, t.y0, t.y1,
                        desc.de_population_size, desc.de_max_generations
                    );

                    const T eps = desc.verification_epsilon;
                    auto verify_ia = utils::evaluate_ia(best.x - eps, best.x + eps,
                                                         best.y - eps, best.y + eps, desc.function);


                    if (utils::possible_root(verify_ia) && !utils::is_unbounded(verify_ia))
                    {
                        local_points.push_back(best);
                    }
                }
                else
                {
                    local_points.push_back({(t.x0 + t.x1) * 0.5, (t.y0 + t.y1) * 0.5});
                }
            }


            if (!local_points.empty())
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                desc.result->insert(desc.result->end(),
                                    std::make_move_iterator(local_points.begin()),
                                    std::make_move_iterator(local_points.end()));
            }
        }, desc.cpu_threads);
    }

    template <typename TFunc, typename T, typename... Args>
    IntervalPlot2DDescriptor(TFunc, std::vector<Point2D<T>>*, Args...)
        -> IntervalPlot2DDescriptor<T, TFunc>;
}
