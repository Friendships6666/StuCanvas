#pragma once
#include <vector>
#include <cmath>
#include "../types/point.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

    template <typename T>
    class VoxelBoolean3D {
    public:
        struct GridCoord3D {
            int64_t ix, iy, iz;
            bool operator==(const GridCoord3D& o) const { 
                return ix == o.ix && iy == o.iy && iz == o.iz; 
            }
        };

    private:
        // 3D 空间哈希：利用三个大质数分散 int64 的坐标分布
        static inline uint64_t GetHash(int64_t ix, int64_t iy, int64_t iz) {
            uint64_t h = static_cast<uint64_t>(ix) * 73856093;
            h ^= static_cast<uint64_t>(iy) * 19349663;
            h ^= static_cast<uint64_t>(iz) * 83492791;
            return h;
        }

        using VoxelMap = StuCanvas::utils::FlatMap<uint64_t, GridCoord3D>;

        static VoxelMap CloudToMap(const std::vector<Point3D<T>>& cloud, T size) {
            VoxelMap m(cloud.size());
            for (const auto& p : cloud) {
                int64_t ix = static_cast<int64_t>(std::floor(p.x / size));
                int64_t iy = static_cast<int64_t>(std::floor(p.y / size));
                int64_t iz = static_cast<int64_t>(std::floor(p.z / size));
                m.insert(GetHash(ix, iy, iz), {ix, iy, iz});
            }
            return m;
        }

        static inline Point3D<T> ToPoint(const GridCoord3D& c, T size) {
            return { (static_cast<T>(c.ix) + static_cast<T>(0.5)) * size,
                     (static_cast<T>(c.iy) + static_cast<T>(0.5)) * size,
                     (static_cast<T>(c.iz) + static_cast<T>(0.5)) * size };
        }

    public:
        static std::vector<Point3D<T>> Union(const std::vector<Point3D<T>>& A, 
                                            const std::vector<Point3D<T>>& B, T voxel_size) {
            VoxelMap map = CloudToMap(A, voxel_size);
            for (const auto& p : B) {
                int64_t ix = static_cast<int64_t>(std::floor(p.x / voxel_size));
                int64_t iy = static_cast<int64_t>(std::floor(p.y / voxel_size));
                int64_t iz = static_cast<int64_t>(std::floor(p.z / voxel_size));
                map.insert(GetHash(ix, iy, iz), {ix, iy, iz});
            }

            std::vector<Point3D<T>> result;
            result.reserve(map.size());
            for (auto it = map.begin(); it != map.end(); ++it) {
                result.push_back(ToPoint(it->second, voxel_size));
            }
            return result;
        }

        static std::vector<Point3D<T>> Intersection(const std::vector<Point3D<T>>& A, 
                                                   const std::vector<Point3D<T>>& B, T voxel_size) {
            VoxelMap mapA = CloudToMap(A, voxel_size);
            VoxelMap mapB = CloudToMap(B, voxel_size);
            std::vector<Point3D<T>> result;

            for (auto it = mapA.begin(); it != mapA.end(); ++it) {
                auto target = mapB.find(it->first);
                if (target != mapB.end() && target->second == it->second) {
                    result.push_back(ToPoint(it->second, voxel_size));
                }
            }
            return result;
        }

        static std::vector<Point3D<T>> Difference(const std::vector<Point3D<T>>& A, 
                                                 const std::vector<Point3D<T>>& B, T voxel_size) {
            VoxelMap mapA = CloudToMap(A, voxel_size);
            VoxelMap mapB = CloudToMap(B, voxel_size);
            std::vector<Point3D<T>> result;

            for (auto it = mapA.begin(); it != mapA.end(); ++it) {
                auto target = mapB.find(it->first);
                if (target == mapB.end() || !(target->second == it->second)) {
                    result.push_back(ToPoint(it->second, voxel_size));
                }
            }
            return result;
        }
    };
}