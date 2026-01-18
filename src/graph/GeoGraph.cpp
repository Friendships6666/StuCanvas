// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include "../../include/graph/GeoSolver.h"

#include <algorithm>
#include <iostream>
#include <charconv>
#include <stack>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
    /**
     * @brief 硬件加速位扫描：从右往左找到第一个 1 的位置 (LSB)
     */
    uint32_t find_first_set_bit(uint64_t mask) {
        if (mask == 0) return 64;
#ifdef _MSC_VER
        unsigned long index;
        _BitScanForward64(&index, mask);
        return static_cast<uint32_t>(index);
#else
        return static_cast<uint32_t>(__builtin_ctzll(mask));
#endif
    }

    /**
     * @brief 判断给定的求解器是否为图解型（Heuristic）
     */
    bool is_heuristic_solver_local(SolverFunc s) {
        // 在此处维护所有属于图解对象的 Solver 列表
        return (s == Solver_ConstrainedPoint);

    }
}

// =========================================================
// 1. 构造与生命周期管理
// =========================================================

GeometryGraph::GeometryGraph() : id_generator(1) {
    m_last_view.zoom = -1.0;
    // 构造函数逻辑，确保在头文件中没有重复定义
    buckets_all_heads.resize(128, NULL_ID);
    active_ranks_mask.resize(2, 0);
    m_dirty_mask.reserve(1024);
    id_to_index_table.resize(1024, -1);
}

uint32_t GeometryGraph::allocate_node() {
    uint32_t new_id = id_generator.fetch_add(1, std::memory_order_relaxed);
    if (new_id >= id_to_index_table.size()) {
        id_to_index_table.resize(new_id + 1024, -1);
    }
    uint32_t physical_index = static_cast<uint32_t>(node_pool.size());
    node_pool.emplace_back(new_id);
    id_to_index_table[new_id] = static_cast<int32_t>(physical_index);
    return new_id;
}

void GeometryGraph::physical_delete(uint32_t delete_id) {
    if (delete_id >= id_to_index_table.size()) return;
    int32_t target_idx = id_to_index_table[delete_id];
    if (target_idx == -1) return;

    UnregisterNodeName(node_pool[target_idx].config.name);

    id_to_index_table[delete_id] = -1;
    node_pool.erase(node_pool.begin() + target_idx);
    update_mapping_after_erase(static_cast<size_t>(target_idx));
}

void GeometryGraph::update_mapping_after_erase(size_t start_index) {
    for (size_t i = start_index; i < node_pool.size(); ++i) {
        uint32_t node_id = node_pool[i].id;
        id_to_index_table[node_id] = static_cast<int32_t>(i);
    }
}

// =========================================================
// 2. 名字管理系统
// =========================================================

void GeometryGraph::RegisterNodeName(const std::string& name, uint32_t id) {
    if (!name.empty()) name_to_id_map[name] = id;
}

void GeometryGraph::UnregisterNodeName(const std::string& name) {
    if (!name.empty()) name_to_id_map.erase(name);
}

uint32_t GeometryGraph::GetNodeID(const std::string& name) const {
    auto it = name_to_id_map.find(name);
    if (it != name_to_id_map.end()) return it->second;
    throw std::runtime_error("Linker Error: Unknown identifier '" + name + "'");
}

std::string GeometryGraph::GenerateNextName() {
    uint32_t current_idx = next_name_index++;
    char letter = static_cast<char>('a' + (current_idx % 26));
    uint32_t cycle = current_idx / 26;
    if (cycle == 0) return std::string(1, letter);
    char buf[12];
    buf[0] = letter;
    auto [ptr, ec] = std::to_chars(buf + 1, buf + 12, cycle);
    return std::string(buf, ptr - buf);
}

// =========================================================
// 3. 拓扑层级维护 (Rank & Bucket List)
// =========================================================

