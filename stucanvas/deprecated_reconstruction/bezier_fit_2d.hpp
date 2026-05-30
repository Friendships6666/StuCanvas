#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <algorithm>
#include "../types/point.hpp"
#include "../types/path.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class BezierFit2D {
public:
    /**
     * @brief 从无序2D点云拟合贝塞尔曲线
     * @param points  输入点云（可能带噪声和厚度）
     * @param epsilon RDP简化误差阈值（越大控制点越少）
     * @param voxel_size 体素大小用于除噪（0表示自动估算）
     * @return Path2D<T> 控制点序列（严格按每 4 个一组构成一段三次贝塞尔）
     */
    static Path2D<T> Fit(const std::vector<Point2D<T>>& points,
                         T epsilon,
                         T voxel_size = T(0))
    {
        if (points.size() < 2) return {};

        // 1. 体素下采样（消除厚度和噪声）
        auto sampled = voxel_downsample(points, voxel_size);
        if (sampled.size() < 2) return {};

        // 2. 使用MST提取点云主骨架（彻底解决有厚度点云导致的折返和锯齿问题）
        auto skeleton = extract_skeleton(sampled);
        if (skeleton.size() < 2) return {};

        // 3. 对骨架进行平滑处理，消除微小抖动
        auto smoothed = smooth_path(skeleton, 3);

        // 4. RDP算法对平滑后的路径进行简化，提取关键控制节点
        std::vector<Point2D<T>> simplified;
        rdp(smoothed, epsilon, simplified);
        if (simplified.size() < 2) return {};

        // 5. 从简化折线生成平滑的三次贝塞尔控制点
        return generate_bezier(simplified);
    }

