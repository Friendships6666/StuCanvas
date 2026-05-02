#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../types/point.hpp"
#include "../types/mesh.hpp"
#include "../utils/flat_map.hpp"
#include "../reconstruction/qhull_wrapper.hpp"
#include "../reconstruction/boolean_engine.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class VoxelClusterDecomposer {
public:
    /**
     * @brief 基于正方体体素的空间聚类（带重叠）
     * @param points      输入点云
     * @param voxel_size  正方体边长
     * @param overlap     重叠距离（向外扩张，绝对值）
     * @return 聚类点云列表（每个元素为一个聚类的点集）
     */
    static std::vector<std::vector<Point3D<T>>> Decompose(
        const std::vector<Point3D<T>>& points,
        T voxel_size,
        T overlap)
    {
        if (points.empty() || voxel_size <= T(0)) return {};

        // 计算包围盒
        Point3D<T> bmin = points[0], bmax = points[0];
        for (const auto& p : points) {
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }

        // 体素网格映射：键 = 体素坐标 (ix, iy, iz) ，值 = 该体素内的点索引
        utils::FlatMap<uint64_t, std::vector<const Point3D<T>*>> voxelMap;
        for (const auto& p : points) {
            int64_t ix = static_cast<int64_t>(std::floor((p.x - bmin.x) / voxel_size));
            int64_t iy = static_cast<int64_t>(std::floor((p.y - bmin.y) / voxel_size));
            int64_t iz = static_cast<int64_t>(std::floor((p.z - bmin.z) / voxel_size));
            uint64_t key = pack(ix, iy, iz);
            voxelMap[key].push_back(&p);
        }

        // 收集所有非空体素坐标
        std::vector<std::tuple<int64_t, int64_t, int64_t>> occupiedCells;
        for (auto it = voxelMap.begin(); it != voxelMap.end(); ++it) {
            int64_t x, y, z;
            unpack(it->first, x, y, z);
            occupiedCells.emplace_back(x, y, z);
        }

        std::vector<std::vector<Point3D<T>>> clusters;
        clusters.reserve(occupiedCells.size());

        for (const auto& cell : occupiedCells) {
            int64_t cx = std::get<0>(cell);
            int64_t cy = std::get<1>(cell);
            int64_t cz = std::get<2>(cell);

            // 扩张后的包围盒（世界坐标）
            T minX = bmin.x + static_cast<T>(cx) * voxel_size - overlap;
            T minY = bmin.y + static_cast<T>(cy) * voxel_size - overlap;
            T minZ = bmin.z + static_cast<T>(cz) * voxel_size - overlap;
            T maxX = minX + voxel_size + overlap;
            T maxY = minY + voxel_size + overlap;
            T maxZ = minZ + voxel_size + overlap;

            std::vector<Point3D<T>> clusterPoints;
            // 收集该扩张盒内的所有点
            for (const auto& p : points) {
                if (p.x >= minX && p.x < maxX &&
                    p.y >= minY && p.y < maxY &&
                    p.z >= minZ && p.z < maxZ) {
                    clusterPoints.push_back(p);
                }
            }

            if (!clusterPoints.empty()) {
                clusters.push_back(std::move(clusterPoints));
            }
        }
        return clusters;
    }

    /**
     * @brief 一步完成聚类分解 → Qhull 凸包 → 批量布尔并集
     * @param points     输入点云
     * @param voxel_size 正方体边长
     * @param overlap    重叠距离
     * @return 合并后的封闭网格
     */
    static Mesh3D<T> Reconstruct(
        const std::vector<Point3D<T>>& points,
        T voxel_size,
        T overlap)
    {
        auto clusters = Decompose(points, voxel_size, overlap);
        std::vector<Mesh3D<T>> hulls;
        hulls.reserve(clusters.size());

        for (const auto& cluster : clusters) {
            if (cluster.size() < 4) continue;
            Mesh3D<T> hull = QhullWrapper<T>::Compute(cluster);
            if (!hull.vertices.empty()) {
                hulls.push_back(std::move(hull));
            }
        }

        if (hulls.empty()) return Mesh3D<T>();
        return BooleanEngine<T>::UnionMultiple(hulls);
    }

private:
    static uint64_t pack(int64_t x, int64_t y, int64_t z) {
        uint64_t ux = static_cast<uint64_t>(x + (1 << 19));
        uint64_t uy = static_cast<uint64_t>(y + (1 << 19));
        uint64_t uz = static_cast<uint64_t>(z + (1 << 19));
        return (ux << 42) | (uy << 21) | uz;
    }

    static void unpack(uint64_t key, int64_t& x, int64_t& y, int64_t& z) {
        uint64_t uz = key & 0x1FFFFF;
        uint64_t uy = (key >> 21) & 0x1FFFFF;
        uint64_t ux = (key >> 42) & 0x1FFFFF;
        x = static_cast<int64_t>(ux) - (1 << 19);
        y = static_cast<int64_t>(uy) - (1 << 19);
        z = static_cast<int64_t>(uz) - (1 << 19);
    }
};

} // namespace StuCanvas::Reconstruction