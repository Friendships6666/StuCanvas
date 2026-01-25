// --- src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
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
    // --- src/plot/plotCall.cpp ---

    void RefreshAllLabels(GeometryGraph& graph) {
        auto& label_buffer = graph.final_labels_buffer;
        label_buffer.clear();

        // 1. 全局开关检查 (检查 64 位掩码的第一位)
        if (graph.global_state_mask & GlobalState::DISABLE_LABELS) {
            return;
        }

        const auto& points = graph.final_points_buffer;

        // 2. 快速遍历所有活跃节点
        for (const auto& node : graph.node_pool) {
            // 判定：活跃、有采样点、且节点自身开启了标签
            if (!node.active || node.current_point_count == 0 || !node.config.label.show) {
                continue;
            }

            // --- 核心算法：确定锚点索引 ---
            uint32_t anchor_idx;
            if (node.type == GeoType::LINE_SEGMENT || node.type == GeoType::LINE_STRAIGHT) {
                // 线段对象：取中间那个点
                anchor_idx = node.buffer_offset + (node.current_point_count >> 1); // >>1 比 /2 快
            } else {
                // 点对象或曲线：直接取第一个点
                anchor_idx = node.buffer_offset;
            }

            const PointData& anchor = points[anchor_idx];

            // --- 极致性能：直接整数加法，没有任何浮点转换或缩放 ---
            // 这里的 offset 已经是 16 位整数单位
            label_buffer.push_back({
                {
                    static_cast<int16_t>(anchor.x + node.config.label.offset_x),
                    static_cast<int16_t>(anchor.y + node.config.label.offset_y)
                },
                node.id
            });
        }
    }

    /**
     * @brief 缓冲区压缩 (GC)
     */
    void CompactBuffer(GeometryGraph& graph) {
        auto& buffer = graph.final_points_buffer; // 类型为 std::vector<PointData>

        // 修正 1: 这里统一使用 PointData
        std::vector<PointData> new_buffer;
        new_buffer.reserve(buffer.capacity());

        for (auto& node : graph.node_pool) {
            if (node.active && node.current_point_count > 0) {
                uint32_t old_offset = node.buffer_offset;
                uint32_t count = node.current_point_count;

                node.buffer_offset = static_cast<uint32_t>(new_buffer.size());

                // 修正 2: 插入逻辑保持一致
                new_buffer.insert(new_buffer.end(),
                                  buffer.begin() + old_offset,
                                  buffer.begin() + old_offset + count);
            }
        }
        // 修正 3: 现在类型一致，std::move 正常工作
        graph.final_points_buffer = std::move(new_buffer);
    }

    /**
     * @brief 刷新节点采样结果
     */
    void flush_node_results(
        GeoNode& node,
        oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& queue, // 修正类型
        GeometryGraph& graph
    ) {
        if (!graph.is_healthy()) {
            node.current_point_count = 0;
            std::vector<PointData> dummy;
            while (queue.try_pop(dummy));
            return;
        }

        auto& out_points = graph.final_points_buffer;

        std::vector<PointData> all_new_points;
        std::vector<PointData> chunk;
        while (queue.try_pop(chunk)) {
            if (!chunk.empty()) {
                all_new_points.insert(all_new_points.end(), chunk.begin(), chunk.end());
            }
        }

        uint32_t new_count = static_cast<uint32_t>(all_new_points.size());
        if (new_count == 0) {
            node.current_point_count = 0;
            return;
        }

        // 计算字节大小时使用 PointData
        size_t incoming_bytes = new_count * sizeof(PointData);
        size_t current_bytes = out_points.size() * sizeof(PointData);

        if (current_bytes + incoming_bytes > graph.max_buffer_bytes) {
            CompactBuffer(graph);
            current_bytes = out_points.size() * sizeof(PointData);
            if (current_bytes + incoming_bytes > graph.max_buffer_bytes) {
                graph.status = GraphStatus::ERR_OUT_OF_MEMORY;
                node.current_point_count = 0;
                return;
            }
        }

        node.buffer_offset = static_cast<uint32_t>(out_points.size());
        node.current_point_count = new_count;
        out_points.insert(out_points.end(), all_new_points.begin(), all_new_points.end());
    }

    bool check_children_need_plot(const GeoNode& node, const GeometryGraph& graph) {
        for (uint32_t cid : node.children) {
            const auto& child = graph.get_node_by_id(cid);
            if (child.active && (child.result.check_f(ComputedResult::IS_HEURISTIC) || child.config.is_visible)) {
                return true;
            }
        }
        return false;
    }
}

void calculate_points_core(GeometryGraph& graph) {
    if (!graph.is_healthy()) return;

    auto& out_points = graph.final_points_buffer;
    auto& out_meta = graph.final_meta_buffer;

    bool viewport_changed = graph.detect_view_change();
    if (viewport_changed) {
        out_points.clear();
        for (auto& n : graph.node_pool) {
            if (n.active) graph.mark_as_seed(n.id);
        }
    }

    // 💡 这里 view 就是 ViewState 类型
    const ViewState& view = graph.view;
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>> node_queue;
    node_queue.set_capacity(4096);

    std::vector<uint32_t> affected_ids = graph.FastScan();

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

                    if (is_logic_dirty || (viewport_changed && is_heuristic)) {
                        if (is_heuristic) {
                            for (uint32_t pid : node.parents) {
                                GeoNode& parent = graph.get_node_by_id(pid);
                                if (parent.active && parent.current_point_count == 0 && parent.render_task) {
                                    // 修正签名后，这里不再报错
                                    parent.render_task(parent, graph, view, node_queue);
                                    flush_node_results(parent, node_queue, graph);
                                }
                            }
                        }

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

                    if (GeoStatus::ok(node.status)) {
                        bool child_needs = check_children_need_plot(node, graph);

                        if (node.config.is_visible || child_needs) {
                            if (viewport_changed || is_logic_dirty) {
                                if (node.render_task) {
                                    // 💡 关键调用点：确保签名匹配
                                    node.render_task(node, graph, view, node_queue);
                                    flush_node_results(node, node_queue, graph);
                                }
                            }
                        } else if (is_logic_dirty) {
                            node.current_point_count = 0;
                        }
                    }

                    if (is_logic_dirty) {
                        node.result.set_f(ComputedResult::DIRTY, false);
                    }
                }
                curr_id = node.next_in_bucket;
            }
            mask &= ~(1ULL << r_offset);
        }
    }

    out_meta.clear();
    for (const auto& node : graph.node_pool) {
        if (node.active && GeoStatus::ok(node.status) && node.config.is_visible && node.current_point_count > 0) {
            out_meta.push_back({
                node.buffer_offset,
                node.current_point_count,
                node.id,
                node.type,
                node.config
            });
        }
    }
    RefreshAllLabels(graph);

    graph.sync_view_snapshot();
    graph.m_pending_seeds.clear();
    std::ranges::fill(graph.m_dirty_mask, 0);
}