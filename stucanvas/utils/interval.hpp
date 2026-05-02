/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/
#pragma once
#include "../utils/math_traits.hpp"

namespace StuCanvas
{
    namespace utils
    {
        struct FastRNG
        {
            uint32_t s[4]{};

            FastRNG(uint32_t seed)
            {
                for (int i = 0; i < 4; ++i) s[i] = seed = seed * 1812433253U + i;
            }

            static uint32_t rotl(const uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

            uint32_t operator()()
            {
                const uint32_t res = rotl(s[0] + s[3], 7) + s[0];
                const uint32_t t = s[1] << 9;
                s[2] ^= s[0];
                s[3] ^= s[1];
                s[1] ^= s[2];
                s[0] ^= s[3];
                s[2] ^= t;
                s[3] = rotl(s[3], 11);
                return res;
            }

            double next_double() { return (operator()() >> 8) * (1.0 / 16777216.0); }
        };
    }


    template <typename T>
    struct Constant
    {
        static T pi() { return acos(static_cast<T>(-1)); }
        static T hpi() { return pi() / static_cast<T>(2); }
        static T pi2() { return pi() * static_cast<T>(2); }
        static T inf() { return std::numeric_limits<T>::infinity(); }
        static T undefined_val() { return static_cast<T>(-0x1.BAADC0DEp+300L); }
    };

    template <typename T>
    struct IntervalSet;

    template <typename U, typename T>
    concept Scalable = std::is_arithmetic_v<U> || std::convertible_to<U, T>;

    template <typename U>
    concept Arithmetic = std::is_arithmetic_v<U>;


    template <typename T>
    struct Interval
    {
        using value_type = T;
        T lower, upper;

        constexpr Interval(T v = 0) : lower(v), upper(v)
        {
        }

        constexpr Interval(T l, T u) : lower(min(l, u)), upper(max(l, u))
        {
        }


        static constexpr Interval poisoned()
        {
            return Interval(Constant<T>::undefined_val(), Constant<T>::undefined_val());
        }

        static constexpr Interval universe()
        {
            return Interval(-Constant<T>::inf(), Constant<T>::inf());
        }


        template <typename SetType>
        explicit Interval(const SetType& set)
        {
            if (set.is_poisoned())
            {
                lower = Constant<T>::undefined_val();
                upper = Constant<T>::undefined_val();
            }

            else
            {
                lower = set.intervals[0].lower;
                upper = set.intervals[0].upper;
                for (const auto& iv : set.intervals)
                {
                    if (iv.lower < lower) lower = iv.lower;
                    if (iv.upper > upper) upper = iv.upper;
                }
            }
        }

        T center() const { return (lower + upper) * static_cast<T>(0.5); }


        T width() const { return upper - lower; }

        [[nodiscard]] bool is_poisoned() const
        {
            return lower == Constant<T>::undefined_val() || upper == Constant<T>::undefined_val();
        }

        [[nodiscard]] bool is_nan() const
        {
            return isnan(lower) || isnan(upper);
        }

        [[nodiscard]] bool is_universe() const
        {
            return isinf(lower) && isinf(upper);
        }


        friend constexpr Interval operator-(const Interval& a)
        {
            if (a.is_poisoned()) return poisoned();
            return Interval(-a.upper, -a.lower);
        }


        friend constexpr Interval operator+(const Interval& a, const Interval& b)
        {
            if (a.is_poisoned() || b.is_poisoned()) return poisoned();
            T c = a.lower + b.lower;
            T d = a.upper + b.upper;
            if (isnan(c) || isnan(d))
            {
                return Interval(universe());
            }
            return Interval(c, d);
        }


        friend constexpr Interval operator-(const Interval& a, const Interval& b)
        {
            if (a.is_poisoned() || b.is_poisoned()) return poisoned();
            T c = a.lower - b.upper;
            T d = a.upper - b.lower;
            if (isnan(c) || isnan(d))
            {
                return Interval(universe());
            }
            return Interval(c, d);
        }


        friend constexpr Interval operator*(const Interval& a, const Interval& b)
        {
            if (a.is_poisoned() || b.is_poisoned()) return poisoned();
            T p1 = a.lower * b.lower, p2 = a.lower * b.upper;
            T p3 = a.upper * b.lower, p4 = a.upper * b.upper;
            if (isnan(p1) || isnan(p2) || isnan(p3) || isnan(p4))
            {
                return Interval(universe());
            }
            return Interval(min({p1, p2, p3, p4}), max({p1, p2, p3, p4}));
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


        friend IntervalSet<T> operator/(const Interval& lhs, const Interval& rhs)
        {
            if (lhs.is_poisoned() || rhs.is_poisoned()) return Interval<T>::poisoned();

            T zero(0);
            T inf = Constant<T>::inf();


            if (rhs.lower > zero || rhs.upper < zero)
            {
                T d1 = lhs.lower / rhs.lower, d2 = lhs.lower / rhs.upper;
                T d3 = lhs.upper / rhs.lower, d4 = lhs.upper / rhs.upper;

                if (isnan(d1) || isnan(d2) || isnan(d3) || isnan(d4))
                {
                    return Interval(universe());
                }

                return IntervalSet<T>(Interval<T>(min({d1, d2, d3, d4}), max({d1, d2, d3, d4})));
            }


            if (rhs.lower == zero && rhs.upper == zero) return IntervalSet<T>(universe());
            if (rhs.lower < zero && rhs.upper > zero && lhs.lower < zero && lhs.upper > zero)
                return IntervalSet<T>(
                    universe());

            T a = lhs.lower, b = lhs.upper, c = rhs.lower, d = rhs.upper;


            if (c < zero && d > zero)
            {
                if (b < zero) return IntervalSet<T>({Interval<T>(-inf, b / d), Interval<T>(b / c, inf)});
                if (a > zero) return IntervalSet<T>({Interval<T>(-inf, a / c), Interval<T>(a / d, inf)});
                return IntervalSet<T>(universe());
            }


            if (c == zero)
            {
                if (b < zero) return IntervalSet<T>(Interval<T>(-inf, b / d));
                if (a > zero) return IntervalSet<T>(Interval<T>(a / d, inf));
                return IntervalSet<T>(universe());
            }
            if (d == zero)
            {
                if (b < zero) return IntervalSet<T>(Interval<T>(b / c, inf));
                if (a > zero) return IntervalSet<T>(Interval<T>(-inf, a / c));
                return IntervalSet<T>(universe());
            }
            return IntervalSet<T>(universe());
        }
    };


    template <typename T>
    struct IntervalSet
    {
        using value_type = T;
        std::vector<Interval<T>> intervals;

        IntervalSet() = default;

        IntervalSet(Interval<T> iv)
        {
            if (iv.is_poisoned())
            {
                intervals.clear();
                intervals.emplace_back(iv);
            }
            else if (iv.is_nan())
            {
                intervals.emplace_back(Interval<T>::universe());
            }
            else
            {
                intervals.emplace_back(iv);
            }
        }


