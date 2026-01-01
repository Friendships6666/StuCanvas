// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include <algorithm>

GeometryGraph::GeometryGraph() {
    buckets.resize(128);
    for(auto& b : buckets) b.reserve(32);
}

uint32_t GeometryGraph::allocate_node() {
    auto id = static_cast<uint32_t>(node_pool.size());
    node_pool.emplace_back(id);
    return id;
}

void GeometryGraph::Enqueue(GeoNode& node) {
    if (node.last_update_frame == current_frame_index) return;
    node.last_update_frame = current_frame_index;

    if (node.rank >= buckets.size()) buckets.resize(node.rank + 32);
    buckets[node.rank].emplace_back(node.id);

    if (node.rank < min_dirty_rank) min_dirty_rank = node.rank;
    if (node.rank > max_dirty_rank) max_dirty_rank = node.rank;
}

void GeometryGraph::TouchNode(uint32_t id) {
    if (id >= node_pool.size()) return;
    Enqueue(node_pool[id]);
}

std::vector<uint32_t> GeometryGraph::SolveFrame() {
    current_frame_index++; // 确保新的一帧

    std::unordered_set<uint32_t> render_nodes_set;

    for (auto r = min_dirty_rank; r <= max_dirty_rank; ++r) {
        auto& bucket = buckets[r];
        if (bucket.empty()) continue;

        // 遍历当前 Rank 的所有脏节点
        for (uint32_t id : bucket) {
            GeoNode& node = node_pool[id];

            // 只有当 node 的更新帧等于当前帧，才说明它是真的需要算
            // Enqueue 内部已经设置过 node.last_update_frame = current_frame_index

            if (node.solver) {
                node.solver(node, node_pool);
            }

            if (node.render_type != GeoNode::RenderType::None && node.render_type != GeoNode::RenderType::Scalar) {
                render_nodes_set.insert(id);
            }

            // 传播
            for (uint32_t child_id : node.children) {
                Enqueue(node_pool[child_id]);
            }
        }
        bucket.clear(); // 处理完必须清理
    }

    min_dirty_rank = 10000; max_dirty_rank = 0;
    return std::vector<uint32_t>(render_nodes_set.begin(), render_nodes_set.end());
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

std::vector<std::vector<uint32_t>> GeometryGraph::GetRequiredRankedBatches(const std::vector<uint32_t>& targets) {
    std::unordered_set<uint32_t> needed;
    std::vector<uint32_t> q = targets;
    for (uint32_t id : targets) needed.insert(id);

    // BFS 补全依赖
    size_t head = 0;
    while (head < q.size()) {
        uint32_t curr = q[head++];
        for (uint32_t pid : node_pool[curr].parents) {
            if (needed.insert(pid).second) q.push_back(pid);
        }
    }

    // 分桶
    uint32_t max_r = 0;
    for (uint32_t id : needed) max_r = std::max(max_r, node_pool[id].rank);

    std::vector<std::vector<uint32_t>> batches(max_r + 1);
    for (uint32_t id : needed) batches[node_pool[id].rank].push_back(id);

    return batches;
}