private:
    // ---------- 体素下采样 ----------
    static std::vector<Point2D<T>> voxel_downsample(const std::vector<Point2D<T>>& pts, T voxel_size) {
        if (pts.empty()) return {};

        T min_x = pts[0].x, max_x = pts[0].x;
        T min_y = pts[0].y, max_y = pts[0].y;
        for (const auto& p : pts) {
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }

        if (voxel_size <= T(0)) {
            // 自动估算：提高分辨率到1/100，同时保证 MST 生成速度
            T span = std::max(max_x - min_x, max_y - min_y);
            voxel_size = span > 0 ? span / T(100) : T(1);
        }

        std::unordered_set<uint64_t> voxels;
        std::vector<Point2D<T>> centers;

        for (const auto& p : pts) {
            int64_t ix = static_cast<int64_t>(std::floor((p.x - min_x) / voxel_size));
            int64_t iy = static_cast<int64_t>(std::floor((p.y - min_y) / voxel_size));
            uint64_t key = (static_cast<uint64_t>(ix) << 32) | static_cast<uint32_t>(iy);

            if (voxels.insert(key).second) {
                T cx = min_x + (static_cast<T>(ix) + T(0.5)) * voxel_size;
                T cy = min_y + (static_cast<T>(iy) + T(0.5)) * voxel_size;
                centers.emplace_back(cx, cy);
            }
        }
        return centers;
    }

    static T dist_sq(const Point2D<T>& a, const Point2D<T>& b) {
        T dx = a.x - b.x, dy = a.y - b.y;
        return dx*dx + dy*dy;
    }

    // ---------- 基于最小生成树(MST)提取主干骨架 ----------
    static std::vector<Point2D<T>> extract_skeleton(const std::vector<Point2D<T>>& pts) {
        int n = static_cast<int>(pts.size());
        if (n < 2) return pts;

        // 1. 构建完全图的最小生成树 (Prim算法)
        std::vector<T> min_weight(n, std::numeric_limits<T>::max());
        std::vector<int> parent(n, -1);
        std::vector<bool> in_mst(n, false);
        std::vector<std::vector<int>> mst_adj(n);

        min_weight[0] = 0;
        for (int i = 0; i < n; ++i) {
            int u = -1;
            for (int j = 0; j < n; ++j) {
                if (!in_mst[j] && (u == -1 || min_weight[j] < min_weight[u])) {
                    u = j;
                }
            }
            if (u == -1) break;

            in_mst[u] = true;
            if (parent[u] != -1) {
                mst_adj[u].push_back(parent[u]);
                mst_adj[parent[u]].push_back(u);
            }

            for (int v = 0; v < n; ++v) {
                if (!in_mst[v]) {
                    T w = dist_sq(pts[u], pts[v]);
                    if (w < min_weight[v]) {
                        min_weight[v] = w;
                        parent[v] = u;
                    }
                }
            }
        }

        // 2. 通过两次 BFS 寻找 MST 的直径（最长的物理路径）
        auto bfs = [&](int start_node, std::vector<int>& out_parent) {
            std::vector<T> dist(n, T(0));
            std::vector<bool> visited(n, false);
            std::vector<int> q;
            q.reserve(n);
            int head = 0;

            q.push_back(start_node);
            visited[start_node] = true;
            out_parent.assign(n, -1);

            int furthest_node = start_node;
            T max_dist = T(0);

            while (head < static_cast<int>(q.size())) {
                int u = q[head++];
                for (int v : mst_adj[u]) {
                    if (!visited[v]) {
                        visited[v] = true;
                        T edge_len = std::sqrt(dist_sq(pts[u], pts[v]));
                        dist[v] = dist[u] + edge_len; // 沿用物理距离防误判
                        out_parent[v] = u;
                        q.push_back(v);
                        if (dist[v] > max_dist) {
                            max_dist = dist[v];
                            furthest_node = v;
                        }
                    }
                }
            }
            return furthest_node;
        };

        std::vector<int> p1;
        int extreme1 = bfs(0, p1);
        std::vector<int> p2;
        int extreme2 = bfs(extreme1, p2);

        // 3. 回溯提取正确的主干路径
        std::vector<Point2D<T>> skeleton;
        int curr = extreme2;
        while (curr != -1) {
            skeleton.push_back(pts[curr]);
            curr = p2[curr];
        }
        return skeleton;
    }

    // ---------- 路径平滑 ----------
    static std::vector<Point2D<T>> smooth_path(const std::vector<Point2D<T>>& pts, int iterations = 3) {
        if (pts.size() < 3) return pts;
        std::vector<Point2D<T>> result = pts;
        std::vector<Point2D<T>> temp = pts;

        for (int iter = 0; iter < iterations; ++iter) {
            for (size_t i = 1; i < result.size() - 1; ++i) {
                // 1-2-1 加权移动平均
                temp[i].x = (result[i-1].x + result[i].x * 2 + result[i+1].x) / T(4);
                temp[i].y = (result[i-1].y + result[i].y * 2 + result[i+1].y) / T(4);
            }
            result = temp;
        }
        return result;
    }

    // ---------- RDP简化 ----------
    static void rdp(const std::vector<Point2D<T>>& pts,
                    T epsilon,
                    std::vector<Point2D<T>>& out) {
        if (pts.size() < 2) { out = pts; return; }

        T max_dist = 0;
        size_t max_idx = 0;
        Point2D<T> a = pts[0], b = pts.back();

        T dx = b.x - a.x;
        T dy = b.y - a.y;
        T denom = std::sqrt(dx*dx + dy*dy);

        if (denom < T(1e-9)) {
            // [关键修复] 当画圆（起点终点重合）时，从最远的点劈开继续递归处理，防止形状丢失
            for (size_t i = 1; i < pts.size()-1; ++i) {
                T dist = std::sqrt(dist_sq(pts[i], a));
                if (dist > max_dist) { max_dist = dist; max_idx = i; }
            }
        } else {
            T A = b.y - a.y;
            T B = a.x - b.x;
            T C = b.x * a.y - a.x * b.y;

            for (size_t i = 1; i < pts.size()-1; ++i) {
                T dist = std::abs(A * pts[i].x + B * pts[i].y + C) / denom;
                if (dist > max_dist) { max_dist = dist; max_idx = i; }
            }
        }

        if (max_dist > epsilon) {
            std::vector<Point2D<T>> left(pts.begin(), pts.begin() + max_idx + 1);
            std::vector<Point2D<T>> right(pts.begin() + max_idx, pts.end());
            std::vector<Point2D<T>> left_out, right_out;

            rdp(left, epsilon, left_out);
            rdp(right, epsilon, right_out);

            out.insert(out.end(), left_out.begin(), left_out.end() - 1);
            out.insert(out.end(), right_out.begin(), right_out.end());
        } else {
            out = {a, b};
        }
    }

    // ---------- 生成符合 4N 格式规范的贝塞尔控制点 ----------
    static Path2D<T> generate_bezier(const std::vector<Point2D<T>>& pts) {
        Path2D<T> path;
        if (pts.size() < 2) return path;

        size_t n = pts.size();
        const T tension = T(0.3); // 平滑张力（曲线圆滑度）

        for (size_t i = 0; i < n - 1; ++i) {
            const Point2D<T>& p0 = pts[i];
            const Point2D<T>& p1 = pts[i+1];

            Point2D<T> t0, t1;
            if (i == 0) {
                t0.x = p1.x - p0.x;
                t0.y = p1.y - p0.y;
            } else {
                t0.x = (p1.x - pts[i-1].x) * T(0.5);
                t0.y = (p1.y - pts[i-1].y) * T(0.5);
            }

            if (i == n - 2) {
                t1.x = p1.x - p0.x;
                t1.y = p1.y - p0.y;
            } else {
                t1.x = (pts[i+2].x - p0.x) * T(0.5);
                t1.y = (pts[i+2].y - p0.y) * T(0.5);
            }

            Point2D<T> c1 { p0.x + t0.x * tension, p0.y + t0.y * tension };
            Point2D<T> c2 { p1.x - t1.x * tension, p1.y - t1.y * tension };

            // [关键修复] 严格落实 path.hpp 的 "每 4 个连续点构成一段三阶贝塞尔"
            path.control_points.push_back(p0);
            path.control_points.push_back(c1);
            path.control_points.push_back(c2);
            path.control_points.push_back(p1);
        }

        return path;
    }
};

} // namespace StuCanvas::Reconstruction