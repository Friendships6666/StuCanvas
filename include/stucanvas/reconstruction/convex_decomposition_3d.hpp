/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <utility>
#include <algorithm>

#include "../types/point.hpp"
#include "../types/mesh.hpp"
#include "quick_hull_3d.hpp"
#include "statistics.hpp"

namespace StuCanvas
{
    template <typename T>
    class ConvexDecomposition
    {
    public:
        struct Config {
            uint32_t max_depth = 5;          // 最大递归深度
            size_t min_points = 20;          // 叶子节点最小点数
            T concavity_threshold = 0.1;     // 凹陷度阈值 (相对于包围盒尺寸)
            T plane_search_steps = 3;        // 沿主轴搜索最优切面的步数
        };

        Config config;

        /**
         * @brief 改良版近似凸分解 (ACD)
         */
        std::vector<Mesh3D<T>> Compute(const std::vector<Point3D<T>>& points)
        {
            std::vector<Mesh3D<T>> result_hulls;
            if (points.size() < 4) return result_hulls;

            struct Task {
                std::vector<Point3D<T>> pts;
                uint32_t depth;
            };

            std::vector<Task> task_stack;
            task_stack.push_back({points, 0});

            QuickHull3D<T> qhull_engine;

            while (!task_stack.empty())
            {
                Task current = std::move(task_stack.back());
                task_stack.pop_back();

                // 1. 基础终止条件
                if (current.pts.size() < 4) continue;

                // 2. 鲁棒性检查：计算当前凸包及凹陷度
                Mesh3D<T> current_hull = qhull_engine.Compute(current.pts);
                if (current_hull.vertices.empty()) continue;

                // 计算当前点集的尺度 (AABB 对角线长度)
                auto bbox = CalculateBoundingBox(current.pts);
                T dx = bbox.max.x - bbox.min.x;
                T dy = bbox.max.y - bbox.min.y;
                T dz = bbox.max.z - bbox.min.z;
                T scale = std::sqrt(dx*dx + dy*dy + dz*dz);

                // 计算凹陷度：找到距离凸包面最远的点
                T max_concavity = CalculateConcavity(current.pts, current_hull);

                // 3. 启发式终止条件：如果已经足够“凸”或者达到深度限制
                if (current.depth >= config.max_depth ||
                    max_concavity < (config.concavity_threshold * scale) ||
                    current.pts.size() <= config.min_points)
                {
                    result_hulls.push_back(std::move(current_hull));
                    continue;
                }

                // 4. 计算 PCA 获取主轴
                PCAInfo pca = ComputePCA(current.pts);

                // 5. 优化切分平面：在质心附近搜索能使两侧点数更平衡且凹陷度下降最快的平面
                T best_offset = 0;
                T best_score = std::numeric_limits<T>::max();

                // 搜索步进：尝试在主轴方向进行微调，而不只是切正中心
                for (int s = -1; s <= 1; ++s) {
                    T offset = s * (scale * 0.1); // 偏移 10% 范围搜索
                    T balance_score = EvaluateSplit(current.pts, pca, offset);
                    if (balance_score < best_score) {
                        best_score = balance_score;
                        best_offset = offset;
                    }
                }

                // 6. 执行切分
                std::vector<Point3D<T>> left, right;
                left.reserve(current.pts.size() / 2);
                right.reserve(current.pts.size() / 2);

                for (const auto& p : current.pts) {
                    T dist = (p.x - pca.centroid.x) * pca.primary_axis.x +
                             (p.y - pca.centroid.y) * pca.primary_axis.y +
                             (p.z - pca.centroid.z) * pca.primary_axis.z;
                    if (dist > best_offset) right.push_back(p);
                    else left.push_back(p);
                }

                // 7. 防御退化切分：如果一侧几乎为空，说明该方向无法继续分解
                if (left.size() < 4 || right.size() < 4) {
                    result_hulls.push_back(std::move(current_hull));
                    continue;
                }

                task_stack.push_back({std::move(left), current.depth + 1});
                task_stack.push_back({std::move(right), current.depth + 1});
            }

            return result_hulls;
        }

