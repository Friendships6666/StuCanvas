// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../include/graph/GeoSolver.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <vector>
#include <algorithm>
#include <cstring>

namespace {
    inline uint32_t find_first_set_bit_local(uint64_t mask) {
        if (mask == 0) return 64;
#ifdef _MSC_VER
        unsigned long index;
        _BitScanForward64(&index, mask);
        return static_cast<uint32_t>(index);
#else
        return static_cast<uint32_t>(__builtin_ctzll(mask));
#endif
    }

    void flush_node_results(
        GeoNode& node,
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue,
        std::vector<PointData>& out_points
    ) {
        uint32_t start_offset = static_cast<uint32_t>(out_points.size());
        FunctionResult res;
        while (queue.try_pop(res)) {
            if (res.points.empty()) continue;
            out_points.insert(out_points.end(), res.points.begin(), res.points.end());
        }
        node.buffer_offset = start_offset;
        node.current_point_count = static_cast<uint32_t>(out_points.size()) - start_offset;
    }

    bool check_children_need_plot(const GeoNode& node, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut) {
        for (uint32_t cid : node.children) {
            const auto& child = pool[lut[cid]];
            if (child.active && (child.result.check_f(ComputedResult::IS_HEURISTIC) || child.config.is_visible)) {
                return true;
            }
        }
        return false;
    }
}

// =========================================================
// 核心渲染调度入口 (含脏位自动清理机制)
// =========================================================
void calculate_points_core(
    std::vector<PointData>& out_points,
    std::vector<FunctionRange>& out_ranges,
    GeometryGraph& graph
) {
    // ---------------------------------------------------------
    // 1. 模式检测：Ping-Pong 视图检测
    // ---------------------------------------------------------
    bool viewport_changed = std::memcmp(&graph.view, &graph.m_last_view, sizeof(ViewState)) != 0;

    if (viewport_changed) [[unlikely]] {
        out_points.clear();
        out_points.reserve(graph.node_pool.size() * 128);
    }

    const ViewState& view = graph.view;
    NDCMap ndc_map = BuildNDCMap(view);
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> node_queue;
    node_queue.set_capacity(4096);

    // ---------------------------------------------------------
    // 2. 拓扑扩散：获取受灾名单
    // ---------------------------------------------------------
    std::vector<uint32_t> affected_ids = graph.FastScan();

    // ---------------------------------------------------------
    // 3. 核心管线：按 Rank 序循环
    // ---------------------------------------------------------
    for (size_t w = 0; w < graph.active_ranks_mask.size(); ++w) {
        uint64_t mask = graph.active_ranks_mask[w];
        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit_local(mask);
            uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

            uint32_t curr_id = graph.buckets_all_heads[r];
            while (curr_id != NULL_ID) {
                GeoNode& node = graph.get_node_by_id(curr_id);

                if (node.active) {
                    // 判定：逻辑是否脏了
                    bool is_logic_dirty = std::binary_search(affected_ids.begin(), affected_ids.end(), node.id);
                    bool is_heuristic = node.result.check_f(ComputedResult::IS_HEURISTIC);

                    // --- 阶段 A: Solver (逻辑解算) ---
                    if (is_logic_dirty || (viewport_changed && is_heuristic)) {
                        // 级联预检
                        bool parents_ok = true;
                        for (uint32_t pid : node.parents) {
                            if (!graph.get_node_by_id(pid).result.check_f(ComputedResult::VALID)) {
                                parents_ok = false;
                                break;
                            }
                        }

                        if (parents_ok) {
                            node.solver(node, graph.node_pool, graph.id_to_index_table, view);
                        } else {
                            node.result.set_f(ComputedResult::VALID, false);
                        }
                    }

                    // --- 阶段 B: Plot (采样与重投影) ---
                    if (node.result.check_f(ComputedResult::VALID)) {
                        bool child_needs = check_children_need_plot(node, graph.node_pool, graph.id_to_index_table);

                        if (node.config.is_visible || child_needs) {
                            if (viewport_changed || is_logic_dirty) {
                                if (node.render_task) {
                                    node.render_task(node, graph.node_pool, graph.id_to_index_table, view, ndc_map, node_queue);
                                    flush_node_results(node, node_queue, out_points);
                                }
                            }
                        } else if (is_logic_dirty) {
                            node.current_point_count = 0;
                        }
                    }

                    // --- 阶段 C: 【核心新增】脏位清理 ---
                    // 逻辑已经算完，Plot 已经刷完，该节点的本帧使命已完成。
                    // 清除脏标记，防止下一帧重复计算。
                    if (is_logic_dirty) {
                        node.result.set_f(ComputedResult::DIRTY, false);
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }

    // ---------------------------------------------------------
    // 4. 指令生成
    // ---------------------------------------------------------
    out_ranges.clear();
    for (const auto& node : graph.node_pool) {
        if (node.active && node.result.check_f(ComputedResult::VALID) && node.config.is_visible) {
            out_ranges.push_back({ node.buffer_offset, node.current_point_count });
        }
    }

    // ---------------------------------------------------------
    // 5. 状态同步：完成 Ping-Pong 并清除可能存在的全局 Seeds 遗留
    // ---------------------------------------------------------
    std::memcpy(&graph.m_last_view, &graph.view, sizeof(ViewState));

    // 如果 Factory 有任何漏掉的标记，在这里做最后的兜底清理
    // (虽然 FastScan 已经清过了，但这增加了系统的鲁棒性)


    graph.m_pending_seeds.clear();
    std::ranges::fill(graph.m_dirty_mask, 0);
}