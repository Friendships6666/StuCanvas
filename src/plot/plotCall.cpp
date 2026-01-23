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
#include <iostream>

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

    /**
     * @brief 缓冲区压缩 (Garbage Collection)
     * 作用：剔除 Ring Buffer 中废弃的旧数据，重整内存布局
     */
    void CompactBuffer(GeometryGraph& graph) {
        auto& buffer = graph.final_points_buffer;

        std::vector<PointData> new_buffer;
        new_buffer.reserve(buffer.size() / 2);

        for (auto& node : graph.node_pool) {
            // 只保留 active 且拥有有效采样点的节点数据
            if (node.active && node.current_point_count > 0) {
                uint32_t old_offset = node.buffer_offset;
                uint32_t count = node.current_point_count;

                // 记录新偏移量
                uint32_t new_offset = static_cast<uint32_t>(new_buffer.size());

                // 搬运数据
                new_buffer.insert(new_buffer.end(),
                                  buffer.begin() + old_offset,
                                  buffer.begin() + old_offset + count);

                // 更新节点的重定向索引
                node.buffer_offset = new_offset;
            }
        }

        graph.final_points_buffer = std::move(new_buffer);
        // std::cout << "[GC] Buffer compacted. New size: " << graph.final_points_buffer.size() << std::endl;
    }

    /**
     * @brief 刷新节点采样结果 (Ring Buffer 策略 + 熔断保护)
     */
    void flush_node_results(
        GeoNode& node,
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue,
        GeometryGraph& graph
    ) {
        // 1. 健康检查：如果已经 OOM 了，停止追加
        if (!graph.is_healthy()) {
            node.current_point_count = 0;
            FunctionResult dummy;
            while (queue.try_pop(dummy));
            return;
        }

        auto& out_points = graph.final_points_buffer;

        // 2. 收集新数据
        std::vector<PointData> new_points;
        FunctionResult res;
        while (queue.try_pop(res)) {
            if (!res.points.empty()) {
                new_points.insert(new_points.end(), res.points.begin(), res.points.end());
            }
        }

        uint32_t new_count = static_cast<uint32_t>(new_points.size());
        if (new_count == 0) {
            node.current_point_count = 0;
            return;
        }

        // 3. 容量预检与熔断
        size_t current_bytes = out_points.size() * sizeof(PointData);
        size_t incoming_bytes = new_count * sizeof(PointData);

        if (current_bytes + incoming_bytes > graph.max_buffer_bytes) {
            // 尝试 GC
            CompactBuffer(graph);

            current_bytes = out_points.size() * sizeof(PointData);
            if (current_bytes + incoming_bytes > graph.max_buffer_bytes) {
                // 彻底耗尽，设置错误状态
                graph.status = GraphStatus::ERR_OUT_OF_MEMORY;
                // std::cerr << "[Critical] Memory Limit Exceeded! Halting computation." << std::endl;
                node.current_point_count = 0;
                return;
            }
        }

        // 4. 安全追加 (Append Only)
        // 只有这里才会真正扩容，且由 vector 自动管理增长策略
        uint32_t new_offset = static_cast<uint32_t>(out_points.size());
        out_points.insert(out_points.end(), new_points.begin(), new_points.end());

        // 5. 更新索引指向新位置
        node.buffer_offset = new_offset;
        node.current_point_count = new_count;
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
// 核心渲染调度入口
// =========================================================
void calculate_points_core(GeometryGraph& graph) {
    // 0. 全局准入检查
    if (!graph.is_healthy()) return;

    auto& out_points = graph.final_points_buffer;
    auto& out_ranges = graph.final_ranges_buffer;

    // 1. 视图与 GC 检测
    bool viewport_changed = graph.detect_view_change();

    // 💡 只有在视图变化（全量重算）时才清空 Buffer
    // 此时相当于 Ring Buffer 的指针归零重置
    if (viewport_changed) {
        out_points.clear();
        // 标记所有活跃节点为 dirty，强迫它们重新采样
        // 注意：不需 shrink_to_fit，保留容量供下一帧复用
        for (auto& n : graph.node_pool) {
            if (n.active) graph.mark_as_seed(n.id);
        }
    }

    const ViewState& view = graph.view;
    NDCMap ndc_map = BuildNDCMap(view);
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> node_queue;
    node_queue.set_capacity(4096);

    std::vector<uint32_t> affected_ids = graph.FastScan();

    // 2. 核心管线
    for (size_t w = 0; w < graph.active_ranks_mask.size(); ++w) {
        uint64_t mask = graph.active_ranks_mask[w];
        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit_local(mask);
            uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

            uint32_t curr_id = graph.buckets_all_heads[r];
            while (curr_id != NULL_ID) {
                GeoNode& node = graph.get_node_by_id(curr_id);

                if (node.active) {
                    bool is_logic_dirty = std::ranges::binary_search(affected_ids, node.id);
                    bool is_heuristic = node.result.check_f(ComputedResult::IS_HEURISTIC);

                    // --- 阶段 A: Solver ---
                    if (is_logic_dirty || (viewport_changed && is_heuristic)) {

                        // 💡 采样保障机制：确保父节点已有采样点
                        if (is_heuristic) {
                            for (uint32_t pid : node.parents) {
                                GeoNode& parent = graph.get_node_by_id(pid);
                                // 如果父亲应该有数据但还是 0 (增量跳过导致)，强制补跑一次渲染
                                if (parent.active && parent.current_point_count == 0 && parent.render_task) {
                                    parent.render_task(parent, graph, ndc_map, node_queue);
                                    flush_node_results(parent, node_queue, graph);
                                }
                            }
                        }

                        // 父节点状态检查
                        bool parents_ok = true;
                        for (uint32_t pid : node.parents) {
                            if (!GeoStatus::ok(graph.get_node_by_id(pid).status)) {
                                parents_ok = false; break;
                            }
                        }

                        if (parents_ok) {
                            node.solver(node, graph);
                        } else {
                            node.status = GeoStatus::ERR_PARENT_INVALID;
                        }
                    }

                    // --- 阶段 B: Plot ---
                    if (GeoStatus::ok(node.status)) {
                        bool child_needs = check_children_need_plot(node, graph.node_pool, graph.id_to_index_table);

                        if (node.config.is_visible || child_needs) {
                            // 增量策略：只有变脏了才追加新点，否则保留原 offset 指向的旧点
                            if (viewport_changed || is_logic_dirty) {
                                if (node.render_task) {
                                    node.render_task(node, graph, ndc_map, node_queue);
                                    // 💡 调用带熔断检查的 flush
                                    flush_node_results(node, node_queue, graph);
                                }
                            }
                        } else if (is_logic_dirty) {
                            node.current_point_count = 0;
                        }
                    }

                    // --- 阶段 C: 状态清理 ---
                    if (is_logic_dirty) {
                        node.result.set_f(ComputedResult::DIRTY, false);
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }

    // 3. 指令生成
    out_ranges.clear();
    for (const auto& node : graph.node_pool) {
        if (node.active && GeoStatus::ok(node.status) && node.config.is_visible) {
            out_ranges.push_back({ node.buffer_offset, node.current_point_count });
        }
    }

    graph.sync_view_snapshot();
    graph.m_pending_seeds.clear();
    std::ranges::fill(graph.m_dirty_mask, 0);
}