void GeometryGraph::UpdateBit(uint32_t rank, bool has_elements) {
    size_t word_idx = rank / 64;
    if (word_idx >= active_ranks_mask.size()) {
        active_ranks_mask.resize(word_idx + 1, 0);
    }
    if (has_elements) active_ranks_mask[word_idx] |= (1ULL << (rank % 64));
    else active_ranks_mask[word_idx] &= ~(1ULL << (rank % 64));
}

void GeometryGraph::MoveNodeInBuckets(uint32_t id, uint32_t new_rank) {
    auto& node = get_node_by_id(id);
    uint32_t old_rank = node.rank;

    if (node.is_in_bucket && old_rank < buckets_all_heads.size()) {
        if (node.prev_in_bucket != NULL_ID) {
            get_node_by_id(node.prev_in_bucket).next_in_bucket = node.next_in_bucket;
        } else {
            buckets_all_heads[old_rank] = node.next_in_bucket;
        }
        if (node.next_in_bucket != NULL_ID) {
            get_node_by_id(node.next_in_bucket).prev_in_bucket = node.prev_in_bucket;
        }
        if (buckets_all_heads[old_rank] == NULL_ID) UpdateBit(old_rank, false);
    }

    node.rank = new_rank;
    if (new_rank >= buckets_all_heads.size()) buckets_all_heads.resize(new_rank + 32, NULL_ID);

    uint32_t current_head = buckets_all_heads[new_rank];
    node.next_in_bucket = current_head;
    node.prev_in_bucket = NULL_ID;
    if (current_head != NULL_ID) get_node_by_id(current_head).prev_in_bucket = id;
    buckets_all_heads[new_rank] = id;
    node.is_in_bucket = true;
    UpdateBit(new_rank, true);
    if (new_rank > max_graph_rank) max_graph_rank = new_rank;
}

void GeometryGraph::UpdateRankRecursive(uint32_t start_node_id) {
    static thread_local std::vector<uint32_t> traversal_stack;
    traversal_stack.clear();
    traversal_stack.push_back(start_node_id);

    while (!traversal_stack.empty()) {
        uint32_t id = traversal_stack.back();
        traversal_stack.pop_back();

        auto& node = get_node_by_id(id);
        uint32_t old_rank = node.rank;

        uint32_t max_p_rank = 0;
        for (uint32_t pid : node.parents) {
            max_p_rank = std::max(max_p_rank, get_node_by_id(pid).rank);
        }
        uint32_t new_rank = node.parents.empty() ? 0 : max_p_rank + 1;

        if (new_rank == old_rank && node.is_in_bucket) continue;

        MoveNodeInBuckets(id, new_rank);
        for (uint32_t cid : node.children) traversal_stack.push_back(cid);
    }
}


/**
 * @brief 自动响应式扫描引擎 (修正 ID 寻址逻辑)
 */
