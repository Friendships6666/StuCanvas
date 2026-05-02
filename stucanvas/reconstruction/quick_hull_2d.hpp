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
#include <algorithm>
#include <iostream>

#include "../types/point.hpp"
#include "../types/mesh.hpp"

namespace StuCanvas {

/**
 * @brief 高性能 3D 平面点云的 2D 快速凸包算法
 *
 * 专门设计用于处理 3D 空间中近似共面的点云。
 * 核心原理：利用 3D 叉乘与平面法向量的混合积 (Scalar Triple Product)，
 * 在不丢弃任何轴坐标、不显式矩阵降维的情况下，极其鲁棒地执行 2D QuickHull 逻辑。
 */
template <typename T>
class QuickHull2D {
public:
    using Index = uint32_t;
    using Point3 = Point3D<T>;
    using Mesh3  = Mesh3D<T>;

    static constexpr Index INVALID_INDEX = std::numeric_limits<Index>::max();

    Mesh3 Compute(const std::vector<Point3>& points) {
        if (points.size() < 3) {
            return Mesh3{}; // 至少需要 3 个点构成面
        }
        return compute_hull(points);
    }

private:
    T global_tol = 1e-5;
    const std::vector<Point3>* pts = nullptr;

    // --- 核心数学工具 ---
    static Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static T dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Point3 cross(const Point3& a, const Point3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static T dist_sq(const Point3& a, const Point3& b) {
        Point3 d = sub(a, b);
        return dot(d, d);
    }
    static T length_sq(const Point3& v) { return dot(v, v); }
    static T length(const Point3& v) { return std::sqrt(length_sq(v)); }

    /**
     * @brief 定向距离 (混合积)
     * 计算点 P 相对于平面内的有向线段 A->B 的符号距离。
     * @return > 0 表示在左侧， < 0 表示在右侧，绝对值正比于距离。
     */
    static T directed_dist(const Point3& a, const Point3& b, const Point3& p, const Point3& n) {
        Point3 ab = sub(b, a);
        Point3 ap = sub(p, a);
        Point3 cp = cross(ab, ap);
        return dot(cp, n); // (AB x AP) · N
    }

    // 字典序决胜，用于共线、极值点的稳定排序
    bool lexicographically_greater(const Point3& p, const Point3& ref) const {
        if (p.x > ref.x + global_tol) return true;
        if (std::abs(p.x - ref.x) <= global_tol) {
            if (p.y > ref.y + global_tol) return true;
            if (std::abs(p.y - ref.y) <= global_tol && p.z > ref.z + global_tol) return true;
        }
        return false;
    }

    // --- 算法主流程 ---
    Mesh3 compute_hull(const std::vector<Point3>& points) {
        pts = &points;
        size_t n = points.size();

        // 1. 动态容差计算
        T min_x = points[0].x, max_x = points[0].x;
        T min_y = points[0].y, max_y = points[0].y;
        T min_z = points[0].z, max_z = points[0].z;

        for (size_t i = 1; i < n; ++i) {
            const auto& p = points[i];
            if (p.x < min_x) min_x = p.x; if (p.x > max_x) max_x = p.x;
            if (p.y < min_y) min_y = p.y; if (p.y > max_y) max_y = p.y;
            if (p.z < min_z) min_z = p.z; if (p.z > max_z) max_z = p.z;
        }
        T max_span = std::max({max_x - min_x, max_y - min_y, max_z - min_z});
        global_tol = max_span * static_cast<T>(1e-5);
        if (global_tol < static_cast<T>(1e-9)) global_tol = static_cast<T>(1e-9);

        // 2. 寻找空间极值点 (X, Y, Z 的六个极值)
        auto tie_min = [&](T v, T rv, T s, T rs, T t, T rt) {
            if (v < rv - global_tol) return true;
            if (std::abs(v - rv) <= global_tol) {
                if (s < rs - global_tol) return true;
                if (std::abs(s - rs) <= global_tol && t < rt - global_tol) return true;
            } return false;
        };
        auto tie_max = [&](T v, T rv, T s, T rs, T t, T rt) {
            if (v > rv + global_tol) return true;
            if (std::abs(v - rv) <= global_tol) {
                if (s > rs + global_tol) return true;
                if (std::abs(s - rs) <= global_tol && t > rt + global_tol) return true;
            } return false;
        };

        Index ex[6] = {0,0,0,0,0,0};
        for (size_t i = 1; i < n; ++i) {
            const auto& p = points[i];
            if (tie_min(p.x, points[ex[0]].x, p.y, points[ex[0]].y, p.z, points[ex[0]].z)) ex[0] = i;
            if (tie_max(p.x, points[ex[1]].x, p.y, points[ex[1]].y, p.z, points[ex[1]].z)) ex[1] = i;
            if (tie_min(p.y, points[ex[2]].y, p.z, points[ex[2]].z, p.x, points[ex[2]].x)) ex[2] = i;
            if (tie_max(p.y, points[ex[3]].y, p.z, points[ex[3]].z, p.x, points[ex[3]].x)) ex[3] = i;
            if (tie_min(p.z, points[ex[4]].z, p.x, points[ex[4]].x, p.y, points[ex[4]].y)) ex[4] = i;
            if (tie_max(p.z, points[ex[5]].z, p.x, points[ex[5]].x, p.y, points[ex[5]].y)) ex[5] = i;
        }

        // 3. 提取距离最远的两个点作为基准线 A->B
        Index A = 0, B = 0;
        T max_dsq = -1;
        for (int i = 0; i < 6; ++i) {
            for (int j = i + 1; j < 6; ++j) {
                T d = dist_sq(points[ex[i]], points[ex[j]]);
                if (d > max_dsq) { max_dsq = d; A = ex[i]; B = ex[j]; }
            }
        }
        if (max_dsq < global_tol * global_tol) return Mesh3{}; // 所有点重合

        // 4. 寻找离线 A->B 最远的点 C_plane，用以确定平面的法向量 N
        Index C_plane = INVALID_INDEX;
        T max_cross_sq = -1;
        Point3 ab = sub(points[B], points[A]);
        for (size_t i = 0; i < n; ++i) {
            if (i == A || i == B) continue;
            Point3 ap = sub(points[i], points[A]);
            T cr_sq = length_sq(cross(ab, ap)); // ||AB x AP||^2 正比于距离的平方

            if (cr_sq > max_cross_sq + global_tol) {
                max_cross_sq = cr_sq;
                C_plane = i;
            } else if (std::abs(cr_sq - max_cross_sq) <= global_tol && C_plane != INVALID_INDEX) {
                if (lexicographically_greater(points[i], points[C_plane])) C_plane = i;
            }
        }

        if (max_cross_sq < global_tol * global_tol) return Mesh3{}; // 所有点共线

        // 计算平面法向量并归一化
        Point3 plane_normal = cross(ab, sub(points[C_plane], points[A]));
        T n_len = length(plane_normal);
        plane_normal.x /= n_len; plane_normal.y /= n_len; plane_normal.z /= n_len;

        // 5. 根据法向量将剩余点云沿基准线 A->B 划分为左侧和右侧
        std::vector<Index> S_left, S_right;
        S_left.reserve(n / 2);
        S_right.reserve(n / 2);

        for (size_t i = 0; i < n; ++i) {
            if (i == A || i == B) continue;
            T d = directed_dist(points[A], points[B], points[i], plane_normal);
            if (d > global_tol) {
                S_left.push_back(i);
            } else if (d < -global_tol) {
                S_right.push_back(i);
            }
        }

        // 6. 递归构建凸包边缘 (严密保证逆时针 CCW)
        std::vector<Index> hull_indices;
        hull_indices.reserve(n);

        hull_indices.push_back(A);
        build_hull(S_left, A, B, plane_normal, hull_indices);

        hull_indices.push_back(B);
        // 注意：线段 B->A 的左侧正是 A->B 的右侧，所以直接传 B, A 即可
        build_hull(S_right, B, A, plane_normal, hull_indices);

        return extract_mesh(hull_indices);
    }

    // --- 核心分治构建 ---
    void build_hull(const std::vector<Index>& S, Index P1, Index P2, const Point3& N, std::vector<Index>& hull) {
        if (S.empty()) return;

        // 1. 寻找离 P1->P2 轴线最远的点 C
        Index C = S[0];
        T max_dist = directed_dist((*pts)[P1], (*pts)[P2], (*pts)[C], N);

        for (size_t i = 1; i < S.size(); ++i) {
            Index idx = S[i];
            T d = directed_dist((*pts)[P1], (*pts)[P2], (*pts)[idx], N);

            if (d > max_dist + global_tol) {
                max_dist = d;
                C = idx;
            } else if (std::abs(d - max_dist) <= global_tol) {
                // 距离决胜：当存在平行的最远边时，取距离 P1 最远的点以消除歧义
                if (dist_sq((*pts)[P1], (*pts)[idx]) > dist_sq((*pts)[P1], (*pts)[C])) {
                    C = idx;
                }
            }
        }

        // 2. 依据新生成的三角形 P1-C-P2 过滤点集
        std::vector<Index> S1, S2;
        S1.reserve(S.size());
        S2.reserve(S.size());

        for (Index idx : S) {
            if (idx == C) continue;
            // 在 P1->C 外部的交给左半边处理
            if (directed_dist((*pts)[P1], (*pts)[C], (*pts)[idx], N) > global_tol) {
                S1.push_back(idx);
            }
            // 在 C->P2 外部的交给右半边处理
            else if (directed_dist((*pts)[C], (*pts)[P2], (*pts)[idx], N) > global_tol) {
                S2.push_back(idx);
            }
        }

        // 3. 递归压入凸包序列 (P1 -> C -> P2)
        build_hull(S1, P1, C, N, hull);
        hull.push_back(C);
        build_hull(S2, C, P2, N, hull);
    }

    // --- 网格转换 ---
    Mesh3 extract_mesh(const std::vector<Index>& hull_indices) {
        Mesh3 mesh;
        if (hull_indices.size() < 3) return mesh;

        // 装载物理顶点坐标
        for (Index idx : hull_indices) {
            mesh.vertices.push_back(Vertex3D<T>((*pts)[idx]));
        }

        // 三角扇形剖分 (Triangle Fan)：以顶点 0 为枢纽将凸多边形面缝合
        for (size_t i = 1; i + 1 < hull_indices.size(); ++i) {
            mesh.AddTriangle(0, i, i + 1);
        }

        return mesh;
    }
};

} // namespace StuCanvas