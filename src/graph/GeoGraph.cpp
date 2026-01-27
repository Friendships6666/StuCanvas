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

GeometryGraph::GeometryGraph() : view(), id_generator(1) {
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

// --- src/graph/GeoGraph.cpp ---

void GeometryGraph::physical_delete(uint32_t delete_id) {
    if (delete_id >= id_to_index_table.size()) return;
    int32_t target_idx = id_to_index_table[delete_id];
    if (target_idx == -1) return;

    // 1. 获取该节点在点缓冲区中的“遗产”信息
    GeoNode& node = node_pool[target_idx];
    uint32_t off = node.buffer_offset;
    uint32_t cnt = node.current_point_count;

    // 2. 💡 物理清理 final_points_buffer
    // 这会导致 [off + cnt, end] 范围内的点全部前移 cnt 个单位
    if (cnt > 0 && off < final_points_buffer.size()) {
        final_points_buffer.erase(
            final_points_buffer.begin() + off,
            final_points_buffer.begin() + off + cnt
        );

        // 3. 💡 修正所有受灾节点的偏移量
        // 逻辑：在池子里遍历，凡是排在被删节点“后面”的节点，偏移量全部减去被删点的数量
        for (auto& other : node_pool) {
            if (other.buffer_offset > off) {
                other.buffer_offset -= cnt;
            }
        }
    }

    // 4. 从拓扑结构中脱离 (DetachFromBucket)
    DetachFromBucket(delete_id);

    // 5. 注销名字映射
    UnregisterNodeName(node.config.name);

    // 6. 物理从映射表注销
    id_to_index_table[delete_id] = -1;

    // 7. 从池子中物理擦除节点
    node_pool.erase(node_pool.begin() + target_idx);

    // 8. 修正 node_pool 搬运后的 ID 映射 (O(N) 重整)
    update_mapping_after_erase(static_cast<size_t>(target_idx));
}
void GeometryGraph::update_mapping_after_erase(size_t start_index) {
    // 这一步是 O(N) 复杂度，虽然慢，但保证了 ID 到物理地址的绝对准确
    for (size_t i = start_index; i < node_pool.size(); ++i) {
        uint32_t node_id = node_pool[i].id; // 这里取的是节点自带的逻辑 ID
        id_to_index_table[node_id] = static_cast<int32_t>(i);
    }
}

// =========================================================
// 2. 名字管理系统
// =========================================================

void GeometryGraph::RegisterNodeName(const std::string& name, uint32_t id) {
    if (name.empty()) return;
    // 直接存储，区分 "PointA" 和 "pointa"
    name_to_id_map[name] = id;
}

void GeometryGraph::UnregisterNodeName(const std::string& name) {
    if (name.empty()) return;
    // 直接删除
    name_to_id_map.erase(name);
}

uint32_t GeometryGraph::GetNodeID(const std::string& name) const {
    auto it = name_to_id_map.find(name);
    if (it != name_to_id_map.end()) return it->second;

    // 💡 错误信息现在也可以包含原始名称，方便用户定位
    // 这里不再 throw，可以根据你之前的架构返回错误码
    return GeoErrorStatus::ERR_ID_NOT_FOUND;
}

std::string GeometryGraph::GenerateNextName() {
    while (true) {
        // 1. 记录当前索引并递增，准备下一次尝试
        uint32_t current_idx = next_name_index++;

        char letter = static_cast<char>('a' + (current_idx % 26));
        uint32_t cycle = current_idx / 26;

        std::string name;
        if (cycle == 0) {
            name = std::string(1, letter);
        } else {
            char buf[12];
            buf[0] = letter;
            auto [ptr, ec] = std::to_chars(buf + 1, buf + 12, cycle);
            name = std::string(buf, ptr - buf);
        }

        // 2. 💡 核心逻辑：区分大小写查重
        // 如果地图里不包含这个名字，说明可用，直接返回
        if (!name_to_id_map.contains(name)) {
            return name;
        }

        // 如果重名（比如用户手动创建了一个叫 "a" 的点），
        // 循环会继续，使用下一个 next_name_index 再次生成并校验
    }
}

std::string GeometryGraph::GenerateInternalName() {
    while (true) {
        // 1. 递增内部计数器
        uint32_t idx = ++next_internal_index;
        std::string name = "_internal_scalar_" + std::to_string(idx);

        // 2. 💡 查重校验
        if (!name_to_id_map.contains(name)) {
            return name;
        }
    }
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


std::vector<uint32_t> GeometryGraph::FastScan() {
    // 1. 种子消费：如果没有待处理的震源，直接返回
    if (m_pending_seeds.empty()) return {};

    // 2. 脏位图初始化与自动扩容
    uint32_t max_id = id_generator.load(std::memory_order_relaxed);
    if (m_dirty_mask.size() < max_id) {
        m_dirty_mask.resize(max_id + 128, 0);
    }

    std::vector<uint32_t> targets;
    uint32_t min_rank_to_start = 0xFFFFFFFF;

    // 3. 初始震源处理（种子节点）
    for (uint32_t id : m_pending_seeds) {
        if (!is_alive(id)) continue;
        if (m_dirty_mask[id]) continue; // 避免重复添加

        m_dirty_mask[id] = 1;
        targets.push_back(id);

        GeoNode& node = get_node_by_id(id);
        if (node.rank < min_rank_to_start) min_rank_to_start = node.rank;


            node.error_status = GeoErrorStatus::VALID;

    }
    m_pending_seeds.clear();

    // 4. 位图跳跃式拓扑扩散
    size_t start_word = (min_rank_to_start == 0xFFFFFFFF) ? 0 : min_rank_to_start / 64;

    for (size_t w = start_word; w < active_ranks_mask.size(); ++w) {
        uint64_t mask = active_ranks_mask[w];
        if (mask == 0) continue;

        // 对齐起始 Rank
        if (w == start_word && min_rank_to_start != 0xFFFFFFFF) {
            mask &= (~0ULL << (min_rank_to_start % 64));
        }

        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit(mask);
            uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

            // 遍历当前 Rank 的桶
            uint32_t curr_id = buckets_all_heads[r];
            while (curr_id != NULL_ID) {
                GeoNode& node = get_node_by_id(curr_id);

                // 如果当前节点还没变脏，检查它的父节点们
                if (m_dirty_mask[curr_id] == 0) {
                    for (uint32_t pid : node.parents) {
                        if (m_dirty_mask[pid]) {
                            // 只要有一个父亲脏了，我也变脏
                            m_dirty_mask[curr_id] = 1;
                            targets.push_back(curr_id);

                            // 💡 级联重置状态：给子节点重新计算的机会
                            if ((node.error_status & GeoErrorStatus::MASK_CAT) != GeoErrorStatus::CAT_LINK) {
                                node.error_status = GeoErrorStatus::VALID;
                            }
                            break;
                        }
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }

    // 5. 排序：为了在 calculate_points_core 中能用 binary_search 快速判定
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
            std::erase(p_kids, child_id);
        }
    }

    node.parents = new_parent_ids;


    // 建立新连边
    for (uint32_t pid : node.parents) {
        if (is_alive(pid)) {
            if (DetectCycle(child_id, pid)) throw std::runtime_error("Circular dependency!");
            get_node_by_id(pid).children.push_back(child_id);
        }
    }
    UpdateRankRecursive(child_id);
}



// --- src/graph/GeoGraph.cpp ---

void GeometryGraph::ClearEverything() {
    // 1. 💡 极致物理清理：释放 LogicChannel 内部的堆内存
    // 必须在 node_pool.clear() 之前执行，否则会导致 bytecode_ptr 等堆指针丢失造成泄漏
    for (auto& node : node_pool) {
        for (int i = 0; i < 4; ++i) {
            node.channels[i].clear();
        }
        // ComputedResult 是 POD 类型，reset_all 仅物理清零
        node.result.reset_all();
    }

    // 2. 清空采样点缓冲区并释放物理内存 (归还给 WASM/系统)
    final_points_buffer.clear();
    final_points_buffer.shrink_to_fit();

    // 适配最新的 GeoFunctionMeta 容器
    final_meta_buffer.clear();
    final_meta_buffer.shrink_to_fit();

    // 3. 重置核心容器与 ID 映射表
    node_pool.clear();
    // 确保映射表恢复到逻辑初始状态
    std::ranges::fill(id_to_index_table, -1);

    // 4. 计数器归位：确保 Git 重演和 ID 生成的一致性
    id_generator.store(1);
    next_name_index = 0;
    next_internal_index = 0;
    name_to_id_map.clear();

    // 5. 重置拓扑 Rank 系统
    std::ranges::fill(buckets_all_heads, NULL_ID);
    std::ranges::fill(active_ranks_mask, 0);
    max_graph_rank = 0;

    // 6. 清理脏数据追踪器
    m_pending_seeds.clear();
    std::ranges::fill(m_dirty_mask, 0);


    m_last_view.zoom = -1.0;

    // 8. 重置 Git/历史树状态
    history_tree.clear();
    head_version_id = -1;
    version_id_counter = 0;

    // 9. 恢复健康状态
    status = GraphStatus::READY;
}