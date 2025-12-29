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
    std::cout << "SolveFrame: min_rank=" << min_dirty_rank << " max_rank=" << max_dirty_rank << std::endl;

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

bool GeometryGraph::DetectCycle(uint32_t child_id, uint32_t parent_id) const {
    // 如果 child 根本没有被创建或者刚创建(没有children)，那么它不可能有后代指向 parent
    // 除非我们是在修改一个已有的图结构。
    // 逻辑：从 child_id 出发，向下遍历所有 children。如果遇到了 parent_id，说明 parent 已经是 child 的后代了。
    // 如果此时再让 child 依赖 parent，就会形成环。

    if (child_id == parent_id) return true; // 自环

    std::vector<uint32_t> stack;
    std::vector<bool> visited(node_pool.size(), false);

    stack.push_back(child_id);
    visited[child_id] = true;

    while(!stack.empty()) {
        uint32_t curr = stack.back();
        stack.pop_back();

        // 检查当前节点的所有孩子
        for (uint32_t kid : node_pool[curr].children) {
            if (kid == parent_id) return true; // 找到了！parent 已经在 child 的下游了

            if (!visited[kid]) {
                visited[kid] = true;
                stack.push_back(kid);
            }
        }
    }
    return false;
}