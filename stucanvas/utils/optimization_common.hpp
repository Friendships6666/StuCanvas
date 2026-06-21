/*
 * Copyright (c) StuCanvas, 2026
 * Shared common utilities for global optimization (concepts and FastPrng).
 */
#pragma once

#include <array>
#include <tuple>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <type_traits>

namespace StuCanvas::utils::optimization {

    // =========================================================================
    // 💡 编译期约束 (Concepts) 与返回值自适应推导助手
    // =========================================================================
    template <typename Func, typename Real, size_t Dimension>
    concept InvocableWithArray = requires(const Func& f, const std::array<Real, Dimension>& arr) {
        { std::apply(f, arr) } -> std::floating_point;
    };

    // 💡 核心修复：用于推导通过 std::apply 解包打散调用后的真实返回值类型
    template <typename Func, typename Real, size_t Dimension>
    using apply_invoke_result_t = decltype(std::apply(std::declval<const Func&>(), std::declval<const std::array<Real, Dimension>&>()));

    // =========================================================================
    // 💡 高性能 SplitMix64 伪随机数发生器
    // =========================================================================
    class [[nodiscard]] FastPrng {
    private:
        uint64_t state;

    public:
        explicit FastPrng(uint64_t seed) noexcept {
            state = seed ^ 0x9e3779b97f4a7c15ULL;
            next();
        }

        uint64_t next() noexcept {
            uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        }

        template <typename Real>
        Real next_real() noexcept {
            uint64_t val = next();
            if constexpr (std::is_same_v<Real, float>) {
                return static_cast<Real>((val >> 40) * 0x1.0p-24f);
            } else {
                return static_cast<Real>((val >> 11) * 0x1.0p-53);
            }
        }

        template <typename Real>
        Real next_real_range(Real lb, Real ub) noexcept {
            return lb + next_real<Real>() * (ub - lb);
        }
    };

} // namespace StuCanvas::utils::optimization