std::vector<uint32_t> GeometryGraph::FastScan() {
    // 1. 种子消费
    if (m_pending_seeds.empty()) return {};
    std::vector<uint32_t> all_seeds = std::move(m_pending_seeds);
    m_pending_seeds.clear();

    // 2. 脏位图初始化
    uint32_t max_id = id_generator.load(std::memory_order_relaxed);
    if (m_dirty_mask.size() < max_id) {
        m_dirty_mask.resize(max_id + 128, 0);
    }
    std::ranges::fill(m_dirty_mask, 0);

    std::vector<uint32_t> targets;
    uint32_t min_rank_to_start = 0xFFFFFFFF;

    // 3. 初始震源处理
    for (uint32_t id : all_seeds) {
        if (!is_alive(id)) continue;

        m_dirty_mask[id] = 1;
        targets.push_back(id);

        uint32_t r = get_node_by_id(id).rank;
        if (r < min_rank_to_start) min_rank_to_start = r;

        // 解除失效粘滞
        get_node_by_id(id).result.set_f(ComputedResult::VALID, true);
    }

    // 4. 位图跳跃
    size_t start_word = min_rank_to_start / 64;
    for (size_t w = start_word; w < active_ranks_mask.size(); ++w) {
        uint64_t mask = active_ranks_mask[w];
        if (mask == 0) continue;

        if (w == start_word) {
            mask &= (~0ULL << (min_rank_to_start % 64));
        }

        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit(mask);
            uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

            // 💡 修正点：buckets_all_heads[r] 存储的是该层第一个节点的 ID
            uint32_t curr_id = buckets_all_heads[r];

            while (curr_id != NULL_ID) {
                // 💡 修正点：必须通过身份证查表，才能拿到漂移后的物理对象引用
                GeoNode& node = get_node_by_id(curr_id);

                if (m_dirty_mask[curr_id] == 0) {
                    for (uint32_t pid : node.parents) {
                        if (m_dirty_mask[pid]) {
                            m_dirty_mask[curr_id] = 1;
                            targets.push_back(curr_id);
                            node.result.set_f(ComputedResult::VALID, true);
                            break;
                        }
                    }
                }
                // 下一个也是 ID
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }
    std::ranges::sort(targets);

    return targets;
}
// =========================================================
// 5. 辅助与安全校验
// =========================================================

bool GeometryGraph::DetectCycle(uint32_t child_id, uint32_t parent_id) const {
    if (child_id == parent_id) return true;
    static thread_local std::vector<uint32_t> stack;
    stack.clear();
    // 使用 std::vector<bool> 局部标记，避免污染全局 m_dirty_mask
    std::vector<bool> local_visited(id_to_index_table.size(), false);

    stack.push_back(child_id);
    local_visited[child_id] = true;

    while (!stack.empty()) {
        uint32_t curr = stack.back();
        stack.pop_back();

        // 关键修复：在 const 函数中调用 const 版本的 get_node_by_id
        for (uint32_t kid : get_node_by_id(curr).children) {
            if (kid == parent_id) return true;
            if (is_alive(kid) && !local_visited[kid]) {
                local_visited[kid] = true;
                stack.push_back(kid);
            }
        }
    }
    return false;
}

void GeometryGraph::DetachFromBucket(uint32_t id) {
    auto& node = get_node_by_id(id);
    if (!node.is_in_bucket) return;
    uint32_t r = node.rank;
    if (node.prev_in_bucket != NULL_ID) {
        get_node_by_id(node.prev_in_bucket).next_in_bucket = node.next_in_bucket;
    } else {
        if (r < buckets_all_heads.size()) buckets_all_heads[r] = node.next_in_bucket;
    }
    if (node.next_in_bucket != NULL_ID) {
        get_node_by_id(node.next_in_bucket).prev_in_bucket = node.prev_in_bucket;
    }
    node.prev_in_bucket = node.next_in_bucket = NULL_ID;
    node.is_in_bucket = false;
    if (r < buckets_all_heads.size() && buckets_all_heads[r] == NULL_ID) UpdateBit(r, false);
}

void GeometryGraph::LinkAndRank(uint32_t child_id, const std::vector<uint32_t>& new_parent_ids) {
    if (!is_alive(child_id)) return;
    auto& node = get_node_by_id(child_id);

    // 断开旧连边
    for (uint32_t old_pid : node.parents) {
        if (is_alive(old_pid)) {
            auto& p_kids = get_node_by_id(old_pid).children;
            p_kids.erase(std::remove(p_kids.begin(), p_kids.end(), child_id), p_kids.end());
        }
    }

    node.parents = new_parent_ids;

    // ★ 关键重构：将图解状态统一到 ComputedResult 标志位中 ★
    node.result.set_f(ComputedResult::IS_HEURISTIC, is_heuristic_solver_local(node.solver));

    // 建立新连边
    for (uint32_t pid : node.parents) {
        if (is_alive(pid)) {
            if (DetectCycle(child_id, pid)) throw std::runtime_error("Circular dependency!");
            get_node_by_id(pid).children.push_back(child_id);
        }
    }
    UpdateRankRecursive(child_id);
}