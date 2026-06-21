/*
 * Copyright (c) StuCanvas, 2026
 * Fully compile-time templated L-SHADE algorithm.
 * Parallelized via Intel oneTBB and powered by FastPrng.
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
#include <numeric>
#include <algorithm>
#include <concepts>
#include <type_traits>

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/global_control.h>

// 💡 引入共享基础组件
#include "optimization_common.hpp"

namespace StuCanvas::utils::optimization {

    // 💡 纯解析、零随机库开销的正态分布与柯西分布采样器
    template <typename Real>
    Real sample_normal(FastPrng& prng, Real mean, Real stddev) noexcept {
        constexpr Real PI = static_cast<Real>(3.14159265358979323846);
        Real u1 = prng.next_real<Real>();
        Real u2 = prng.next_real<Real>();
        while (u1 <= std::numeric_limits<Real>::min()) {
            u1 = prng.next_real<Real>();
        }
        Real z = std::sqrt(static_cast<Real>(-2.0) * std::log(u1)) * std::cos(static_cast<Real>(2.0 * PI) * u2);
        return mean + stddev * z;
    }

    template <typename Real>
    Real sample_cauchy(FastPrng& prng, Real location, Real scale) noexcept {
        constexpr Real PI = static_cast<Real>(3.14159265358979323846);
        Real u = prng.next_real<Real>();
        return location + scale * std::tan(PI * (u - static_cast<Real>(0.5)));
    }

    // =========================================================================
    // 💡 2. L-SHADE 专有超参数结构体
    // =========================================================================
    template <typename Real = double, size_t Dimension = 2>
        requires std::floating_point<Real> && (Dimension > 0)
    struct [[nodiscard]] l_shade_parameters {
        using Container = std::array<Real, Dimension>;

        Container lower_bounds;  // 各维度下界
        Container upper_bounds;  // 各维度上界

        size_t NP_init = 100;                 // 初始种群大小
        size_t NP_min = 4;                    // 极限最小种群大小
        size_t max_evaluations = 150000;      // 最大函数评估次数（NFE 限制）

        size_t H = 6;                         // 历史内存表大小
        Real pbest_ratio = static_cast<Real>(0.11);   // pbest 优秀个体选拔比例 (通常为 0.11)
        Real archive_ratio = static_cast<Real>(1.4);  // 外部存档 A 大小与当前种群的比例 (通常为 1.4)

        uint64_t seed = 0;
        unsigned int threads = 0;
    };

    template <typename Real, size_t Dimension>
        requires std::floating_point<Real> && (Dimension > 0)
    void validate_l_shade_parameters(const l_shade_parameters<Real, Dimension>& params) {
        if (params.NP_init < params.NP_min) {
            throw std::invalid_argument("NP_init must be greater than or equal to NP_min.");
        }
        if (params.NP_min < 4) {
            throw std::invalid_argument("NP_min must be at least 4 for mutation operations.");
        }
        for (size_t i = 0; i < Dimension; ++i) {
            if (params.lower_bounds[i] > params.upper_bounds[i]) {
                throw std::invalid_argument("Lower bound cannot be strictly greater than upper bound.");
            }
        }
    }

    template <typename Real, size_t Dimension>
    std::vector<std::array<Real, Dimension>> generate_initial_population(
        const std::array<Real, Dimension>& lb, const std::array<Real, Dimension>& ub, size_t NP, FastPrng& prng) {

        std::vector<std::array<Real, Dimension>> pop(NP);
        for (size_t i = 0; i < NP; ++i) {
            for (size_t k = 0; k < Dimension; ++k) {
                pop[i][k] = prng.next_real_range<Real>(lb[k], ub[k]);
            }
        }
        return pop;
    }

    // =========================================================================
    // 💡 3. 修正后的 L-SHADE 核心算法接口（同步采用 apply_invoke_result_t）
    // =========================================================================
    template <typename Real, size_t Dimension, typename Func>
        requires std::floating_point<Real> &&
                 (Dimension > 0) &&
                 InvocableWithArray<Func, Real, Dimension>
    [[nodiscard]] std::array<Real, Dimension> l_shade(
        const Func& cost_function,
        const l_shade_parameters<Real, Dimension>& de_params,
        apply_invoke_result_t<Func, Real, Dimension> target_value =
            std::numeric_limits<apply_invoke_result_t<Func, Real, Dimension>>::quiet_NaN(),
        std::atomic<bool>* cancellation = nullptr,
        std::atomic<apply_invoke_result_t<Func, Real, Dimension>>* current_minimum_cost = nullptr,
        std::vector<std::pair<std::array<Real, Dimension>, apply_invoke_result_t<Func, Real, Dimension>>>* queries = nullptr) {

        using Container = std::array<Real, Dimension>;
        using ResultType = apply_invoke_result_t<Func, Real, Dimension>;
        using std::clamp;
        using std::isnan;

        validate_l_shade_parameters(de_params);

        size_t NP_curr = de_params.NP_init;
        size_t NFE = 0;

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

        const size_t H = de_params.H;
        std::vector<Real> M_F(H, static_cast<Real>(0.5));
        std::vector<Real> M_CR(H, static_cast<Real>(0.5));
        size_t memory_index = 0;

        auto population = generate_initial_population<Real, Dimension>(de_params.lower_bounds, de_params.upper_bounds, NP_curr, master_prng);
        std::vector<ResultType> cost(NP_curr, std::numeric_limits<ResultType>::quiet_NaN());
        std::atomic<bool> target_attained = false;
        std::mutex mt;

        arena.execute([&]() {
            oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<size_t>(0, NP_curr), [&](const oneapi::tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i) {
                    cost[i] = std::apply(cost_function, population[i]);
                    if (current_minimum_cost) {
                        auto current_val = current_minimum_cost->load();
                        while (cost[i] < current_val && !current_minimum_cost->compare_exchange_weak(current_val, cost[i])) {}
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
        NFE += NP_curr;

        std::vector<Container> archive;
        archive.reserve(static_cast<size_t>(de_params.NP_init * de_params.archive_ratio));

        std::vector<Container> trial_vectors(de_params.NP_init);

        while (NFE < de_params.max_evaluations) {
            if (cancellation && *cancellation) break;
            if (target_attained) break;

            size_t NP_new = static_cast<size_t>(std::round(
                (static_cast<double>(de_params.NP_min) - static_cast<double>(de_params.NP_init)) / de_params.max_evaluations * NFE + de_params.NP_init
            ));
            NP_new = std::clamp(NP_new, de_params.NP_min, de_params.NP_init);

            if (NP_new < NP_curr) {
                std::vector<size_t> idx(NP_curr);
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                    if (isnan(cost[a])) return false;
                    if (isnan(cost[b])) return true;
                    return cost[a] < cost[b];
                });

                std::vector<Container> next_pop(NP_new);
                std::vector<ResultType> next_cost(NP_new);
                for (size_t i = 0; i < NP_new; ++i) {
                    next_pop[i] = std::move(population[idx[i]]);
                    next_cost[i] = cost[idx[i]];
                }
                population = std::move(next_pop);
                cost = std::move(next_cost);
                NP_curr = NP_new;

                size_t A_max = static_cast<size_t>(std::round(NP_curr * de_params.archive_ratio));
                if (archive.size() > A_max) {
                    archive.resize(A_max);
                }
            }

            std::vector<size_t> sorted_idx(NP_curr);
            std::iota(sorted_idx.begin(), sorted_idx.end(), 0);
            std::sort(sorted_idx.begin(), sorted_idx.end(), [&](size_t a, size_t b) {
                if (isnan(cost[a])) return false;
                if (isnan(cost[b])) return true;
                return cost[a] < cost[b];
            });

            std::vector<Real> success_F(NP_curr, static_cast<Real>(-1.0));
            std::vector<Real> success_CR(NP_curr, static_cast<Real>(-1.0));
            std::vector<Real> delta_f(NP_curr, static_cast<Real>(-1.0));
            std::vector<int> updated_indices(NP_curr, 0);

            arena.execute([&]() {
                oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<size_t>(0, NP_curr), [&](const oneapi::tbb::blocked_range<size_t>& range) {

                    int thread_idx = oneapi::tbb::this_task_arena::current_thread_index();
                    if (thread_idx == oneapi::tbb::task_arena::not_initialized) thread_idx = 0;
                    auto& tlg = thread_generators[static_cast<size_t>(thread_idx) % actual_threads];

                    for (size_t i = range.begin(); i < range.end(); ++i) {
                        if (target_attained) return;
                        if (cancellation && *cancellation) return;

                        size_t r_i = tlg.next() % H;

                        Real CR_i;
                        if (M_CR[r_i] == -1.0) {
                            CR_i = static_cast<Real>(0.0);
                        } else {
                            CR_i = sample_normal(tlg, M_CR[r_i], static_cast<Real>(0.1));
                            CR_i = std::clamp(CR_i, static_cast<Real>(0.0), static_cast<Real>(1.0));
                        }

                        Real F_i;
                        do {
                            F_i = sample_cauchy(tlg, M_F[r_i], static_cast<Real>(0.1));
                        } while (F_i <= static_cast<Real>(0.0));
                        if (F_i > static_cast<Real>(1.0)) {
                            F_i = static_cast<Real>(1.0);
                        }

                        size_t pbest_pool_size = std::max(static_cast<size_t>(2), static_cast<size_t>(std::round(NP_curr * 0.11)));
                        size_t pbest_idx = tlg.next() % pbest_pool_size;
                        size_t p_best_ind = sorted_idx[pbest_idx];

                        size_t r1;
                        do { r1 = tlg.next() % NP_curr; } while (r1 == i || r1 == p_best_ind);

                        size_t r2;
                        size_t union_size = NP_curr + archive.size();
                        do { r2 = tlg.next() % union_size; } while (r2 == i || r2 == p_best_ind || r2 == r1);

                        const auto& x_r2 = (r2 < NP_curr) ? population[r2] : archive[r2 - NP_curr];

                        size_t guaranteed_changed_idx = tlg.next() % Dimension;
                        for (size_t k = 0; k < Dimension; ++k) {
                            if (tlg.next_real<Real>() < CR_i || k == guaranteed_changed_idx) {
                                auto tmp = population[i][k] + F_i * (population[p_best_ind][k] - population[i][k]) + F_i * (population[r1][k] - x_r2[k]);
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

                        if (trial_cost < cost[i] || isnan(cost[i])) {
                            success_F[i] = F_i;
                            success_CR[i] = CR_i;
                            delta_f[i] = isnan(cost[i]) ? static_cast<Real>(1e-5) : std::abs(cost[i] - trial_cost);

                            cost[i] = trial_cost;
                            if (!isnan(target_value) && cost[i] <= target_value) {
                                target_attained = true;
                            }
                            if (current_minimum_cost) {
                                auto current_val = current_minimum_cost->load();
                                while (trial_cost < current_val && !current_minimum_cost->compare_exchange_weak(current_val, trial_cost)) {}
                            }
                            updated_indices[i] = 1;
                        }
                    }
                });
            });

            NFE += NP_curr;

            size_t A_max = static_cast<size_t>(std::round(NP_curr * de_params.archive_ratio));
            for (size_t i = 0; i < NP_curr; ++i) {
                if (updated_indices[i]) {
                    if (A_max > 0) {
                        if (archive.size() < A_max) {
                            archive.push_back(population[i]);
                        } else {
                            size_t replace_idx = master_prng.next() % A_max;
                            archive[replace_idx] = population[i];
                        }
                    }
                    population[i] = trial_vectors[i];
                }
            }

            Real sum_delta_f = 0.0;
            Real sum_F_num = 0.0, sum_F_den = 0.0;
            Real sum_CR = 0.0;
            size_t num_success = 0;

            for (size_t i = 0; i < NP_curr; ++i) {
                if (delta_f[i] > 0.0) {
                    sum_delta_f += delta_f[i];
                    sum_CR += delta_f[i] * success_CR[i];
                    sum_F_num += delta_f[i] * (success_F[i] * success_F[i]);
                    sum_F_den += delta_f[i] * success_F[i];
                    num_success++;
                }
            }

            if (num_success > 0 && sum_delta_f > 0.0) {
                Real S_F = (sum_F_den > 0.0) ? (sum_F_num / sum_F_den) : static_cast<Real>(0.5);
                Real S_CR = sum_CR / sum_delta_f;

                M_F[memory_index] = S_F;
                M_CR[memory_index] = S_CR;
                memory_index = (memory_index + 1) % H;
            }
        }

        auto it = std::min_element(cost.begin(), cost.end());
        return population[std::distance(cost.begin(), it)];
    }

} // namespace StuCanvas::utils::optimization