// stucanvas/reconstruction/bezier_fitting.hpp
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "../types/point.hpp"
#include "../types/segment_strip.hpp"
#include "../types/path.hpp"
#include <Eigen/Dense>

namespace StuCanvas {
namespace reconstruction {

template <typename T>
T point_distance(const Point2D<T>& a, const Point2D<T>& b) {
    T dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * @brief 最简单的三次贝塞尔拟合（无压缩，Catmull‑Rom 插值）
 *
 * 对于开曲线：每两个相邻顶点之间生成一段贝塞尔，段数 = 顶点数 - 1。
 * 对于闭曲线：每两个相邻顶点之间生成一段贝塞尔，段数 = 顶点数。
 * 曲线会精确穿过所有输入顶点。
 *
 * @param strip  有序顶点集（可闭合）
 * @param alpha  张力参数：0=均匀，0.5=向心（推荐），1=弦长
 * @return Path2D<T>  每4个连续控制点组成一段三次贝塞尔曲线
 *
 * 本实现使用Eigen进行向量运算。
 */
template <typename T>
Path2D<T> fit_cubic_bezier_simple(const SegmentStrip2D<T>& strip, T alpha = T(0.5)) {
    Path2D<T> result;
    const auto& pts = strip.vertices;
    const size_t n = pts.size();
    if (n < 2) return result;

    const bool closed = strip.closed;

    // 索引辅助函数：处理闭合/开曲线的边界
    auto idx = [closed, n](int i) -> size_t {
        if (closed) {
            int m = static_cast<int>(n);
            return static_cast<size_t>((i % m + m) % m);
        } else {
            if (i < 0) return 0;
            if (i >= static_cast<int>(n)) return n - 1;
            return static_cast<size_t>(i);
        }
    };

    // 预计算所有相邻点之间的参数化距离
    std::vector<T> dists(n);
    if (closed) {
        for (size_t i = 0; i < n; ++i) {
            dists[i] = std::pow(point_distance(pts[i], pts[(i + 1) % n]), alpha);
        }
    } else {
        for (size_t i = 0; i < n - 1; ++i) {
            dists[i] = std::pow(point_distance(pts[i], pts[i + 1]), alpha);
        }
    }

    int seg_count = closed ? static_cast<int>(n) : static_cast<int>(n) - 1;

    for (int i = 0; i < seg_count; ++i) {
        const Point2D<T>& P0 = pts[idx(i - 1)];
        const Point2D<T>& P1 = pts[idx(i)];
        const Point2D<T>& P2 = pts[idx(i + 1)];
        const Point2D<T>& P3 = pts[idx(i + 2)];

        // 获取预计算的距离值
        T d01 = closed ? dists[(i - 1 + n) % n] : (i - 1 >= 0 ? dists[i - 1] : 0);
        T d12 = closed ? dists[i] : (i < static_cast<int>(n) - 1 ? dists[i] : 0);
        T d23 = closed ? dists[(i + 1) % n] : (i + 1 < static_cast<int>(n) - 1 ? dists[i + 1] : 0);

        T t0 = 0, t1 = d01, t2 = d01 + d12, t3 = d01 + d12 + d23;

        // 防止除零
        if (t2 - t0 < T(1e-9) || t3 - t1 < T(1e-9)) {
            result.control_points.push_back(P1);
            result.control_points.push_back(P1);
            result.control_points.push_back(P2);
            result.control_points.push_back(P2);
            continue;
        }

        // 使用Eigen进行向量运算
        using EigenVec = Eigen::Matrix<T, 2, 1>;
        EigenVec V0(P0.x, P0.y);
        EigenVec V1(P1.x, P1.y);
        EigenVec V2(P2.x, P2.y);
        EigenVec V3(P3.x, P3.y);

        T c1 = (t2 - t1) / (t2 - t0);
        T c2 = (t1 - t0) / (t2 - t0);
        T d1 = (t3 - t2) / (t3 - t1);
        T d2 = (t2 - t1) / (t3 - t1);

        T inv_t1_t0 = T(1) / (t1 - t0 + T(1e-9));
        T inv_t2_t1 = T(1) / (t2 - t1 + T(1e-9));
        T inv_t3_t2 = T(1) / (t3 - t2 + T(1e-9));

        // 切线计算
        EigenVec M1 = (t2 - t1) * (
            c1 * (V1 - V0) * inv_t1_t0 +
            c2 * (V2 - V1) * inv_t2_t1
        );
        EigenVec M2 = (t2 - t1) * (
            d1 * (V2 - V1) * inv_t2_t1 +
            d2 * (V3 - V2) * inv_t3_t2
        );

        // 贝塞尔控制点
        EigenVec B0 = V1;
        EigenVec B1 = B0 + M1 / T(3);
        EigenVec B3 = V2;
        EigenVec B2 = B3 - M2 / T(3);

        result.control_points.push_back({B0.x(), B0.y()});
        result.control_points.push_back({B1.x(), B1.y()});
        result.control_points.push_back({B2.x(), B2.y()});
        result.control_points.push_back({B3.x(), B3.y()});
    }

    return result;
}

} // namespace reconstruction
} // namespace StuCanvas