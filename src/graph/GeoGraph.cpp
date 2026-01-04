// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include <algorithm>
std::string GeometryGraph::GenerateNextName() {
    // 1. 拷贝当前的索引值，并自增计数器为下一次调用做准备
    uint32_t current_idx = next_name_index++;

    // 2. 计算字母部分
    // 26 为英文字母表长度。使用取模运算确定当前处于 a-z 中的哪一个。
    char letter = static_cast<char>('a' + (current_idx % 26));

    // 3. 计算数字后缀部分
    // 使用整除运算确定当前是第几轮循环。
    // 0-25 轮次为 0 (不带后缀)，26-51 轮次为 1 (后缀为 1)，以此类推。
    uint32_t cycle = current_idx / 26;

    // 4. 构造最终字符串
    std::string name;
    name += letter; // 添加基础字母

    if (cycle > 0) {
        // 从第二轮开始，追加数字后缀
        name += std::to_string(cycle);
    }

    return name;
}
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

            if (node.solver && !node.is_heuristic && !node.is_buffer_dependent) {
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


void GeometryGraph::UpdateRankRecursive(uint32_t node_id) {
    auto& node = node_pool[node_id];
    uint32_t old_rank = node.rank;
    uint32_t max_p_rank = 0;

    // 1. 根据当前所有父节点计算理论 Rank
    for (uint32_t pid : node.parents) {
        max_p_rank = std::max(max_p_rank, node_pool[pid].rank);
    }
    uint32_t new_rank = node.parents.empty() ? 0 : max_p_rank + 1;

    // 2. 更新属性传播 (除了 Rank，还要传播 is_buffer_dependent)
    bool new_buffer_dep = node.is_heuristic;
    for (uint32_t pid : node.parents) {
        if (node_pool[pid].is_heuristic || node_pool[pid].is_buffer_dependent) {
            new_buffer_dep = true; break;
        }
    }
    node.is_buffer_dependent = new_buffer_dep;

    // 3. 只有当 Rank 真的变了，或者属性变了，才继续向下游传播
    // 这是一种“剪枝”优化，防止无效递归
    if (new_rank == old_rank) {
        // 如果 Rank 没变，但子节点可能需要更新 is_buffer_dependent 标记，
        // 这里可以根据需要决定是否继续。为了绝对安全，建议继续。
    }

    node.rank = new_rank;

    // 4. 递归触发所有孩子
    for (uint32_t cid : node.children) {
        UpdateRankRecursive(cid);
    }
}