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
#include <utility> // for std::move

#include "../types/point.hpp"
// 假设 Mesh3D 和 Vertex3D 定义在 mesh.hpp 中
#include "../types/mesh.hpp"
#include "quick_hull_3d.hpp"
#include "statistics.hpp"

namespace StuCanvas
{
    template <typename T>
    class ConvexDecomposition
    {
    public:
        // ==========================================
        // 配置参数
        // ==========================================
        struct Config {
            uint32_t max_depth = 4;        // 最大切分深度
            size_t min_points = 50;        // 最小点数阈值
        };

        Config config;

        /**
         * @brief 执行近似凸分解 (ACD) - 基于堆内存迭代，防止栈溢出
         */
        std::vector<Mesh3D<T>> Compute(const std::vector<Point3D<T>>& points)
        {
            std::vector<Mesh3D<T>> result_hulls;
            if (points.size() < 4) return result_hulls;

            // ==========================================
            // 定义处理任务结构
            // ==========================================
            struct Task {
                std::vector<Point3D<T>> pts;
                uint32_t depth;
            };

            // 使用 std::vector 作为显式栈（其底层内存在堆上动态分配）
            std::vector<Task> task_stack;

            // 初始化第一个任务（这里发生唯一一次全量拷贝）
            task_stack.push_back({points, 0});

            // ==========================================
            // 迭代处理任务栈
            // ==========================================
            while (!task_stack.empty())
            {
                // 弹出栈顶任务，利用 std::move 转移内部点云的所有权，避免数据拷贝
                Task current = std::move(task_stack.back());
                task_stack.pop_back();

                // 1. 终止条件判定：达到最大深度，或点数太少
                if (current.depth >= config.max_depth || current.pts.size() <= config.min_points)
                {
                    QuickHull3D<T> qhull;
                    Mesh3D<T> hull = qhull.Compute(current.pts);
                    if (!hull.vertices.empty()) {
                        result_hulls.push_back(std::move(hull));
                    }
                    continue; // 结束当前分支，继续处理栈中下一个任务
                }

                // 2. 主成分分析 (PCA) 获取切分平面
                PCAInfo pca = ComputePCA(current.pts);

                // 3. 空间划分
                std::vector<Point3D<T>> left_points;
                std::vector<Point3D<T>> right_points;
                left_points.reserve(current.pts.size() / 2);
                right_points.reserve(current.pts.size() / 2);

                for (const auto& p : current.pts)
                {
                    T dx = p.x - pca.centroid.x;
                    T dy = p.y - pca.centroid.y;
                    T dz = p.z - pca.centroid.z;

                    T proj = dx * pca.primary_axis.x + dy * pca.primary_axis.y + dz * pca.primary_axis.z;

                    if (proj > 0) {
                        right_points.push_back(p);
                    } else {
                        left_points.push_back(p);
                    }
                }

                // 极端情况防御：切分失败(共面或数据极度偏斜)，强制生成凸包
                if (left_points.size() < 4 || right_points.size() < 4)
                {
                    QuickHull3D<T> qhull;
                    Mesh3D<T> hull = qhull.Compute(current.pts);
                    if (!hull.vertices.empty()) {
                        result_hulls.push_back(std::move(hull));
                    }
                    continue;
                }

                // 4. 将切分后的子任务压入堆栈
                // 使用 std::move 将左右子集的内存控制权直接移交给栈上的新任务
                task_stack.push_back({std::move(left_points), current.depth + 1});
                task_stack.push_back({std::move(right_points), current.depth + 1});
            }

            return result_hulls;
        }

    private:
        struct PCAInfo {
            Point3D<T> centroid;
            Point3D<T> primary_axis;
        };

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

                cov[0][0] += dx * dx;
                cov[0][1] += dx * dy;
                cov[0][2] += dx * dz;
                cov[1][1] += dy * dy;
                cov[1][2] += dy * dz;
                cov[2][2] += dz * dz;
            }
            cov[1][0] = cov[0][1];
            cov[2][0] = cov[0][2];
            cov[2][1] = cov[1][2];

            Point3D<T> vec = {1.0, 1.0, 1.0};
            const int max_iterations = 15;

            for (int iter = 0; iter < max_iterations; ++iter)
            {
                T nx = cov[0][0] * vec.x + cov[0][1] * vec.y + cov[0][2] * vec.z;
                T ny = cov[1][0] * vec.x + cov[1][1] * vec.y + cov[1][2] * vec.z;
                T nz = cov[2][0] * vec.x + cov[2][1] * vec.y + cov[2][2] * vec.z;

                T length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length > static_cast<T>(1e-8)) {
                    vec.x = nx / length;
                    vec.y = ny / length;
                    vec.z = nz / length;
                }
            }

            info.primary_axis = vec;
            return info;
        }
    };
}