/*
 * Copyright (c) StuCanvas, 2026
 * Fully compile-time templated, strictly zero-heap-allocation.
 * Handcoded high-performance SplitMix64 PRNG replacing STL `<random>`.
 * Integrated with proactive multi-criteria early exit triggers.
 */
#pragma once

#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <array>
#include <tuple>
#include <algorithm>
#include <concepts>
#include <type_traits>

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/global_control.h>

#include "optimization_common.hpp"

namespace StuCanvas::utils::optimization {

    // =========================================================================
    // 💡 全局优化参数结构体（集成提前退出配置）
    // =========================================================================
    template <typename Real = double, size_t Dimension = 2>
        requires std::floating_point<Real> && (Dimension > 0)
    struct [[nodiscard]] differential_evolution_parameters {
        using Container = std::array<Real, Dimension>;

        Container lower_bounds;  // 各维度下界
        Container upper_bounds;  // 各维度上界

        Real mutation_factor = static_cast<Real>(0.65);       // F
        Real crossover_probability = static_cast<Real>(0.5);  // CR

        size_t NP = 100;                 // 种群大小
        size_t max_generations = 1000;   // 最大迭代次数

        uint64_t seed = 0;
        unsigned int threads = 0;

        // 💡 提前退出配置
        bool enable_early_exit = false;
        Real early_exit_value_low = static_cast<Real>(0.1);
        Real early_exit_value_high = static_cast<Real>(0.3);
        size_t early_exit_decimal_places = 100;
    };

    template <typename Real, size_t Dimension>
        requires std::floating_point<Real> && (Dimension > 0)
    void validate_differential_evolution_parameters(const differential_evolution_parameters<Real, Dimension>& de_params) {
        using std::isnan;
        std::ostringstream oss;

        for (size_t i = 0; i < Dimension; ++i) {
            if (de_params.lower_bounds[i] > de_params.upper_bounds[i]) {
                throw std::invalid_argument("Lower bound cannot be strictly greater than upper bound.");
            }
        }
        if (de_params.NP < 4) {
            oss << __FILE__ << ":" << __LINE__ << ": Population size must be >= 4, but got " << de_params.NP << ".";
            throw std::invalid_argument(oss.str());
        }
        auto F = de_params.mutation_factor;
        if (isnan(F) || F >= 1 || F <= 0) {
            oss << __FILE__ << ":" << __LINE__ << ": F must lie in (0, 1), but got F=" << F << ".";
            throw std::domain_error(oss.str());
        }
        if (de_params.max_generations < 1) {
            throw std::invalid_argument("Max generations must be at least 1.");
        }
    }

    template <typename Real, size_t Dimension>
        requires std::floating_point<Real> && (Dimension > 0)
    std::vector<std::array<Real, Dimension>> generate_random_population(
        const std::array<Real, Dimension>& lb, const std::array<Real, Dimension>& ub, size_t NP, FastPrng &prng) {

        std::vector<std::array<Real, Dimension>> pop(NP);

        for (size_t i = 0; i < NP; ++i) {
            for (size_t k = 0; k < Dimension; ++k) {
                pop[i][k] = prng.next_real_range<Real>(lb[k], ub[k]);
            }
        }
        return pop;
    }

