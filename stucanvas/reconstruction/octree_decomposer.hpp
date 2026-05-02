#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../types/point.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class OctreeDecomposer {
public:
    /**
     * @brief 将点云分解为若干重叠的子点云（每个块用于后续凸包重建）
     * @param points 输入点云
     * @param cellSize 八叉树叶节点目标边长
     * @param overlapFactor 膨胀因子，实际膨胀距离 = cellSize * overlapFactor
     * @return 子点云列表（每个子点云包含原块及邻域膨胀点，相邻块有重叠）
     */
    static std::vector<std::vector<Point3D<T>>> Decompose(
        const std::vector<Point3D<T>>& points,
        T cellSize,
        T overlapFactor = T(0.2))
    {
        if (points.empty() || cellSize <= T(0)) return {};
        // 计算包围盒
        Point3D<T> bmin = points[0], bmax = points[0];
        for (const auto& p : points) {
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }
        // 扩充包围盒，保证所有点都在叶节点内
        bmin.x -= cellSize; bmin.y -= cellSize; bmin.z -= cellSize;
        bmax.x += cellSize; bmax.y += cellSize; bmax.z += cellSize;

        // 递归构建叶子节点包围盒
        std::vector<AABB> leaves;
        BuildLeaves(bmin, bmax, points, cellSize, 0, 6, leaves);

        // 为每个叶节点生成膨胀后的点集
        std::vector<std::vector<Point3D<T>>> result;
        result.reserve(leaves.size());
        const T expandDist = cellSize * overlapFactor; // 膨胀距离
        for (const auto& leaf : leaves) {
            AABB expanded = leaf;
            expanded.min.x -= expandDist; expanded.min.y -= expandDist; expanded.min.z -= expandDist;
            expanded.max.x += expandDist; expanded.max.y += expandDist; expanded.max.z += expandDist;
            std::vector<Point3D<T>> blockPoints;
            blockPoints.reserve(points.size() / leaves.size()); // 粗略预估
            for (const auto& p : points) {
                if (Contains(expanded, p)) {
                    blockPoints.push_back(p);
                }
            }
            result.push_back(std::move(blockPoints));
        }
        return result;
    }

    /**
     * @brief 将点云分解后合并所有重叠子点云（允许重复，点云数量略有增加）
     * @param points 输入点云
     * @param cellSize 叶节点目标边长
     * @param overlapFactor 膨胀因子
     * @return 合并后的点云（含重复点，用于观察重叠）
     */
    static std::vector<Point3D<T>> DecomposeToCloud(
        const std::vector<Point3D<T>>& points,
        T cellSize,
        T overlapFactor = T(0.2))
    {
        auto blocks = Decompose(points, cellSize, overlapFactor);
        std::vector<Point3D<T>> merged;
        merged.reserve(points.size() * 2); // 粗略预留
        for (const auto& blk : blocks)
            merged.insert(merged.end(), blk.begin(), blk.end());
        return merged;
    }

private:
    struct AABB {
        Point3D<T> min, max;
    };

    static bool Contains(const AABB& box, const Point3D<T>& p) {
        return p.x >= box.min.x && p.x < box.max.x &&
               p.y >= box.min.y && p.y < box.max.y &&
               p.z >= box.min.z && p.z < box.max.z;
    }

    static void BuildLeaves(const Point3D<T>& bmin, const Point3D<T>& bmax,
                            const std::vector<Point3D<T>>& points,
                            T cellSize, int depth, int maxDepth,
                            std::vector<AABB>& leaves) {
        // 点数过少或深度达到上限则停止划分
        size_t count = 0;
        for (const auto& p : points) { if (Contains({bmin, bmax}, p)) count++; }
        if (count == 0) return;
        if (depth >= maxDepth || (bmax.x - bmin.x) <= cellSize ||
            (bmax.y - bmin.y) <= cellSize || (bmax.z - bmin.z) <= cellSize || count <= 64) {
            leaves.push_back({bmin, bmax});
            return;
        }
        // 八等分
        T mx = (bmin.x + bmax.x) * T(0.5);
        T my = (bmin.y + bmax.y) * T(0.5);
        T mz = (bmin.z + bmax.z) * T(0.5);
        int nd = depth + 1;
        BuildLeaves({bmin.x, bmin.y, bmin.z}, {mx, my, mz}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({mx, bmin.y, bmin.z}, {bmax.x, my, mz}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({bmin.x, my, bmin.z}, {mx, bmax.y, mz}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({mx, my, bmin.z}, {bmax.x, bmax.y, mz}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({bmin.x, bmin.y, mz}, {mx, my, bmax.z}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({mx, bmin.y, mz}, {bmax.x, my, bmax.z}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({bmin.x, my, mz}, {mx, bmax.y, bmax.z}, points, cellSize, nd, maxDepth, leaves);
        BuildLeaves({mx, my, mz}, {bmax.x, bmax.y, bmax.z}, points, cellSize, nd, maxDepth, leaves);
    }
};

} // namespace StuCanvas::Reconstruction