    private:
        struct PCAInfo {
            Point3D<T> centroid;
            Point3D<T> primary_axis;
        };

        /**
         * @brief 计算点云相对于凸包的最大凹陷深度
         */
        T CalculateConcavity(const std::vector<Point3D<T>>& points, const Mesh3D<T>& hull) {
            T max_d = 0;
            // 抽样检查点到凸包平面的距离 (为了性能，每 5 个点检查一个)
            for (size_t i = 0; i < points.size(); i += 5) {
                const auto& p = points[i];
                T min_dist_to_hull = std::numeric_limits<T>::max();

                // 遍历凸包面，寻找点到表面的最小距离（即该点被包围的深度）
                for (size_t f = 0; f < hull.indices.size(); f += 3) {
                    const auto& v0 = hull.vertices[hull.indices[f]].position;
                    const auto& v1 = hull.vertices[hull.indices[f+1]].position;
                    const auto& v2 = hull.vertices[hull.indices[f+2]].position;

                    // 计算面法线
                    Point3D<T> e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                    Point3D<T> e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                    Point3D<T> n = {
                        e1.y * e2.z - e1.z * e2.y,
                        e1.z * e2.x - e1.x * e2.z,
                        e1.x * e2.y - e1.y * e2.x
                    };
                    T len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                    if (len < 1e-9) continue;
                    n.x /= len; n.y /= len; n.z /= len;

                    // 点到面所在平面的距离
                    T d = std::abs((p.x - v0.x)*n.x + (p.y - v0.y)*n.y + (p.z - v0.z)*n.z);
                    min_dist_to_hull = std::min(min_dist_to_hull, d);
                }
                max_d = std::max(max_d, min_dist_to_hull);
            }
            return max_d;
        }

        /**
         * @brief 评估切分的评分（越小越好）
         * 考虑平衡度：防止出现极小碎片
         */
        T EvaluateSplit(const std::vector<Point3D<T>>& pts, const PCAInfo& pca, T offset) {
            size_t l = 0, r = 0;
            for (const auto& p : pts) {
                T d = (p.x - pca.centroid.x) * pca.primary_axis.x +
                      (p.y - pca.centroid.y) * pca.primary_axis.y +
                      (p.z - pca.centroid.z) * pca.primary_axis.z;
                if (d > offset) r++; else l++;
            }
            if (l == 0 || r == 0) return std::numeric_limits<T>::max();
            // 平衡度分数：两侧点数差异比重
            return std::abs(static_cast<T>(l) - static_cast<T>(r)) / pts.size();
        }

        PCAInfo ComputePCA(const std::vector<Point3D<T>>& points)
        {
            PCAInfo info;
            info.centroid = CalculateCentroid(points);

            T cov[3][3] = {{0}};
            for (const auto& p : points)
            {
                T dx = p.x - info.centroid.x;
                T dy = p.y - info.centroid.y;
                T dz = p.z - info.centroid.z;
                cov[0][0] += dx * dx; cov[0][1] += dx * dy; cov[0][2] += dx * dz;
                cov[1][1] += dy * dy; cov[1][2] += dy * dz;
                cov[2][2] += dz * dz;
            }
            cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];

            // 幂迭代法求主成分向量
            Point3D<T> vec = {1.0, 1.0, 1.0};
            for (int iter = 0; iter < 10; ++iter)
            {
                T nx = cov[0][0] * vec.x + cov[0][1] * vec.y + cov[0][2] * vec.z;
                T ny = cov[1][0] * vec.x + cov[1][1] * vec.y + cov[1][2] * vec.z;
                T nz = cov[2][0] * vec.x + cov[2][1] * vec.y + cov[2][2] * vec.z;
                T length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length > 1e-8) {
                    vec = {nx / length, ny / length, nz / length};
                }
            }
            info.primary_axis = vec;
            return info;
        }
    };
}