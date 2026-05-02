#pragma once
#include <functional>
#include <cstddef>
#include <type_traits>

namespace StuCanvas::cache {

    struct HashTool {
        /**
         * @brief 核心哈希合并算法 (来自 Boost)
         */
        template <typename T>
        static void combine(size_t& seed, const T& v) {
            std::hash<T> hasher;
            seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        /**
         * @brief 变长参数哈希聚合
         */
        template <typename... Args>
        static size_t compute(Args&&... args) {
            size_t seed = 0;
            (combine(seed, std::forward<Args>(args)), ...);
            return seed;
        }
    };

} // namespace StuCanvas::cache