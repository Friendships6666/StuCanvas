// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../include/graph/GeoSolver.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <vector>
#include <algorithm>
#include <cstring> // for std::memcmp

namespace {
    /**
     * @brief 硬件级位扫描：定位下一个非空 Rank
     */
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

    /**
     * @brief 物理合并：将并行采样的分块结果顺序刷入主连续 Buffer
     */
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

    /**
     * @brief 检查直接子节点是否需要父节点的采样 Buffer
     * 逻辑：如果孩子是图解点（需要吸附）或孩子可见（需要基于父数据绘制），则父节点必须 Plot
     */
    bool check_children_need_plot(const GeoNode& node, const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut) {
        for (uint32_t cid : node.children) {
            // 通过直映表获取子节点
            const auto& child = pool[lut[cid]];
            if (child.active && (child.result.check_f(ComputedResult::IS_HEURISTIC) || child.config.is_visible)) {
                return true;
            }
        }
        return false;
    }
}

// =========================================================
// 核心渲染调度入口 (C++23 响应式全自动版)
// =========================================================
void calculate_points_core(
    std::vector<PointData>& out_points,
    std::vector<FunctionRange>& out_ranges,
    GeometryGraph& graph
) {
    // ---------------------------------------------------------
    // 1. 模式检测：硬件级 Ping-Pong ViewState 比对
    // ---------------------------------------------------------
    // 使用 std::memcmp 实现最高性能的位模式比对 (C++23 风格)
    bool viewport_changed = std::memcmp(&graph.view, &graph.m_last_view, sizeof(ViewState)) != 0;

    if (viewport_changed) [[unlikely]] {
        out_points.clear();
        out_points.reserve(graph.node_pool.size() * 128); // 全量重绘预分配
    }

    // 环境映射准备
    const ViewState& view = graph.view;
    NDCMap ndc_map = BuildNDCMap(view);
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> node_queue;
    node_queue.set_capacity(4096);

    // ---------------------------------------------------------
    // 2. 拓扑扩散：获取本帧逻辑受灾名单
    // ---------------------------------------------------------
    // FastScan 已按 ID 排序（用于 binary_search）
    std::vector<uint32_t> affected_ids = graph.FastScan();

    // ---------------------------------------------------------
    // 3. 核心管线：按 Rank 序循环 (保证因果律)
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
                    // --- 阶段 A: Solver (逻辑解算) ---
                    // 只有逻辑脏了，或者视图变了且是图解点，才运行 Solver
                    bool is_logic_dirty = std::binary_search(affected_ids.begin(), affected_ids.end(), node.id);
                    bool is_heuristic = node.result.check_f(ComputedResult::IS_HEURISTIC);

                    if (is_logic_dirty || (viewport_changed && is_heuristic)) {
                        // 级联有效性预检
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
                    // 条件：节点逻辑有效 且 (自身可见 或 孩子需要 Buffer 支撑)
                    if (node.result.check_f(ComputedResult::VALID)) {
                        bool child_needs = check_children_need_plot(node, graph.node_pool, graph.id_to_index_table);

                        if (node.config.is_visible || child_needs) {
                            // 只要视图动了，或者逻辑重新算了，就必须重新 Plot
                            if (viewport_changed || is_logic_dirty) {
                                if (node.render_task) {
                                    node.render_task(node, graph.node_pool, graph.id_to_index_table, view, ndc_map, node_queue);
                                    flush_node_results(node, node_queue, out_points);
                                }
                            }
                        } else if (is_logic_dirty) {
                            // 若逻辑变了但不再需要显示，重置点数
                            node.current_point_count = 0;
                        }
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }

    // ---------------------------------------------------------
    // 4. 指令生成：按物理序扫描 (保证后来者后画)
    // ---------------------------------------------------------
    out_ranges.clear();
    for (const auto& node : graph.node_pool) {
        // 只有活跃、有效且用户希望看到的对象才发出绘图指令
        if (node.active && node.result.check_f(ComputedResult::VALID) && node.config.is_visible) {
            out_ranges.push_back({ node.buffer_offset, node.current_point_count });
        }
    }

    // ---------------------------------------------------------
    // 5. 状态存档：完成 Ping-Pong 步进
    // ---------------------------------------------------------
    std::memcpy(&graph.m_last_view, &graph.view, sizeof(ViewState));
}