        template <typename U>
            requires (std::is_arithmetic_v<U> || std::convertible_to<U, T>)
            && (!std::is_same_v<std::remove_cvref_t<U>, Interval<T>>)
            && (!std::is_same_v<std::remove_cvref_t<U>, IntervalSet<T>>)
        IntervalSet(U val)
        {
            T t_val = static_cast<T>(val);


            if (t_val == Constant<T>::undefined_val())
            {
                intervals.emplace_back(Interval<T>::poisoned());
            }
            else
            {
                if (!isnan(t_val))
                {
                    intervals.emplace_back(t_val);
                }
                else
                {
                    intervals.emplace_back(Interval<T>::universe());
                }
            }
        }

        IntervalSet(std::initializer_list<Interval<T>> list) : intervals(list) { normalize(); }

        IntervalSet intersect(const Interval<T>& other) const
        {
            if (this->is_poisoned()) return *this;

            IntervalSet result;
            for (const auto& iv : this->intervals)
            {
                T new_lower = max(iv.lower, other.lower);
                T new_upper = min(iv.upper, other.upper);
                if (new_lower <= new_upper)
                {
                    result.intervals.emplace_back(Interval<T>(new_lower, new_upper));
                }
            }
            return result;
        }

        IntervalSet intersect(const IntervalSet& other) const
        {
            if (this->is_poisoned()) return *this;
            if (other.is_poisoned()) return other;

            IntervalSet result;
            for (const auto& iv1 : this->intervals)
            {
                for (const auto& iv2 : other.intervals)
                {
                    T new_lower = max(iv1.lower, iv2.lower);
                    T new_upper = min(iv1.upper, iv2.upper);

                    if (new_lower <= new_upper)
                    {
                        result.intervals.emplace_back(Interval<T>(new_lower, new_upper));
                    }
                }
            }
            return result;
        }

        [[nodiscard]] bool is_poisoned() const
        {
            for (const auto& iv : intervals) if (iv.is_poisoned()) return true;
            return false;
        }

        [[nodiscard]] bool is_universe() const
        {
            for (const auto& iv : intervals) if (iv.is_universe()) return true;
            return false;
        }

        void normalize()
        {
            if (is_poisoned())
            {
                intervals.clear();
                intervals.emplace_back(Interval<T>::poisoned());
                return;
            }
            if (intervals.size() < 2) return;
            std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b)
            {
                return a.lower < b.lower;
            });
            std::vector<Interval<T>> merged;
            merged.emplace_back(intervals[0]);
            for (size_t i = 1; i < intervals.size(); ++i)
            {
                if (intervals[i].lower <= merged.back().upper)
                    merged.back().upper = max(merged.back().upper, intervals[i].upper);
                else merged.emplace_back(intervals[i]);
            }
            intervals = std::move(merged);
        }

        Interval<T> to_hull() const
        {
            if (is_poisoned()) return Interval<T>::poisoned();
            if (intervals.empty()) return {0, 0};
            T l = intervals.front().lower, u = intervals.front().upper;
            for (const auto& iv : intervals)
            {
                l = min(l, iv.lower);
                u = max(u, iv.upper);
            }
            return {l, u};
        }
    };

