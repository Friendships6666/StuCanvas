// stucanvas/reconstruction/reconstruct.hpp
#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include "../types/point.hpp"
#include "../types/segment_strip.hpp"

namespace StuCanvas {
namespace reconstruction {

namespace detail {

struct GridCell {
    int64_t x, y;
    bool operator==(const GridCell& o) const { return x == o.x && y == o.y; }
};

struct GridHasher {
    size_t operator()(const GridCell& gc) const {
        return std::hash<int64_t>()(gc.x) ^ (std::hash<int64_t>()(gc.y) << 32);
    }
};

} // namespace detail

template <typename T>
std::vector<SegmentStrip2D<T>> reconstruct_from_points(
    const std::vector<Point2D<T>>& points,
    T pixel_size)
{
    if (pixel_size <= T(0))
        throw std::invalid_argument("pixel_size must be positive");
    if (points.empty()) return {};

    std::cout << "[LOG] Starting reconstruction with " << points.size()
              << " input points, pixel_size = " << pixel_size << std::endl;

    // ---------- 1. 伪光栅化 ----------
    std::unordered_map<detail::GridCell, std::vector<Point2D<T>>, detail::GridHasher> grid_map;

    for (const auto& p : points) {
        detail::GridCell gc{ static_cast<int64_t>(std::floor(p.x / pixel_size)),
                             static_cast<int64_t>(std::floor(p.y / pixel_size)) };
        grid_map[gc].push_back(p);
    }

    std::cout << "[LOG] Rasterized into " << grid_map.size() << " non-empty cells.\n";

    struct Node {
        Point2D<T> pos;
        std::vector<int> neighbors;
    };
    std::vector<Node> nodes;
    std::unordered_map<detail::GridCell, int, detail::GridHasher> cell_to_idx;

    for (auto& [cell, pts] : grid_map) {
        Point2D<T> avg{ T(0), T(0) };
        for (auto& p : pts) avg.x += p.x, avg.y += p.y;
        T inv = T(1) / static_cast<T>(pts.size());
        avg.x *= inv; avg.y *= inv;
        nodes.push_back({ std::move(avg), {} });
        cell_to_idx[cell] = nodes.size() - 1;
        std::cout << "  Cell (" << cell.x << "," << cell.y << ") -> node "
                  << nodes.size() - 1 << " avg(" << avg.x << "," << avg.y
                  << ") from " << pts.size() << " points\n";
    }

    const int n_nodes = nodes.size();
    std::cout << "[LOG] Total nodes after merging: " << n_nodes << "\n";

    // ---------- 2. 双向最近邻连接 ----------
    const T search_radius = T(2) * pixel_size;
    const int cell_radius = 3; // ceil(search_radius / pixel_size) + 1

    std::cout << "[LOG] Starting nearest-neighbor connection with search_radius = "
              << search_radius << "\n";

    for (int i = 0; i < n_nodes; ++i) {
        const Point2D<T>& pi = nodes[i].pos;
        int cx = static_cast<int>(std::floor(pi.x / pixel_size));
        int cy = static_cast<int>(std::floor(pi.y / pixel_size));

        std::cout << "\n[LOG] Processing node " << i << " at (" << pi.x << "," << pi.y
                  << ") grid(" << cx << "," << cy << ")\n";

        struct Cand { T dist; int idx; };
        std::vector<Cand> candidates;

        // 搜索邻域网格
        for (int dx = -cell_radius; dx <= cell_radius; ++dx) {
            for (int dy = -cell_radius; dy <= cell_radius; ++dy) {
                detail::GridCell key{ cx + dx, cy + dy };
                auto it = cell_to_idx.find(key);
                if (it == cell_to_idx.end()) continue;
                int j = it->second;
                if (j == i) continue;
                T d2 = (nodes[j].pos.x - pi.x) * (nodes[j].pos.x - pi.x) +
                       (nodes[j].pos.y - pi.y) * (nodes[j].pos.y - pi.y);
                if (d2 <= search_radius * search_radius) {
                    candidates.push_back({ d2, j });
                }
            }
        }

        // 按距离排序
        std::sort(candidates.begin(), candidates.end(),
                  [](const Cand& a, const Cand& b) { return a.dist < b.dist; });

        std::cout << "    Candidates (distance, index): ";
        for (auto& c : candidates)
            std::cout << "(" << std::sqrt(c.dist) << ", " << c.idx << ") ";
        std::cout << "\n";

        // 尝试添加边
        int added = 0;
        for (size_t k = 0; k < candidates.size() && nodes[i].neighbors.size() < 2; ++k) {
            int j = candidates[k].idx;
            // 检查是否已经连接
            if (std::find(nodes[i].neighbors.begin(), nodes[i].neighbors.end(), j) !=
                nodes[i].neighbors.end()) {
                std::cout << "    -> Candidate " << j << " already connected, skip\n";
                continue;
            }
            // 检查目标节点容量
            if (nodes[j].neighbors.size() >= 2) {
                std::cout << "    -> Candidate " << j << " already full (degree "
                          << nodes[j].neighbors.size() << "), skip\n";
                continue;
            }
            // 建立双向边
            nodes[i].neighbors.push_back(j);
            nodes[j].neighbors.push_back(i);
            added++;
            std::cout << "    -> Connected i=" << i << " <-> j=" << j << "\n";
        }
        std::cout << "    Added " << added << " edges, current degree of node "
                  << i << " = " << nodes[i].neighbors.size() << "\n";
    }

    // 检查最终度数
    std::cout << "\n[LOG] Final vertex degrees:\n";
    for (int i = 0; i < n_nodes; ++i) {
        std::cout << "  Node " << i << ": degree " << nodes[i].neighbors.size()
                  << " neighbors: ";
        for (int nb : nodes[i].neighbors) std::cout << nb << " ";
        std::cout << "\n";
        if (nodes[i].neighbors.size() > 2) {
            std::ostringstream oss;
            oss << "Non-manifold vertex at index " << i << " (degree "
                << nodes[i].neighbors.size() << ")";
            std::cerr << "[ERROR] " << oss.str() << std::endl;
            throw std::runtime_error(oss.str());
        }
    }

    // ---------- 3. 提取连通分量 ----------
    std::vector<bool> visited(n_nodes, false);
    std::vector<SegmentStrip2D<T>> strips;

    for (int i = 0; i < n_nodes; ++i) {
        if (visited[i] || nodes[i].neighbors.empty()) continue;

        // BFS 收集分量
        std::vector<int> comp;
        std::queue<int> q;
        q.push(i);
        visited[i] = true;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            comp.push_back(u);
            for (int v : nodes[u].neighbors) {
                if (!visited[v]) {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }

        std::cout << "\n[LOG] Extracting component of size " << comp.size() << "\n";
        std::cout << "   Nodes: ";
        for (int u : comp) std::cout << u << " ";
        std::cout << "\n";

        // 寻找起点
        int start = -1;
        for (int u : comp) {
            if (nodes[u].neighbors.size() == 1) {
                start = u;
                break;
            }
        }
        if (start == -1) {
            std::cout << "   Pure cycle, picking arbitrary start " << comp[0] << "\n";
            start = comp[0];
        } else {
            std::cout << "   Open chain, start at endpoint " << start << "\n";
        }

        // 遍历生成路径
        std::vector<Point2D<T>> path;
        int curr = start, prev = -1;
        while (true) {
            path.push_back(nodes[curr].pos);
            int next = -1;
            for (int nxt : nodes[curr].neighbors) {
                if (nxt != prev) {
                    next = nxt;
                    break;
                }
            }
            if (next == -1) {
                std::cout << "   Dead end at node " << curr << ", path length "
                          << path.size() << "\n";
                break; // 开路终点
            }
            if (next == start) {
                // 闭合环
                std::cout << "   Closed loop detected (back to start). Path length "
                          << path.size() << "\n";
                strips.emplace_back(std::move(path), true);
                goto next_component;
            }
            prev = curr;
            curr = next;
        }
        if (path.size() >= 2) {
            std::cout << "   Adding open strip with " << path.size() << " vertices\n";
            strips.emplace_back(std::move(path), false);
        } else {
            std::cout << "   Path too short, skipped.\n";
        }
        next_component:;
    }

    std::cout << "\n[LOG] Reconstruction finished. " << strips.size()
              << " strip(s) generated.\n";
    return strips;
}

} // namespace reconstruction
} // namespace StuCanvas