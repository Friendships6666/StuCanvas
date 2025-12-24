// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include <algorithm>

GeometryGraph::GeometryGraph() {
    buckets.resize(128);
    for(auto& b : buckets) b.reserve(32);
}

uint32_t GeometryGraph::allocate_node() {
    auto id = (uint32_t)node_pool.size();
    node_pool.emplace_back(id);
    return id;
}

void GeometryGraph::Enqueue(GeoNode& node) {
    if (node.last_update_frame == current_frame_index) return;
    node.last_update_frame = current_frame_index;

    if (node.rank >= buckets.size()) buckets.resize(node.rank + 32);
    buckets[node.rank].push_back(node.id);

    if ((int)node.rank < min_dirty_rank) min_dirty_rank = (int)node.rank;
    if ((int)node.rank > max_dirty_rank) max_dirty_rank = (int)node.rank;
}

void GeometryGraph::TouchNode(uint32_t id) {
    if (id >= node_pool.size()) return;
    Enqueue(node_pool[id]);
}

std::vector<uint32_t> GeometryGraph::SolveFrame() {
    current_frame_index++;
    std::vector<uint32_t> dirty_nodes;
    dirty_nodes.reserve(64);

    if (min_dirty_rank > max_dirty_rank) {
        min_dirty_rank = 10000; max_dirty_rank = 0;
        return dirty_nodes;
    }

    for (int r = min_dirty_rank; r <= max_dirty_rank; ++r) {
        auto& bucket = buckets[r];
        if (bucket.empty()) continue;

        for (uint32_t id : bucket) {
            GeoNode& node = node_pool[id];
            // 只有 Rank > 0 且绑定了 solver 的节点才需要执行计算
            if (node.rank > 0 && node.solver) {
                node.solver(node, node_pool);
            }
            dirty_nodes.push_back(id);
            for (uint32_t child_id : node.children) {
                Enqueue(node_pool[child_id]);
            }
        }
        bucket.clear();
    }
    min_dirty_rank = 10000; max_dirty_rank = 0;
    return dirty_nodes;
}