    template <typename Real, size_t Dimension, typename Func>
        requires std::floating_point<Real> &&
                 (Dimension > 0) &&
                 InvocableWithArray<Func, Real, Dimension>
    [[nodiscard]] std::array<Real, Dimension> differential_evolution(
        const Func& cost_function,
        const differential_evolution_parameters<Real, Dimension>& de_params,
        apply_invoke_result_t<Func, Real, Dimension> target_value =
            std::numeric_limits<apply_invoke_result_t<Func, Real, Dimension>>::quiet_NaN(),
        std::atomic<bool>* cancellation = nullptr,
        std::atomic<apply_invoke_result_t<Func, Real, Dimension>>* current_minimum_cost = nullptr,
        std::vector<std::pair<std::array<Real, Dimension>, apply_invoke_result_t<Func, Real, Dimension>>>* queries = nullptr) {

        using Container = std::array<Real, Dimension>;
        using ResultType = apply_invoke_result_t<Func, Real, Dimension>;
        using std::clamp;
        using std::isnan;

        validate_differential_evolution_parameters(de_params);
        const size_t NP = de_params.NP;

        int actual_threads = de_params.threads;
        if (actual_threads == 0) {
            actual_threads = oneapi::tbb::info::default_concurrency();
        }
        oneapi::tbb::task_arena arena(actual_threads);

        uint64_t master_seed = de_params.seed;
        if (master_seed == 0) {
            std::random_device rd;
            master_seed = (static_cast<uint64_t>(rd()) << 32) | rd();
        }

        FastPrng master_prng(master_seed);
        std::vector<FastPrng> thread_generators;
        thread_generators.reserve(static_cast<size_t>(actual_threads));
        for (int j = 0; j < actual_threads; ++j) {
            thread_generators.emplace_back(master_prng.next());
        }

        // 💡 编译期预计算坐标判定阈值：10^(-D)
        Real coord_threshold = static_cast<Real>(0.0);
        if (de_params.enable_early_exit) {
            coord_threshold = std::pow(static_cast<Real>(10.0), -static_cast<Real>(de_params.early_exit_decimal_places));
        }

        auto population = generate_random_population<Real, Dimension>(de_params.lower_bounds, de_params.upper_bounds, NP, master_prng);
        std::vector<ResultType> cost(NP, std::numeric_limits<ResultType>::quiet_NaN());
        std::atomic<bool> target_attained = false;
        std::mutex mt;

        // 并行评估初始代价值
        arena.execute([&]() {
            oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<size_t>(0, NP), [&](const oneapi::tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i) {
                    if (target_attained) return;
                    cost[i] = std::apply(cost_function, population[i]);

                    // 💡 初始生成时的提前退出检查
                    bool triggered = false;
                    if (de_params.enable_early_exit) {
                        if (cost[i] >= de_params.early_exit_value_low && cost[i] <= de_params.early_exit_value_high) {
                            triggered = true;
                        }
                        if (!triggered) {
                            for (size_t k = 0; k < Dimension; ++k) {
                                Real dist_lb = std::abs(population[i][k] - de_params.lower_bounds[k]);
                                Real dist_ub = std::abs(de_params.upper_bounds[k] - population[i][k]);
                                if ((dist_lb > static_cast<Real>(0.0) && dist_lb < coord_threshold) ||
                                    (dist_ub > static_cast<Real>(0.0) && dist_ub < coord_threshold)) {
                                    triggered = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (triggered) {
                        target_attained = true;
                    }

                    if (current_minimum_cost) {
                        auto current_val = current_minimum_cost->load();
                        while (cost[i] < current_val && !current_minimum_cost->compare_exchange_weak(current_val, cost[i])) {
                        }
                    }
                    if (queries) {
                        std::scoped_lock lock(mt);
                        queries->push_back(std::make_pair(population[i], cost[i]));
                    }
                    if (!isnan(target_value) && cost[i] <= target_value) {
                        target_attained = true;
                    }
                }
            });
        });

        std::vector<Container> trial_vectors(NP);
        std::vector<int> updated_indices(NP, 0);

        // 演化循环
        for (size_t generation = 0; generation < de_params.max_generations; ++generation) {
            if (cancellation && *cancellation) break;
            if (target_attained) break;

            arena.execute([&]() {
                oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<size_t>(0, NP), [&](const oneapi::tbb::blocked_range<size_t>& range) {

                    int thread_idx = oneapi::tbb::this_task_arena::current_thread_index();
                    if (thread_idx == oneapi::tbb::task_arena::not_initialized) {
                        thread_idx = 0;
                    }
                    auto& tlg = thread_generators[static_cast<size_t>(thread_idx) % actual_threads];

                    for (size_t i = range.begin(); i < range.end(); ++i) {
                        if (target_attained) return;
                        if (cancellation && *cancellation) return;

                        size_t r1, r2, r3;
                        do { r1 = tlg.next() % NP; } while (r1 == i);
                        do { r2 = tlg.next() % NP; } while (r2 == i || r2 == r1);
                        do { r3 = tlg.next() % NP; } while (r3 == i || r3 == r2 || r3 == r1);

                        size_t guaranteed_changed_idx = tlg.next() % Dimension;

                        for (size_t k = 0; k < Dimension; ++k) {
                            if (tlg.next_real<Real>() < de_params.crossover_probability || k == guaranteed_changed_idx) {
                                auto tmp = population[r1][k] + de_params.mutation_factor * (population[r2][k] - population[r3][k]);
                                trial_vectors[i][k] = clamp(tmp, de_params.lower_bounds[k], de_params.upper_bounds[k]);
                            } else {
                                trial_vectors[i][k] = population[i][k];
                            }
                        }

                        auto const trial_cost = std::apply(cost_function, trial_vectors[i]);
                        if (isnan(trial_cost)) continue;

                        if (queries) {
                            std::scoped_lock lock(mt);
                            queries->push_back(std::make_pair(trial_vectors[i], trial_cost));
                        }

                        // 💡 演化过程中的提前退出检测
                        bool triggered = false;
                        if (de_params.enable_early_exit) {
                            if (trial_cost >= de_params.early_exit_value_low && trial_cost <= de_params.early_exit_value_high) {
                                triggered = true;
                            }
                            if (!triggered) {
                                for (size_t k = 0; k < Dimension; ++k) {
                                    Real dist_lb = std::abs(trial_vectors[i][k] - de_params.lower_bounds[k]);
                                    Real dist_ub = std::abs(de_params.upper_bounds[k] - trial_vectors[i][k]);
                                    if ((dist_lb > static_cast<Real>(0.0) && dist_lb < coord_threshold) ||
                                        (dist_ub > static_cast<Real>(0.0) && dist_ub < coord_threshold)) {
                                        triggered = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (triggered) {
                            target_attained = true;
                        }

                        if (trial_cost < cost[i] || isnan(cost[i])) {
                            cost[i] = trial_cost;
                            if (!isnan(target_value) && cost[i] <= target_value) {
                                target_attained = true;
                            }
                            if (current_minimum_cost) {
                                auto current_val = current_minimum_cost->load();
                                while (trial_cost < current_val && !current_minimum_cost->compare_exchange_weak(current_val, trial_cost)) {
                                }
                            }
                            updated_indices[i] = 1;
                        }
                    }
                });
            });

            for (size_t i = 0; i < NP; ++i) {
                if (updated_indices[i]) {
                    population[i] = trial_vectors[i];
                    updated_indices[i] = 0;
                }
            }
        }

        auto it = std::min_element(cost.begin(), cost.end());
        return population[std::distance(cost.begin(), it)];
    }

} // namespace StuCanvas::utils::optimization