    template <typename T>
    [[nodiscard]] IntervalSet<T> operator+(const IntervalSet<T>& a, const IntervalSet<T>& b)
    {
        if (a.is_poisoned() || b.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> r;
        for (const auto& i : a.intervals)
            for (const auto& j : b.intervals) r.intervals.emplace_back(i + j);
        r.normalize();
        return r;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> operator-(const IntervalSet<T>& a, const IntervalSet<T>& b)
    {
        if (a.is_poisoned() || b.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> r;
        for (const auto& i : a.intervals)
            for (const auto& j : b.intervals) r.intervals.emplace_back(i - j);
        r.normalize();
        return r;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> operator*(const IntervalSet<T>& a, const IntervalSet<T>& b)
    {
        if (a.is_poisoned() || b.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> r;
        for (const auto& i : a.intervals)
            for (const auto& j : b.intervals) r.intervals.emplace_back(i * j);
        r.normalize();
        return r;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> operator/(const IntervalSet<T>& a, const IntervalSet<T>& b)
    {
        if (a.is_poisoned() || b.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> r;
        for (const auto& i : a.intervals)
            for (const auto& j : b.intervals)
            {
                auto res = i / j;
                r.intervals.insert(r.intervals.end(), res.intervals.begin(), res.intervals.end());
            }
        r.normalize();
        return r;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> operator-(const IntervalSet<T>& a)
    {
        if (a.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> r;
        for (const auto& iv : a.intervals) r.intervals.emplace_back(-iv);
        r.normalize();
        return r;
    }


    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator+(const IntervalSet<T>& a, U b)
    {
        return a + IntervalSet<T>(static_cast<T>(b));
    }

    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator+(U a, const IntervalSet<T>& b)
    {
        return b + a;
    }


    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator-(const IntervalSet<T>& a, U b)
    {
        return a - IntervalSet<T>(static_cast<T>(b));
    }

    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator-(U a, const IntervalSet<T>& b)
    {
        return IntervalSet<T>(static_cast<T>(a)) - b;
    }


    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator*(const IntervalSet<T>& a, U b)
    {
        return a * IntervalSet<T>(static_cast<T>(b));
    }

    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator*(U a, const IntervalSet<T>& b)
    {
        return b * a;
    }


    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator/(const IntervalSet<T>& a, U b)
    {
        return a / IntervalSet<T>(static_cast<T>(b));
    }

    template <typename T, typename U> requires Scalable<U, T>
    IntervalSet<T> operator/(U a, const IntervalSet<T>& b)
    {
        return IntervalSet<T>(static_cast<T>(a)) / b;
    }

    template <typename T, typename Func>
    IntervalSet<T> map_set(const IntervalSet<T>& set, Func&& f)
    {
        if (set.is_poisoned()) return Interval<T>::poisoned();
        IntervalSet<T> res;
        for (const auto& iv : set.intervals)
        {
            auto out = f(iv);
            if constexpr (std::is_same_v<decltype(out), Interval<T>>) res.intervals.emplace_back(out);
            else res.intervals.insert(res.intervals.end(), out.intervals.begin(), out.intervals.end());
        }
        res.normalize();
        return res;
    }


    template <typename T>
    Interval<T> sin(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T p2 = Constant<T>::pi2(), hp = Constant<T>::hpi();
        if (iv.upper - iv.lower >= p2) return {-1, 1};
        auto contains = [&](T val) { return floor((iv.upper - val) / p2) > floor((iv.lower - val) / p2); };
        T l = sin(iv.lower), u = sin(iv.upper);
        T rl = min(l, u), ru = max(l, u);
        if (contains(hp)) ru = 1;
        if (contains(hp * 3)) rl = -1;
        return {rl, ru};
    }

    template <typename T>
    IntervalSet<T> sin(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return sin(i); });
    }

    template <typename T>
    Interval<T> cos(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T p2 = Constant<T>::pi2(), p = Constant<T>::pi();
        if (iv.upper - iv.lower >= p2) return {-1, 1};
        auto contains = [&](T val) { return floor((iv.upper - val) / p2) > floor((iv.lower - val) / p2); };
        T l = cos(iv.lower), u = cos(iv.upper);
        T rl = min(l, u), ru = max(l, u);
        if (contains(T(0))) ru = 1;
        if (contains(p)) rl = -1;
        return {rl, ru};
    }

    template <typename T>
    IntervalSet<T> cos(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return cos(i); });
    }


    template <typename T>
    IntervalSet<T> tan(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T p = Constant<T>::pi(), hp = Constant<T>::hpi(), inf = Constant<T>::inf();
        if (iv.upper - iv.lower >= p) return Interval<T>::universe();
        T k = floor((iv.lower - hp) / p);
        T s = (k + 1) * p + hp;
        if (iv.upper > s) return IntervalSet<T>({{-inf, tan(iv.upper)}, {tan(iv.lower), inf}});
        return Interval<T>(tan(iv.lower), tan(iv.upper));
    }

    template <typename T>
    IntervalSet<T> tan(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return tan(i); });
    }


    template <typename T>
    Interval<T> log(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T zero(0), inf = Constant<T>::inf();
        if (iv.upper <= zero) return Interval<T>::poisoned();
        if (iv.lower <= zero) return Interval<T>(-inf, log(iv.upper));
        return Interval<T>(log(iv.lower), log(iv.upper));
    }

    template <typename T>
    IntervalSet<T> log(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return log(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> log10(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T zero(0), inf = Constant<T>::inf();

        if (iv.upper <= zero) return Interval<T>::poisoned();

        T lo = (iv.lower <= zero) ? -inf : log10(iv.lower);
        T hi = log10(iv.upper);
        return Interval<T>(lo, hi);
    }

    template <typename T>
    IntervalSet<T> log10(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return log10(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> log2(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T zero(0), inf = Constant<T>::inf();

        if (iv.upper <= zero) return Interval<T>::poisoned();

        T lo = (iv.lower <= zero) ? -inf : log2(iv.lower);
        T hi = log2(iv.upper);
        return Interval<T>(lo, hi);
    }

    template <typename T>
    IntervalSet<T> log2(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return log2(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> log1p(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T neg_one(-1);
        T inf = Constant<T>::inf();


        if (iv.upper <= neg_one) return Interval<T>::poisoned();


        T lo = (iv.lower <= neg_one) ? -inf : log1p(iv.lower);
        T hi = log1p(iv.upper);

        return Interval<T>(lo, hi);
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> log1p(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return log1p(i); });
    }

    template <typename T>
    [[nodiscard]] Interval<T> exp2(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        if (iv.is_universe()) return Interval<T>::universe();


        return Interval<T>(exp2(iv.lower), exp2(iv.upper));
    }

    template <typename T>
    IntervalSet<T> exp2(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return exp2(i); });
    }


    template <typename T>
    Interval<T> sqrt(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T zero(0);
        if (iv.upper < zero) return Interval<T>::poisoned();
        if (iv.lower < zero) return Interval<T>(zero, sqrt(iv.upper));
        return Interval<T>(sqrt(iv.lower), sqrt(iv.upper));
    }

    template <typename T>
    IntervalSet<T> sqrt(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return sqrt(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> exp(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        if (iv.is_universe()) return Interval<T>::universe();


        return Interval<T>(exp(iv.lower), exp(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> exp(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();

        return map_set(s, [](const auto& i)
        {
            return exp(i);
        });
    }


    template <typename T>
    [[nodiscard]] Interval<T> abs(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        if (iv.lower >= static_cast<T>(0))
        {
            return iv;
        }


        if (iv.upper <= static_cast<T>(0))
        {
            return Interval<T>(-iv.upper, -iv.lower);
        }
        T m = max(abs(iv.lower), abs(iv.upper));
        return Interval<T>(static_cast<T>(0), m);
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> abs(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();

        return map_set(s, [](const auto& i)
        {
            return abs(i);
        });
    }

    namespace utils
    {
        template <typename T>
        T scalar_round(T v)
        {
            if (isnan(static_cast<double>(v))) return Constant<T>::undefined_val();
            if (isinf(static_cast<double>(v))) return v;
            return round(v);
        }
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> round(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T r_low = utils::scalar_round(iv.lower);
        T r_high = utils::scalar_round(iv.upper);


        if (r_low == r_high) return IntervalSet<T>(r_low);


        if (r_high - r_low > static_cast<T>(64))
        {
            return Interval<T>(r_low, r_high);
        }


        IntervalSet<T> res;
        for (T v = r_low; v <= r_high; v += static_cast<T>(1.0))
        {
            res.intervals.emplace_back(v);
        }
        return res;
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> round(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return round(i); });
    }

    template <typename T>
    IntervalSet<T> floor(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T f_low = floor(iv.lower);
        T f_high = floor(iv.upper);

        if (f_low == f_high) return Interval<T>(f_low);


        if (f_high - f_low > 32) return Interval<T>(f_low, f_high);

        IntervalSet<T> res;
        for (T v = f_low; v <= f_high; v += 1.0)
        {
            res.intervals.emplace_back(v);
        }
        return res;
    }

    template <typename T>
    IntervalSet<T> floor(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return floor(i); });
    }


    template <typename T>
    IntervalSet<T> ceil(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        T c_low = ceil(iv.lower);
        T c_high = ceil(iv.upper);
        if (c_low == c_high) return Interval<T>(c_low);
        if (c_high - c_low > 32) return Interval<T>(c_low, c_high);

        IntervalSet<T> res;
        for (T v = c_low; v <= c_high; v += 1.0)
        {
            res.intervals.emplace_back(v);
        }
        return res;
    }

    template <typename T>
    IntervalSet<T> ceil(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return ceil(i); });
    }

    template <typename T>
    IntervalSet<T> trunc(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        return Interval<T>(trunc(iv.lower), trunc(iv.upper));
    }

    template <typename T>
    IntervalSet<T> trunc(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return trunc(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> fmod(const IntervalSet<T>& x, const IntervalSet<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return Interval<T>::poisoned();


        IntervalSet<T> res = x - y * trunc(x / y);


        T y_max = 0;
        bool y_has_zero = false;
        for (const auto& iv : y.intervals)
        {
            if (iv.lower <= static_cast<T>(0) && iv.upper >= static_cast<T>(0))
            {
                y_has_zero = true;
                break;
            }
            y_max = max({y_max, abs(iv.lower), abs(iv.upper)});
        }

        if (y_has_zero) return res;


        return res.intersect(Interval<T>(-y_max, y_max));
    }


    template <typename T, Arithmetic U>
    [[nodiscard]] IntervalSet<T> fmod(const IntervalSet<T>& x, U y_val)
    {
        if (x.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        T val = static_cast<T>(y_val);

        if (val == static_cast<T>(0)) return IntervalSet<T>(Interval<T>::universe());

        return fmod(x, IntervalSet<T>(val));
    }


    template <Arithmetic U, typename T>
    [[nodiscard]] IntervalSet<T> fmod(U x_val, const IntervalSet<T>& y)
    {
        if (y.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        return fmod(IntervalSet<T>(static_cast<T>(x_val)), y);
    }


    template <typename T>
    struct is_interval_set : std::false_type
    {
    };

    template <typename T>
    struct is_interval_set<IntervalSet<T>> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool is_interval_set_v = is_interval_set<std::remove_cvref_t<T>>::value;


    template <typename T1, typename T2, typename FuncTrue, typename FuncFalse>
    auto sp_select(T1&& a, T2&& b, FuncTrue&& f_true, FuncFalse&& f_false)
    {
        using ValType = std::remove_cvref_t<T1>;


        if constexpr (!is_interval_set_v<T1> && !is_interval_set_v<T2>)
        {
            return (a > b) ? f_true() : f_false();
        }
        else
        {
            auto get_iv = []<typename U>(U&& val)
            {
                if constexpr (is_interval_set_v<U>) return val.to_hull();
                else return Interval(static_cast<std::remove_cvref_t<decltype(val.to_hull().lower)>>(val));
            };

            auto iv_a = get_iv(std::forward<T1>(a));
            auto iv_b = get_iv(std::forward<T2>(b));


            if (iv_a.is_poisoned() || iv_b.is_poisoned())
            {
                return IntervalSet(Interval<typename ValType::value_type>::poisoned());
            }


            if (iv_a.lower > iv_b.upper) return IntervalSet(f_true());
            if (iv_a.upper <= iv_b.lower) return IntervalSet(f_false());


            IntervalSet res = IntervalSet(f_true());
            IntervalSet res_f = IntervalSet(f_false());
            res.intervals.insert(res.intervals.end(), res_f.intervals.begin(), res_f.intervals.end());
            res.normalize();
            return res;
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> intersect(const IntervalSet<T>& A, const IntervalSet<T>& B)
    {
        if (A.is_poisoned() || B.is_poisoned()) [[unlikely]]
        {
            return IntervalSet<T>(Interval<T>::poisoned());
        }

        IntervalSet<T> result;


        for (const auto& a : A.intervals)
        {
            for (const auto& b : B.intervals)
            {
                T inter_lower = max(a.lower, b.lower);
                T inter_upper = min(a.upper, b.upper);


                if (inter_lower <= inter_upper)
                {
                    result.intervals.emplace_back(inter_lower, inter_upper);
                }
            }
        }


        result.normalize();

        return result;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> intersect(const IntervalSet<T>& A, const Interval<T>& b)
    {
        return intersect(A, IntervalSet<T>(b));
    }


    template <typename T>
    Interval<T> asin(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T one(1), neg_one(-1);

        if (iv.lower > one || iv.upper < neg_one)
        {
            return Interval<T>::poisoned();
        }


        T l = max(neg_one, iv.lower);
        T u = min(one, iv.upper);


        return Interval<T>(asin(l), asin(u));
    }


    template <typename T>
    IntervalSet<T> asin(const IntervalSet<T>& s)
    {
        return map_set(s, [](const auto& i) { return asin(i); });
    }

    template <typename T>
    Interval<T> acos(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T one(1), neg_one(-1);

        if (iv.lower > one || iv.upper < neg_one)
        {
            return Interval<T>::poisoned();
        }


        T l = max(neg_one, iv.lower);
        T u = min(one, iv.upper);


        return Interval<T>(acos(u), acos(l));
    }


    template <typename T>
    IntervalSet<T> acos(const IntervalSet<T>& s)
    {
        return map_set(s, [](const auto& i) { return acos(i); });
    }


    template <typename T>
    Interval<T> atan(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        return Interval<T>(atan(iv.lower), atan(iv.upper));
    }


    template <typename T>
    IntervalSet<T> atan(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return atan(i); });
    }


    template <typename T>
    struct GammaConstants
    {
        static Interval<T> min_pos_enclosure()
        {
            T pos;
            if constexpr (std::is_constructible_v<T, const char*>)
            {
                pos = T("1.461632144968362341262659542325413287465039306");
            }
            else
            {
                pos = static_cast<T>(1.461632144968362341262659542325413287465039306L);
            }
            T eps = std::numeric_limits<T>::epsilon() * T(4);
            return Interval<T>(pos - eps, pos + eps);
        }

        static Interval<T> min_lgamma_val_enclosure()
        {
            T val;
            if constexpr (std::is_constructible_v<T, const char*>)
            {
                val = T("-0.1214862905358496080955145571776915821105952");
            }
            else
            {
                val = static_cast<T>(-0.1214862905358496080955145571776915821105952L);
            }
            T eps = std::numeric_limits<T>::epsilon() * T(4);
            return Interval<T>(val - eps, val + eps);
        }

        static Interval<T> min_val_enclosure()
        {
            T val;
            if constexpr (std::is_constructible_v<T, const char*>)
            {
                val = T("0.885603194410888700278815900832524173030325705");
            }
            else
            {
                val = static_cast<T>(0.885603194410888700278815900832524173030325705L);
            }
            T eps = std::numeric_limits<T>::epsilon() * T(4);
            return Interval<T>(val - eps, val + eps);
        }
    };

    template <typename T>
    [[nodiscard]] IntervalSet<T> tgamma(const Interval<T>& iv)
    {
        if (iv.is_poisoned())
        {
            return IntervalSet<T>(Interval<T>::poisoned());
        }

        T zero(0);
        T one(1);
        T pi = Constant<T>::pi();
        T inf = Constant<T>::inf();
        if (iv.lower < zero && iv.upper > zero)
        {
            IntervalSet<T> res;
            auto left = tgamma(Interval<T>(iv.lower, zero));
            auto right = tgamma(Interval<T>(zero, iv.upper));

            res.intervals.insert(res.intervals.end(), left.intervals.begin(), left.intervals.end());
            res.intervals.insert(res.intervals.end(), right.intervals.begin(), right.intervals.end());
            res.normalize();
            return res;
        }
        if (iv.lower >= zero)
        {
            if (iv.lower == zero && iv.upper == zero)
            {
                return IntervalSet<T>(Interval<T>::universe());
            }

            auto x0_box = GammaConstants<T>::min_pos_enclosure();
            auto g0_box = GammaConstants<T>::min_val_enclosure();


            T lv = (iv.lower == zero) ? inf : tgamma(iv.lower);
            T uv = (iv.upper == zero) ? inf : tgamma(iv.upper);

            if (iv.upper < x0_box.lower)
            {
                return IntervalSet<T>(Interval<T>(uv, lv));
            }
            else if (iv.lower > x0_box.upper)
            {
                return IntervalSet<T>(Interval<T>(lv, uv));
            }
            else
            {
                T max_val = max(lv, uv);
                return IntervalSet<T>(Interval<T>(g0_box.lower, max_val));
            }
        }

        else
        {
            IntervalSet<T> g_pos = tgamma(Interval<T>(one - iv.upper, one - iv.lower));

            IntervalSet<T> s = sin(IntervalSet<T>(iv * pi));

            return IntervalSet<T>(pi) / (s * g_pos);
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> tgamma(const IntervalSet<T>& is)
    {
        if (is.is_poisoned()) return Interval<T>::poisoned();
        return map_set(is, [](const auto& iv) { return tgamma(iv); });
    }

    namespace utils
    {
        template <typename T>
        inline IntervalSet<T> lgamma_positive(const Interval<T>& iv)
        {
            auto x0_box = GammaConstants<T>::min_pos_enclosure();
            auto lg0_box = GammaConstants<T>::min_lgamma_val_enclosure();

            using std::lgamma;
            T lv = lgamma(iv.lower);
            T uv = lgamma(iv.upper);


            if (iv.upper < x0_box.lower) return Interval<T>(uv, lv);

            if (iv.lower > x0_box.upper) return Interval<T>(lv, uv);

            return Interval<T>(lg0_box.lower, std::max(lv, uv));
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> lgamma(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        T zero(0);
        T one(1);
        T pi = Constant<T>::pi();


        if (iv.lower < zero && iv.upper > zero)
        {
            IntervalSet<T> res;
            res.intervals.emplace_back(lgamma(Interval<T>(iv.lower, zero)));
            res.intervals.emplace_back(lgamma(Interval<T>(zero, iv.upper)));
            return res;
        }


        if (iv.lower >= zero)
        {
            if (iv.lower == zero && iv.upper == zero) return Interval<T>::universe();
            return utils::lgamma_positive(iv);
        }


        IntervalSet<T> ln_pi(log(pi));


        IntervalSet<T> sin_part = abs(sin(IntervalSet<T>(iv * pi)));
        IntervalSet<T> ln_abs_sin = log(sin_part);


        Interval<T> reflected_iv(one - iv.upper, one - iv.lower);
        IntervalSet<T> lg_reflected = utils::lgamma_positive(reflected_iv);


        return ln_pi - ln_abs_sin - lg_reflected;
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> lgamma(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return lgamma(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> sinh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        return Interval<T>(sinh(iv.lower), sinh(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> sinh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return sinh(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> cosh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T zero(0);
        T one(1);

        if (iv.lower <= zero && iv.upper >= zero)
        {
            T max_val = max(cosh(iv.lower), cosh(iv.upper));
            return Interval<T>(one, max_val);
        }
        else
        {
            T v1 = cosh(iv.lower);
            T v2 = cosh(iv.upper);
            return Interval<T>(min(v1, v2), max(v1, v2));
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> cosh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return cosh(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> tanh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        return Interval<T>(tanh(iv.lower), tanh(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> tanh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return tanh(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> asinh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();


        return Interval<T>(asinh(iv.lower), asinh(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> asinh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return asinh(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> acosh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T one(1);

        if (iv.upper < one)
        {
            return Interval<T>::poisoned();
        }


        T safe_lower = max(one, iv.lower);


        return Interval<T>(acosh(safe_lower), acosh(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> acosh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return acosh(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> atanh(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        T one(1), neg_one(-1);
        T inf = Constant<T>::inf();
        if (iv.lower >= one || iv.upper <= neg_one)
        {
            return Interval<T>::poisoned();
        }
        T res_lower = (iv.lower <= neg_one) ? -inf : atanh(iv.lower);
        T res_upper = (iv.upper >= one) ? inf : atanh(iv.upper);

        return Interval<T>(res_lower, res_upper);
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> atanh(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return atanh(i); });
    }

    namespace utils
    {
        template <typename T>
        bool is_integer(const T& val)
        {
            return floor(val) == val;
        }


        template <typename T>
        bool is_even(const T& val)
        {
            return floor(val * static_cast<T>(0.5)) * static_cast<T>(2.0) == val;
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> ipow(const Interval<T>& x, int n)
    {
        if (x.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());
        T zero(0), one(1);

        if (n == 0) return IntervalSet<T>(one);

        if (n > 0)
        {
            if (n % 2 != 0)
            {
                return IntervalSet<T>(Interval<T>(pow(x.lower, n), pow(x.upper, n)));
            }
            else
            {
                T p_lo = pow(x.lower, n);
                T p_hi = pow(x.upper, n);
                if (x.lower >= zero) return IntervalSet<T>(Interval<T>(p_lo, p_hi));
                if (x.upper <= zero) return IntervalSet<T>(Interval<T>(p_hi, p_lo));
                return IntervalSet<T>(Interval<T>(zero, std::max(p_lo, p_hi)));
            }
        }

        return IntervalSet<T>(one) / ipow(x, -n);
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> pow(const Interval<T>& x, const Interval<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());


        if (y.lower == y.upper)
        {
            if (floor(y.lower) == y.lower)
            {
                return ipow(x, static_cast<int>(y.lower));
            }
        }


        return exp(IntervalSet<T>(y) * log(IntervalSet<T>(x)));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> pow(const IntervalSet<T>& x, const IntervalSet<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        IntervalSet<T> result;
        for (const auto& ix : x.intervals)
        {
            for (const auto& iy : y.intervals)
            {
                IntervalSet<T> out = pow(ix, iy);
                result.intervals.insert(result.intervals.end(), out.intervals.begin(), out.intervals.end());
            }
        }
        result.normalize();
        return result;
    }


    template <typename T, typename U,
              typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] IntervalSet<T> pow(const IntervalSet<T>& x, U y_val)
    {
        if (x.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        T y_t = static_cast<T>(y_val);
        return pow(x, IntervalSet<T>(Interval<T>(y_t, y_t)));
    }


    template <typename U, typename T,
              typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] IntervalSet<T> pow(U x_val, const IntervalSet<T>& y)
    {
        if (y.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        T x_t = static_cast<T>(x_val);
        return pow(IntervalSet<T>(Interval<T>(x_t, x_t)), y);
    }


    template <typename T, typename U,
              typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] IntervalSet<T> pow(const Interval<T>& x, U y_val)
    {
        if (x.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());
        T y_t = static_cast<T>(y_val);
        return pow(x, Interval<T>(y_t, y_t));
    }

    template <typename T>
    [[nodiscard]] Interval<T> erf(const Interval<T>& iv)
    {
        if (iv.is_poisoned())
        {
            return Interval<T>::poisoned();
        }


        auto compute_erf = [](T val) -> T
        {
            if (isinf(val))
            {
                return (val > 0) ? static_cast<T>(1) : static_cast<T>(-1);
            }


            return erf(val);
        };


        return Interval<T>(compute_erf(iv.lower), compute_erf(iv.upper));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> erf(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return erf(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> erfc(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();

        auto compute_erfc = [](T val) -> T
        {
            if (isinf(val))
            {
                return (val > 0) ? static_cast<T>(0) : static_cast<T>(2);
            }

            return erfc(val);
        };


        return Interval<T>(compute_erfc(iv.upper), compute_erfc(iv.lower));
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> erfc(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return erfc(i); });
    }

    template <typename T>
    [[nodiscard]] Interval<T> cbrt(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        return Interval<T>(cbrt(iv.lower), cbrt(iv.upper));
    }

    template <typename T>
    IntervalSet<T> cbrt(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return cbrt(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> hypot(const Interval<T>& x, const Interval<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return Interval<T>::poisoned();

        auto get_abs_range = [](T lo, T hi)
        {
            if (lo <= 0 && hi >= 0) return std::make_pair(static_cast<T>(0), max(abs(lo), abs(hi)));
            return std::make_pair(min(abs(lo), abs(hi)), max(abs(lo), abs(hi)));
        };

        auto [x_min, x_max] = get_abs_range(x.lower, x.upper);
        auto [y_min, y_max] = get_abs_range(y.lower, y.upper);


        return Interval<T>(hypot(x_min, y_min), hypot(x_max, y_max));
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> hypot(const IntervalSet<T>& x, const IntervalSet<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return IntervalSet<T>::poisoned();
        IntervalSet<T> res;
        for (const auto& ix : x.intervals)
            for (const auto& iy : y.intervals)
                res.intervals.emplace_back(hypot(ix, iy));
        res.normalize();
        return res;
    }


    template <typename T>
    [[nodiscard]] Interval<T> hypot(const Interval<T>& x, const Interval<T>& y, const Interval<T>& z)
    {
        if (x.is_poisoned() || y.is_poisoned() || z.is_poisoned()) return Interval<T>::poisoned();


        auto get_abs_bounds = [](T lo, T hi)
        {
            T min_a = (lo <= 0 && hi >= 0) ? static_cast<T>(0) : min(abs(lo), abs(hi));
            T max_a = max(abs(lo), abs(hi));
            return std::make_pair(min_a, max_a);
        };

        auto [x_min, x_max] = get_abs_bounds(x.lower, x.upper);
        auto [y_min, y_max] = get_abs_bounds(y.lower, y.upper);
        auto [z_min, z_max] = get_abs_bounds(z.lower, z.upper);


        return Interval<T>(hypot(x_min, y_min, z_min),
                           hypot(x_max, y_max, z_max));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> hypot(const IntervalSet<T>& x, const IntervalSet<T>& y, const IntervalSet<T>& z)
    {
        if (x.is_poisoned() || y.is_poisoned() || z.is_poisoned())
        {
            return IntervalSet<T>(Interval<T>::poisoned());
        }

        IntervalSet<T> result;


        for (const auto& ix : x.intervals)
        {
            for (const auto& iy : y.intervals)
            {
                for (const auto& iz : z.intervals)
                {
                    result.intervals.emplace_back(hypot(ix, iy, iz));
                }
            }
        }

        result.normalize();
        return result;
    }


    template <typename T>
    [[nodiscard]] Interval<T> lerp(const Interval<T>& a, const Interval<T>& b, const Interval<T>& t)
    {
        if (a.is_poisoned() || b.is_poisoned() || t.is_poisoned()) return Interval<T>::poisoned();


        T v1 = lerp(a.lower, b.lower, t.lower);
        T v2 = lerp(a.lower, b.lower, t.upper);
        T v3 = lerp(a.lower, b.upper, t.lower);
        T v4 = lerp(a.lower, b.upper, t.upper);
        T v5 = lerp(a.upper, b.lower, t.lower);
        T v6 = lerp(a.upper, b.lower, t.upper);
        T v7 = lerp(a.upper, b.upper, t.lower);
        T v8 = lerp(a.upper, b.upper, t.upper);

        return Interval<T>(min({v1, v2, v3, v4, v5, v6, v7, v8}),
                           max({v1, v2, v3, v4, v5, v6, v7, v8}));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> lerp(const IntervalSet<T>& a, const IntervalSet<T>& b, const IntervalSet<T>& t)
    {
        if (a.is_poisoned() || b.is_poisoned() || t.is_poisoned())
        {
            return IntervalSet<T>(Interval<T>::poisoned());
        }

        IntervalSet<T> result;


        for (const auto& ia : a.intervals)
        {
            for (const auto& ib : b.intervals)
            {
                for (const auto& it : t.intervals)
                {
                    result.intervals.emplace_back(lerp(ia, ib, it));
                }
            }
        }


        result.normalize();
        return result;
    }


    template <typename T, typename U>
        requires std::is_arithmetic_v<U>
    inline IntervalSet<T> lerp(const IntervalSet<T>& a, const IntervalSet<T>& b, U t_val)
    {
        return lerp(a, b, IntervalSet<T>(static_cast<T>(t_val)));
    }


    template <typename U, typename T>
        requires std::is_arithmetic_v<U>
    inline IntervalSet<T> lerp(U a_val, U b_val, const IntervalSet<T>& t)
    {
        return lerp(IntervalSet<T>(static_cast<T>(a_val)),
                    IntervalSet<T>(static_cast<T>(b_val)), t);
    }


    template <typename T, typename U>
        requires std::is_arithmetic_v<U>
    inline IntervalSet<T> lerp(const Interval<T>& a, const Interval<T>& b, U t_val)
    {
        return lerp(IntervalSet<T>(a), IntervalSet<T>(b), IntervalSet<T>(static_cast<T>(t_val)));
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> atan2(const Interval<T>& y, const Interval<T>& x)
    {
        if (x.is_poisoned() || y.is_poisoned()) return Interval<T>::poisoned();

        T zero(0);
        T pi = Constant<T>::pi();


        if (x.lower <= zero && x.upper >= zero && y.lower <= zero && y.upper >= zero)
        {
            return Interval<T>(-pi, pi);
        }


        if (x.lower < zero && y.lower < zero && y.upper > zero)
        {
            IntervalSet<T> res;
            auto lower_half = atan2(Interval<T>(y.lower, zero), x);
            auto upper_half = atan2(Interval<T>(zero, y.upper), x);

            res.intervals.insert(res.intervals.end(), lower_half.intervals.begin(), lower_half.intervals.end());
            res.intervals.insert(res.intervals.end(), upper_half.intervals.begin(), upper_half.intervals.end());
            res.normalize();
            return res;
        }


        T v1 = atan2(y.lower, x.lower);
        T v2 = atan2(y.lower, x.upper);
        T v3 = atan2(y.upper, x.lower);
        T v4 = atan2(y.upper, x.upper);

        return Interval<T>(min({v1, v2, v3, v4}), max({v1, v2, v3, v4}));
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> atan2(const IntervalSet<T>& y, const IntervalSet<T>& x)
    {
        if (x.is_poisoned() || y.is_poisoned()) return Interval<T>::poisoned();

        IntervalSet<T> result;
        for (const auto& iy : y.intervals)
        {
            for (const auto& ix : x.intervals)
            {
                auto out = atan2(iy, ix);

                result.intervals.insert(result.intervals.end(), out.intervals.begin(), out.intervals.end());
            }
        }
        result.normalize();
        return result;
    }

    namespace utils
    {
        template <typename T>
        inline T scalar_rint(T v)
        {
            if (isnan(static_cast<double>(v))) return Constant<T>::undefined_val();
            if (isinf(static_cast<double>(v))) return v;

            return rint(v);
        }
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> rint(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        T r_low = utils::scalar_rint(iv.lower);
        T r_high = utils::scalar_rint(iv.upper);


        if (r_low == r_high) return IntervalSet<T>(r_low);


        if (abs(r_high - r_low) > static_cast<T>(32))
        {
            return IntervalSet<T>(Interval<T>(min(r_low, r_high), max(r_low, r_high)));
        }


        IntervalSet<T> res;
        T v = r_low;

        T step = (r_high > r_low) ? static_cast<T>(1.0) : static_cast<T>(-1.0);

        while (true)
        {
            res.intervals.emplace_back(v);
            if (v == r_high) break;
            v += step;
        }
        res.normalize();
        return res;
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> rint(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return rint(i); });
    }


    template <typename T>
    [[nodiscard]] Interval<T> expm1(const Interval<T>& iv)
    {
        if (iv.is_poisoned()) return Interval<T>::poisoned();
        if (iv.is_universe()) return Interval<T>::universe();


        return Interval<T>(expm1(iv.lower), expm1(iv.upper));
    }

    template <typename T>
    [[nodiscard]] IntervalSet<T> expm1(const IntervalSet<T>& s)
    {
        if (s.is_poisoned()) return Interval<T>::poisoned();
        return map_set(s, [](const auto& i) { return expm1(i); });
    }


    template <typename T>
    [[nodiscard]] IntervalSet<T> remainder(const IntervalSet<T>& x, const IntervalSet<T>& y)
    {
        if (x.is_poisoned() || y.is_poisoned()) return Interval<T>::poisoned();


        bool y_has_zero = false;
        for (const auto& iy : y.intervals)
        {
            if (iy.lower <= 0 && iy.upper >= 0)
            {
                y_has_zero = true;
                break;
            }
        }

        if (y_has_zero)
        {
            return x - y * round(x / y);
        }

        IntervalSet<T> result;


        for (const auto& ix : x.intervals)
        {
            for (const auto& iy : y.intervals)
            {
                IntervalSet<T> res = IntervalSet<T>(ix) - IntervalSet<T>(iy) * round(
                    IntervalSet<T>(ix) / IntervalSet<T>(iy));


                T abs_y_max = max(abs(iy.lower), abs(iy.upper));
                T half_y = abs_y_max * static_cast<T>(0.5);


                result.intervals.emplace_back(res.intersect(Interval<T>(-half_y, half_y)));
            }
        }

        result.normalize();
        return result;
    }


    template <Arithmetic U, typename T>
    [[nodiscard]] IntervalSet<T> remainder(U x_val, const IntervalSet<T>& y)
    {
        if (y.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());


        return remainder(IntervalSet<T>(static_cast<T>(x_val)), y);
    }


    template <typename T, Arithmetic U>
    [[nodiscard]] IntervalSet<T> remainder(const IntervalSet<T>& x, U y_val)
    {
        if (x.is_poisoned()) return IntervalSet<T>(Interval<T>::poisoned());

        return remainder(x, IntervalSet<T>(static_cast<T>(y_val)));
    }

    namespace utils
    {
        template <typename T>
        bool is_unbounded(const IntervalSet<T>& set)
        {
            if (set.is_poisoned() || set.intervals.empty()) return false;
            for (const auto& iv : set.intervals)
            {
                if (isinf(iv.lower) && isinf(iv.upper)) return true;
            }
            return false;
        }

        template <typename T>
        bool possible_root(const IntervalSet<T>& set)
        {
            if (set.is_poisoned() || set.intervals.empty()) return false;
            for (const auto& iv : set.intervals)
            {
                if (iv.lower <= T(0) && iv.upper >= T(0))
                {
                    return true;
                }
            }
            return false;
        }
    }




    namespace utils
    {

        template <typename T>
        Interval<T> sqr_diff(const Interval<T>& I, T c) {

            T d0 = I.lower - c;
            T d1 = I.upper - c;

            // 如果 c 在区间 [lower, upper] 之间，极小值必为 0
            if (d0 * d1 <= 0) {
                return Interval<T>(
                    static_cast<T>(0),
                    max(d0 * d0, d1 * d1)
                );
            }
            // 否则，在区间两端点取极值
            return Interval<T>(
                min(d0 * d0, d1 * d1),
                max(d0 * d0, d1 * d1)
            );
        }

        /**
         * @brief 特化原子函数：计算圆形的隐函数区间
         * f(x, y) = (x-a)^2 + (y-b)^2 - r_sq
         */
        template <typename T>
        Interval<T> evaluate_circle_implicit(
            const Interval<T>& IX, const Interval<T>& IY,
            T a, T b, T r_sq)
        {
            // 利用加法的独立性，将两个原子平方位移区间合并
            Interval<T> res = sqr_diff(IX, a) + sqr_diff(IY, b);

            // 减去常量 r^2
            res.lower -= r_sq;
            res.upper -= r_sq;
            return res;
        }



        template <typename T>
        Interval<T> evaluate_sphere_implicit(
            const Interval<T>& IX, const Interval<T>& IY, const Interval<T>& IZ,
            T a, T b, T c, T r_sq)
        {
            // 1. 分别求出 X, Y, Z 方向的精确平方差区间并相加
            Interval<T> dist_sq = sqr_diff(IX, a) + sqr_diff(IY, b) + sqr_diff(IZ, c);

            // 2. 减去常量 R^2
            dist_sq.lower -= r_sq;
            dist_sq.upper -= r_sq;

            return dist_sq;
        }



        template <typename T>
        Interval<T> evaluate_cone_implicit(
            const Interval<T>& IX, const Interval<T>& IY, const Interval<T>& IZ,
            T ax, T ay, T az,
            T uax, T uay, T uaz,
            T factor, T h)
        {
            // 1. 计算 W = P - A 的三个独立分量区间
            Interval<T> WX = IX - ax;
            Interval<T> WY = IY - ay;
            Interval<T> WZ = IZ - az;

            // 2. 计算 w · d (投影高度区间)
            // 分离标量乘法，保证线性投影区间的 100% 精确度
            T d0 = WX.lower * uax, d1 = WX.upper * uax;
            Interval<T> PX = {std::min(d0, d1), std::max(d0, d1)};

            d0 = WY.lower * uay; d1 = WY.upper * uay;
            Interval<T> PY = {std::min(d0, d1), std::max(d0, d1)};

            d0 = WZ.lower * uaz; d1 = WZ.upper * uaz;
            Interval<T> PZ = {std::min(d0, d1), std::max(d0, d1)};

            Interval<T> PROJ = PX + PY + PZ;

            // 3. 极其强大的高度剪枝逻辑：
            // 如果整个包围盒的投影范围完全在 [0, H] 之外，直接返回 poisoned
            if (PROJ.upper < 0 || PROJ.lower > h) {
                return Interval<T>::poisoned();
            }

            // 4. 计算 ||w||^2 (精确距离区间)
            Interval<T> W_SQ = sqr_diff(IX, ax) + sqr_diff(IY, ay) + sqr_diff(IZ, az);

            // 5. 计算 PROJ^2
            T p0 = PROJ.lower * PROJ.lower;
            T p1 = PROJ.upper * PROJ.upper;
            Interval<T> P_SQ = (PROJ.lower * PROJ.upper <= 0) ?
                                Interval<T>(0, std::max(p0, p1)) :
                                Interval<T>(std::min(p0, p1), std::max(p0, p1));

            // 6. 合并计算 f = W_SQ - factor * P_SQ
            T p_high = P_SQ.upper * factor;
            T p_low = P_SQ.lower * factor;

            return Interval<T>{W_SQ.lower - p_high, W_SQ.upper - p_low};





        }




        template <typename T>
        Interval<T> evaluate_cylinder_implicit(
            const Interval<T>& IX, const Interval<T>& IY, const Interval<T>& IZ,
            T p1x, T p1y, T p1z,
            T uax, T uay, T uaz,
            T r_sq, T h)
        {
            // 1. 计算 w = P - P1
            Interval<T> WX = IX - p1x;
            Interval<T> WY = IY - p1y;
            Interval<T> WZ = IZ - p1z;

            // 2. 计算投影高度 proj = w · d (线性组合，100%精确)
            T d0 = WX.lower * uax, d1 = WX.upper * uax;
            Interval<T> PX = {std::min(d0, d1), std::max(d0, d1)};
            d0 = WY.lower * uay, d1 = WY.upper * uay;
            Interval<T> PY = {std::min(d0, d1), std::max(d0, d1)};
            d0 = WZ.lower * uaz, d1 = WZ.upper * uaz;
            Interval<T> PZ = {std::min(d0, d1), std::max(d0, d1)};

            Interval<T> PROJ = PX + PY + PZ;

            // 3. 高度剪枝：如果包围盒完全在圆柱两端盖之外，直接丢弃
            if (PROJ.upper < 0 || PROJ.lower > h) {
                return Interval<T>::poisoned();
            }

            // 4. 计算 ||w||^2 (使用 sqr_diff 保证原子性)
            Interval<T> W_SQ = sqr_diff(IX, p1x) + sqr_diff(IY, p1y) + sqr_diff(IZ, p1z);

            // 5. 计算 proj^2
            T p_low = PROJ.lower * PROJ.lower, p_high = PROJ.upper * PROJ.upper;
            Interval<T> PROJ_SQ = (PROJ.lower * PROJ.upper <= 0) ?
                                   Interval<T>(0, std::max(p_low, p_high)) :
                                   Interval<T>(std::min(p_low, p_high), std::max(p_low, p_high));

            // 6. f = W_SQ - PROJ_SQ - r_sq
            return W_SQ - PROJ_SQ - r_sq;
        }


        template <typename T>
        Interval<T> evaluate_circle_3d_sos_implicit(
            const Interval<T>& IX, const Interval<T>& IY, const Interval<T>& IZ,
            T cx, T cy, T cz, T nx, T ny, T nz, T r_sq)
        {
            // --- 1. 计算球面约束区间 S ---
            // S = (x-cx)^2 + (y-cy)^2 + (z-cz)^2 - r_sq
            // 使用 sqr_diff 保证 (I-c)^2 的评估是绝对原子且精确的
            Interval<T> dist_sq = sqr_diff(IX, cx) + sqr_diff(IY, cy) + sqr_diff(IZ, cz);
            Interval<T> S = {dist_sq.lower - r_sq, dist_sq.upper - r_sq};

            // --- 2. 计算平面约束区间 P ---
            // P = (x-cx)*nx + (y-cy)*ny + (z-cz)*nz
            Interval<T> WX = IX - cx;
            Interval<T> WY = IY - cy;
            Interval<T> WZ = IZ - cz;

            // 线性函数的区间运算在端点取得，是 100% 精确的
            auto dot_part = [](const Interval<T>& W, T n)
            {
                T v1 = W.lower * n;
                T v2 = W.upper * n;
                return Interval<T>(std::min(v1, v2), std::max(v1, v2));
            };
            Interval<T> P = dot_part(WX, nx) + dot_part(WY, ny) + dot_part(WZ, nz);

            // --- 3. 严格执行平方和合并 F = S^2 + P^2 ---
            // 定义严格的区间平方函数
            auto strict_interval_sqr = [](const Interval<T>& V)
            {
                // 如果区间跨越 0，则平方后的下界必然为 0
                if (V.lower <= 0 && V.upper >= 0)
                {
                    return Interval<T>(
                        static_cast<T>(0),
                        std::max(V.lower * V.lower, V.upper * V.upper)
                    );
                }
                // 否则，平方后的区间由两端点的平方值决定
                T v1 = V.lower * V.lower;
                T v2 = V.upper * V.upper;
                return Interval<T>(std::min(v1, v2), std::max(v1, v2));
            };

            Interval<T> S_sq = strict_interval_sqr(S);
            Interval<T> P_sq = strict_interval_sqr(P);

            // 返回 F = S_sq + P_sq
            // 注意：这里存在微小的过度估计，因为同一个点 P 不一定能同时使 S 和 P 取得各自的最小值。
            // 但这是区间算术处理这类问题的最严密上限。
            return S_sq + P_sq;
        }
    }
}



#if !defined(STUCANVAS_HEADER_ONLY) && !defined(STUCANVAS_COMPILING_LIBRARY)

// 1. 拦截 Interval 和 IntervalSet 的 double 实例
extern template struct StuCanvas::Interval<double>;
extern template struct StuCanvas::IntervalSet<double>;

namespace StuCanvas {
    // 2. 拦截核心运算符 (这是编译最慢的地方)
    extern template IntervalSet<double> operator+(const IntervalSet<double>&, const IntervalSet<double>&);
    extern template IntervalSet<double> operator-(const IntervalSet<double>&, const IntervalSet<double>&);
    extern template IntervalSet<double> operator*(const IntervalSet<double>&, const IntervalSet<double>&);
    extern template IntervalSet<double> operator/(const IntervalSet<double>&, const IntervalSet<double>&);

    // 3. 拦截高频数学函数
    extern template Interval<double> exp2(const Interval<double>&);
    extern template IntervalSet<double> sin(const IntervalSet<double>&);
    extern template IntervalSet<double> cos(const IntervalSet<double>&);




    namespace utils {
        extern template Interval<double> evaluate_circle_implicit(const Interval<double>&, const Interval<double>&, double, double, double);
        extern template Interval<double> evaluate_sphere_implicit(const Interval<double>&, const Interval<double>&, const Interval<double>&, double, double, double, double);
    }
}
#endif