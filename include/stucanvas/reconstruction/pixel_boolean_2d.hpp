#pragma once
#include <vector>
#include <cmath>
#include "../types/point.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

    template <typename T>
    class PixelBoolean2D {
    public:
        struct GridCoord2D {
            int64_t ix, iy;
            // 用于处理哈希碰撞的相等判定
            bool operator==(const GridCoord2D& o) const { return ix == o.ix && iy == o.iy; }
        };

    private:
        // 使用质数扰动实现理论无上限的空间哈希
        static inline uint64_t GetHash(int64_t ix, int64_t iy) {
            uint64_t h = static_cast<uint64_t>(ix) * 73856093;
            h ^= static_cast<uint64_t>(iy) * 19349663;
            return h; // FlatMap 内部会再进行一次二次哈希优化分布
        }

        using PixelMap = StuCanvas::utils::FlatMap<uint64_t, GridCoord2D>;

        static PixelMap CloudToMap(const std::vector<Point2D<T>>& cloud, T size) {
            PixelMap m(cloud.size());
            for (const auto& p : cloud) {
                int64_t ix = static_cast<int64_t>(std::floor(p.x / size));
                int64_t iy = static_cast<int64_t>(std::floor(p.y / size));
                m.insert(GetHash(ix, iy), {ix, iy});
            }
            return m;
        }

        static inline Point2D<T> ToPoint(const GridCoord2D& c, T size) {
            return { (static_cast<T>(c.ix) + static_cast<T>(0.5)) * size,
                     (static_cast<T>(c.iy) + static_cast<T>(0.5)) * size };
        }

    public:
        static std::vector<Point2D<T>> Union(const std::vector<Point2D<T>>& A,
                                            const std::vector<Point2D<T>>& B, T pixel_size) {
            PixelMap map = CloudToMap(A, pixel_size);
            for (const auto& p : B) {
                int64_t ix = static_cast<int64_t>(std::floor(p.x / pixel_size));
                int64_t iy = static_cast<int64_t>(std::floor(p.y / pixel_size));
                map.insert(GetHash(ix, iy), {ix, iy});
            }

            std::vector<Point2D<T>> result;
            result.reserve(map.size());
            for (auto it = map.begin(); it != map.end(); ++it) {
                result.push_back(ToPoint(it->second, pixel_size));
            }
            return result;
        }

        static std::vector<Point2D<T>> Intersection(const std::vector<Point2D<T>>& A,
                                                   const std::vector<Point2D<T>>& B, T pixel_size) {
            PixelMap mapA = CloudToMap(A, pixel_size);
            PixelMap mapB = CloudToMap(B, pixel_size);
            std::vector<Point2D<T>> result;

            for (auto it = mapA.begin(); it != mapA.end(); ++it) {
                auto target = mapB.find(it->first);
                if (target != mapB.end() && target->second == it->second) {
                    result.push_back(ToPoint(it->second, pixel_size));
                }
            }
            return result;
        }

        static std::vector<Point2D<T>> Difference(const std::vector<Point2D<T>>& A,
                                                 const std::vector<Point2D<T>>& B, T pixel_size) {
            PixelMap mapA = CloudToMap(A, pixel_size);
            PixelMap mapB = CloudToMap(B, pixel_size);
            std::vector<Point2D<T>> result;

            for (auto it = mapA.begin(); it != mapA.end(); ++it) {
                auto target = mapB.find(it->first);
                // 只有当 B 中完全不存在该坐标时才保留
                if (target == mapB.end() || !(target->second == it->second)) {
                    result.push_back(ToPoint(it->second, pixel_size));
                }
            }
            return result;
        }
    };
}