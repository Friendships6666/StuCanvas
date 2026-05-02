/***************************************************************************
* Copyright (c) 2026 StuCanvas Team                                        *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
***************************************************************************/

#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
#include <Eigen/Dense>
#include "../types/point.hpp"
#include "../types/mesh.hpp"
#include "qhull_wrapper.hpp"
#include "statistics.hpp"

namespace StuCanvas::Reconstruction
{
    /**
     * @brief 暴力全量点云凸分割器 (Absolute Precision Version)
     *
     * 针对强大算力优化，取消所有抽样逻辑。
     * 每一层递归都会扫描所有点到凸包所有面片的距离，确保找到绝对最大凹陷位。
     */
    template <typename T = double>
    class ConvexPointSplitter
    {
    public:
        struct Config {
            T concavity_threshold = 0.01; // 默认 1% 的极低容差，追求极高凸度
            size_t min_points = 4;       // 只要能构成四面体就继续分
            int max_depth = 10;          // 深度开放到 10 层
        };

        /**
         * @brief 对点云执行递归凸切分
         * @return 分割后的独立点云簇集合
         */
        static std::vector<std::vector<Point3D<T>>> Split(const std::vector<Point3D<T>>& points, Config config = Config())
        {
            std::vector<std::vector<Point3D<T>>> clusters;
            if (points.size() < 4) {
                clusters.push_back(points);
                return clusters;
            }

            RecursiveSplit(points, 0, config, clusters);
            return clusters;
        }

    private:
        /**
         * @brief 递归深度优先分割逻辑
         */
        static void RecursiveSplit(const std::vector<Point3D<T>>& current_pts,
                                   int depth,
                                   const Config& config,
                                   std::vector<std::vector<Point3D<T>>>& results)
        {
            // 1. 强力终止判定
            if (current_pts.size() <= config.min_points || depth >= config.max_depth) {
                results.push_back(current_pts);
                return;
            }

            // 2. 调用 Qhull 生成当前点集的绝对凸包
            Mesh3D<T> hull = QhullWrapper<T>::Compute(current_pts);
            if (hull.vertices.empty()) {
                results.push_back(current_pts);
                return;
            }

            // 3. 全量评估：计算当前点集相对于凸包的“绝对最大凹陷深度”
            auto bbox = CalculateBoundingBox(current_pts);
            T dx = bbox.max.x - bbox.min.x;
            T dy = bbox.max.y - bbox.min.y;
            T dz = bbox.max.z - bbox.min.z;
            T scale = std::sqrt(dx*dx + dy*dy + dz*dz); // 使用 AABB 对角线作为基准尺度

            T max_concavity = CalculateAbsoluteMaxConcavity(current_pts, hull);

            // 4. 判定是否满足凸性要求
            if (max_concavity < (config.concavity_threshold * scale)) {
                results.push_back(current_pts);
                return;
            }

            // 5. 寻找最优切分面：通过 PCA 获取点云分布的主成分轴
            Eigen::Vector3<T> split_normal = ComputePCAMainAxis(current_pts);
            Point3D<T> centroid = CalculateCentroid(current_pts);

            // 6. 物理切分
            std::vector<Point3D<T>> left, right;
            left.reserve(current_pts.size());
            right.reserve(current_pts.size());

            for (const auto& p : current_pts) {
                // 计算点到质心平面（法线为 PCA 主轴）的距离
                T dist = (p.x - centroid.x) * split_normal.x() +
                         (p.y - centroid.y) * split_normal.y() +
                         (p.z - centroid.z) * split_normal.z();

                if (dist > 0) left.push_back(p);
                else right.push_back(p);
            }

            // 7. 防御空切分
            if (left.empty() || right.empty()) {
                results.push_back(current_pts);
                return;
            }

            // 8. 继续向深处切分
            RecursiveSplit(left, depth + 1, config, results);
            RecursiveSplit(right, depth + 1, config, results);
        }

        /**
         * @brief 暴力扫描：计算所有点到所有面片的最小距离中的最大值
         * 这是一个 O(N_points * N_faces) 的计算过程，保证不漏掉任何凹陷细节。
         */
        static T CalculateAbsoluteMaxConcavity(const std::vector<Point3D<T>>& points, const Mesh3D<T>& hull)
        {
            T global_max_concavity = 0;

            // 预计算所有面片的法线和平面方程常量，避免在内层循环重复计算
            struct Plane {
                Eigen::Vector3<T> normal;
                Eigen::Vector3<T> p0;
            };
            std::vector<Plane> planes;
            planes.reserve(hull.indices.size() / 3);

            for (size_t f = 0; f < hull.indices.size(); f += 3) {
                const auto& v0 = hull.vertices[hull.indices[f]].position;
                const auto& v1 = hull.vertices[hull.indices[f+1]].position;
                const auto& v2 = hull.vertices[hull.indices[f+2]].position;

                Eigen::Vector3<T> edge1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
                Eigen::Vector3<T> edge2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
                Eigen::Vector3<T> n = edge1.cross(edge2);
                T len = n.norm();
                if (len > 1e-12) {
                    planes.push_back({ n / len, Eigen::Vector3<T>(v0.x, v0.y, v0.z) });
                }
            }

            // 暴力双重遍历
            for (const auto& p : points) {
                T min_dist_to_hull_surface = std::numeric_limits<T>::max();
                Eigen::Vector3<T> pt(p.x, p.y, p.z);

                for (const auto& plane : planes) {
                    // 点到平面的投影距离
                    T d = std::abs(plane.normal.dot(pt - plane.p0));
                    if (d < min_dist_to_hull_surface) {
                        min_dist_to_hull_surface = d;
                    }
                }

                if (min_dist_to_hull_surface > global_max_concavity) {
                    global_max_concavity = min_dist_to_hull_surface;
                }
            }

            return global_max_concavity;
        }

        /**
         * @brief 严格 PCA 计算
         */
        static Eigen::Vector3<T> ComputePCAMainAxis(const std::vector<Point3D<T>>& points)
        {
            Point3D<T> center = CalculateCentroid(points);
            Eigen::Matrix3<T> covariance = Eigen::Matrix3<T>::Zero();

            for (const auto& p : points) {
                Eigen::Vector3<T> v(p.x - center.x, p.y - center.y, p.z - center.z);
                covariance += v * v.transpose();
            }

            // 协方差归一化
            covariance /= static_cast<T>(points.size());

            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3<T>> solver(covariance);
            // 返回最大特征值对应的特征向量
            return solver.eigenvectors().col(2).normalized();
        }
    };
}