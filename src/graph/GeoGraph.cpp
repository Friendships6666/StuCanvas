// --- 文件路径: src/graph/GeoGraph.cpp ---
#include "../../include/graph/GeoGraph.h"
#include "../../include/graph/GeoSolver.h"
#include <algorithm>
#include <iostream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
    constexpr uint32_t NULL_ID = 0xFFFFFFFF;

    /**
     * @brief 硬件加速位扫描：从右往左找到第一个 1 的位置
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
}

// =========================================================
// 1. 初始化与命名系统
// =========================================================

GeometryGraph::GeometryGraph() {
    buckets_all_heads.resize(128, 0xFFFFFFFF);
    active_ranks_mask.resize(2, 0);
    m_dirty_mask.reserve(1024); // 预留一点空间
}

uint32_t GeometryGraph::allocate_node() {
    uint32_t id = static_cast<uint32_t>(node_pool.size());
    node_pool.emplace_back(id);
    return id;
}

std::string GeometryGraph::GenerateNextName() {
    uint32_t current_idx = next_name_index++;
    char letter = static_cast<char>('a' + (current_idx % 26));
    uint32_t cycle = current_idx / 26;
    // 逻辑：a, b, ... z, a1, b1, ... z1, a2...
    return (cycle == 0) ? std::string(1, letter) : (letter + std::to_string(cycle));
}

// =========================================================
// 2. 核心拓扑维护：双向链表桶 O(1) 搬家
// =========================================================

void GeometryGraph::UpdateBit(uint32_t rank, bool has_elements) {
    size_t word_idx = rank / 64;
    if (word_idx >= active_ranks_mask.size()) {
        active_ranks_mask.resize(word_idx + 1, 0);
    }

    if (has_elements) {
        active_ranks_mask[word_idx] |= (1ULL << (rank % 64));
    } else {
        active_ranks_mask[word_idx] &= ~(1ULL << (rank % 64));
    }
}

void GeometryGraph::MoveNodeInBuckets(uint32_t id, uint32_t new_rank) {
    auto& node = node_pool[id];
    uint32_t old_rank = node.rank;

    // --- 步骤 A：从旧桶脱离 ---
    if (node.is_in_bucket && old_rank < buckets_all_heads.size()) {
        if (node.prev_in_bucket != NULL_ID) {
            node_pool[node.prev_in_bucket].next_in_bucket = node.next_in_bucket;
        } else {
            // 我本来是头指针
            buckets_all_heads[old_rank] = node.next_in_bucket;
        }

        if (node.next_in_bucket != NULL_ID) {
            node_pool[node.next_in_bucket].prev_in_bucket = node.prev_in_bucket;
        }

        // 检查旧桶是否变空，熄灭位图灯
        if (buckets_all_heads[old_rank] == NULL_ID) {
            UpdateBit(old_rank, false);
        }
    }

    // --- 步骤 B：迁入新桶 (插入到链表头部) ---
    node.rank = new_rank;
    if (new_rank >= buckets_all_heads.size()) {
        buckets_all_heads.resize(new_rank + 32, NULL_ID);
    }

    uint32_t current_head = buckets_all_heads[new_rank];
    node.next_in_bucket = current_head;
    node.prev_in_bucket = NULL_ID;

    if (current_head != NULL_ID) {
        node_pool[current_head].prev_in_bucket = id;
    }
    buckets_all_heads[new_rank] = id;
    node.is_in_bucket = true;

    // 点亮位图灯
    UpdateBit(new_rank, true);
}

void GeometryGraph::UpdateRankRecursive(uint32_t node_id) {
    auto& node = node_pool[node_id];
    uint32_t old_rank = node.rank;

    // 1. 重新根据父母计算 Rank
    uint32_t max_p_rank = 0;
    for (uint32_t pid : node.parents) {
        max_p_rank = std::max(max_p_rank, node_pool[pid].rank);
    }
    uint32_t new_rank = node.parents.empty() ? 0 : max_p_rank + 1;

    // 2. 如果 Rank 没变且已经在桶里，终止递归（剪枝）
    if (new_rank == old_rank && node.is_in_bucket) return;

    // 3. 执行 O(1) 物理搬家
    MoveNodeInBuckets(node_id, new_rank);

    // 4. 递归向下传播
    // 拷贝一份 children 以防在递归过程中发生重新分配
    std::vector<uint32_t> children_copy = node.children;
    for (uint32_t cid : children_copy) {
        UpdateRankRecursive(cid);
    }
}

// =========================================================
// 3. 影响分析：FastScan (代替旧的 SolveFrame)
// =========================================================

std::vector<uint32_t> GeometryGraph::FastScan(const std::vector<uint32_t>& moved_ids) {
    // 1. 初始化临时位图（可以使用 vector<uint8_t> 以支持动态 ID）
    if (m_dirty_mask.size() < node_pool.size()) {
        m_dirty_mask.resize(node_pool.size(), 0);
    }
    std::fill(m_dirty_mask.begin(), m_dirty_mask.end(), 0);

    std::vector<uint32_t> targets;
    uint32_t min_rank_to_start = 0xFFFFFFFF;

    // 2. 标记“震源”并找到最低起始 Rank
    for (uint32_t id : moved_ids) {
        if (id >= node_pool.size()) continue;
        m_dirty_mask[id] = 1;
        targets.push_back(id);
        min_rank_to_start = std::min(min_rank_to_start, node_pool[id].rank);
    }

    if (targets.empty()) return {};

    // 3. 位图跳跃扫描
    for (size_t w = min_rank_to_start / 64; w < active_ranks_mask.size(); ++w) {
        uint64_t mask = active_ranks_mask[w];

        // 屏蔽掉低于 min_rank_to_start 的位 (仅在第一轮 word 扫描时需要)
        if (w == min_rank_to_start / 64) {
            mask &= (~0ULL << (min_rank_to_start % 64));
        }

        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit(mask);
            uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

            // 遍历该 Rank 下所有活跃节点
            uint32_t curr_id = buckets_all_heads[r];
            while (curr_id != NULL_ID) {
                auto& node = node_pool[curr_id];

                // 如果当前节点还没进 targets，检查其父母
                if (m_dirty_mask[curr_id] == 0) {
                    for (uint32_t pid : node.parents) {
                        if (m_dirty_mask[pid]) {
                            m_dirty_mask[curr_id] = 1;
                            targets.push_back(curr_id);
                            break;
                        }
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset); // 熄灭当前位，继续找本 Word 下一个
        }
    }

    return targets;
}

// =========================================================
// 4. 工具逻辑：环检测
// =========================================================

bool GeometryGraph::DetectCycle(uint32_t child_id, uint32_t parent_id) const {
    if (child_id == parent_id) return true;

    // 迭代版 DFS
    static thread_local std::vector<uint32_t> stack;
    static thread_local std::vector<bool> visited;

    stack.clear();
    if (visited.size() < node_pool.size()) visited.resize(node_pool.size(), false);
    std::fill(visited.begin(), visited.begin() + node_pool.size(), false);

    stack.push_back(child_id);
    visited[child_id] = true;

    while (!stack.empty()) {
        uint32_t curr = stack.back();
        stack.pop_back();

        for (uint32_t kid : node_pool[curr].children) {
            if (kid == parent_id) return true;
            if (!visited[kid]) {
                visited[kid] = true;
                stack.push_back(kid);
            }
        }
    }
    return false;
}

void GeometryGraph::DetachFromBucket(uint32_t id) {
    auto& node = node_pool[id];
    if (!node.is_in_bucket) return; // 已经不在桶里，直接跳过

    uint32_t r = node.rank;

    // 1. 处理前驱节点的指向
    if (node.prev_in_bucket != NULL_ID) {
        node_pool[node.prev_in_bucket].next_in_bucket = node.next_in_bucket;
    } else {
        // 如果我是头节点，更新头指针数组
        if (r < buckets_all_heads.size()) {
            buckets_all_heads[r] = node.next_in_bucket;
        }
    }

    // 2. 处理后继节点的指向
    if (node.next_in_bucket != NULL_ID) {
        node_pool[node.next_in_bucket].prev_in_bucket = node.prev_in_bucket;
    }

    // 3. 重置自身链表指针
    node.prev_in_bucket = NULL_ID;
    node.next_in_bucket = NULL_ID;
    node.is_in_bucket = false;

    // 4. 关键：如果这一层空了，熄灭位图灯
    if (r < buckets_all_heads.size() && buckets_all_heads[r] == NULL_ID) {
        UpdateBit(r, false);
    }
}



void GeometryGraph::LinkAndRank(uint32_t child_id, const std::vector<uint32_t>& new_parent_ids) {
    if (child_id >= node_pool.size()) return;

    // 1. 本地备份新父 ID，防止参数引用 node.parents 导致自修改冲突
    std::vector<uint32_t> safe_new_parents = new_parent_ids;

    // 2. 切断旧关系：从老父亲们的 children 列表中删除自己
    auto& node = node_pool[child_id];
    for (uint32_t old_pid : node.parents) {
        if (old_pid >= node_pool.size()) continue;
        auto& p_kids = node_pool[old_pid].children;
        p_kids.erase(std::remove(p_kids.begin(), p_kids.end(), child_id), p_kids.end());
    }

    // 3. 建立新关系
    node.parents = safe_new_parents;
    node.is_heuristic = is_heuristic_solver_local(node.solver);

    for (uint32_t pid : safe_new_parents) {
        if (pid >= node_pool.size()) continue;

        // --- 安全检查：防止回环 ---
        if (DetectCycle(child_id, pid)) {
            // 注意：在正式应用中，这里可能需要回滚操作或抛出异常
            throw std::runtime_error("Detected circular dependency in LinkAndRank!");
        }

        // 注册到父亲的子节点列表
        node_pool[pid].children.push_back(child_id);
    }

    // 4. 触发递归重排：计算新 Rank 并执行 MoveNodeInBuckets 搬家
    // 这一步会向下传递，确保整棵依赖树的 Rank 都是最新的
    UpdateRankRecursive(child_id);
}