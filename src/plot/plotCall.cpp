// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../include/graph/GeoSolver.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <unordered_set>
#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {

    constexpr uint32_t NULL_ID = 0xFFFFFFFF;

    // 辅助：位图扫描 (与 GeoGraph.cpp 保持一致)
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

    // =========================================================
    // 1. 判定逻辑
    // =========================================================
    /**
     * @brief 判定逻辑：
     * - Viewport 模式下：只有图解型（is_heuristic）需要重算（因为像素Buffer变了，吸附位置要变）。
     * - Incremental 模式下：图解型及其后代（is_buffer_dependent）都需要重算（同步最新的坐标）。
     *   实际上在 Incremental 模式下，为了绝对安全和代码简洁，所有脏节点都可以重算，
     *   因为解析节点的 Solver 开销极低。
     */
    bool should_resolve_in_render(const GeoNode& node, RenderUpdateMode mode) {
        if (!node.solver) return false;

        if (mode == RenderUpdateMode::Viewport) {
            // 视图更新：解析点坐标不动，只有需要回读 Buffer 的吸附点/交点才重算
            return node.is_heuristic;
        } else {
            // 增量更新：凡是脏的都重算，确保解析点也能读到刚刚通过 Solver 更新的图解点坐标
            return true;
        }
    }

    // =========================================================
    // 2. 结果收集器 (ResultCollector)
    // =========================================================
    class ResultCollector {
    public:
        ResultCollector(AlignedVector<PointData>& buffer, std::vector<GeoNode>& nodes)
            : m_buffer(buffer), m_nodes(nodes) {}

        void Flush(oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue) {
            FunctionResult res;
            while (queue.try_pop(res)) {
                GeoNode& node = m_nodes[res.function_index];

                // 即使没有点，也要更新偏移，防止数据错乱
                if (res.points.empty()) {
                    node.current_point_count = 0;
                    continue;
                }

                // 获取当前物理 Buffer 长度，追加到末尾
                size_t current_offset = m_buffer.size();
                m_buffer.insert(m_buffer.end(), res.points.begin(), res.points.end());

                // 移动对象指针指向新位置
                node.buffer_offset = static_cast<uint32_t>(current_offset);
                node.current_point_count = static_cast<uint32_t>(res.points.size());
            }
        }

    private:
        AlignedVector<PointData>& m_buffer;
        std::vector<GeoNode>& m_nodes;
    };
}

// =========================================================
// 3. 主计算入口 (calculate_points_core)
// =========================================================
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    GeometryGraph& graph, // 注意：改为传 GeometryGraph 引用以便访问 buckets_all_heads
    const std::vector<uint32_t>& draw_order,
    const std::vector<uint32_t>& dirty_node_ids,
    const ViewState& view,
    RenderUpdateMode mode
) {
    // A. 环境同步
    g_global_view_state = view;
    NDCMap ndc_map = BuildNDCMap(view);

    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(4096);
    ResultCollector collector(out_points, graph.node_pool);

    // B. 确定本轮需要处理的节点 ID 集合 (如果是 Viewport 模式)
    // 如果是 Incremental 模式，dirty_node_ids 已经是有序的了，不需要我们再分层
    // 但为了统一代码路径，我们还是构建一个临时的分层结构

    // 如果是视图变化，物理清空 Buffer（采样点集已失效）
    if (mode == RenderUpdateMode::Viewport) {
        out_points.clear();
        out_points.reserve(draw_order.size() * 100); // 预留空间减少扩容
    }

    if (mode == RenderUpdateMode::Incremental && dirty_node_ids.empty()) return;

    // C. 核心执行循环：利用位图跳跃扫描
    // 无论是 Viewport 还是 Incremental，都基于 graph 内部的拓扑层级进行遍历
    // 区别在于：Viewport 扫描全图，Incremental 扫描 dirty_node_ids 构成的子集

    if (mode == RenderUpdateMode::Incremental) {
        // --- 增量模式：由于 FastScan 已经返回了按 Rank 排序的 dirty_node_ids ---
        // 我们需要按 Rank 分批执行，以保证依赖正确
        // FastScan 保证了 dirty_node_ids 是按 Rank 递增的，但同一 Rank 的节点可能相邻

        // 简单策略：遍历 dirty_node_ids，收集同 Rank 的一批，并行执行，然后 Flush
        size_t i = 0;
        while (i < dirty_node_ids.size()) {
            uint32_t current_rank = graph.node_pool[dirty_node_ids[i]].rank;
            std::vector<uint32_t> batch;

            while (i < dirty_node_ids.size() && graph.node_pool[dirty_node_ids[i]].rank == current_rank) {
                batch.push_back(dirty_node_ids[i]);
                i++;
            }

            // 并行执行当前 Batch
            oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                GeoNode& node = graph.node_pool[id];
                if (should_resolve_in_render(node, mode)) {
                    node.solver(node, graph.node_pool);
                }
                if (node.render_task && node.is_visible) {
                    node.render_task(node, graph.node_pool, view, ndc_map, results_queue);
                }
            });
            collector.Flush(results_queue);
        }

    } else {
        // --- 视图模式：全量扫描 Graph 的 buckets_all ---
        // 利用 active_ranks_mask 跳过空层
        for (size_t w = 0; w < graph.active_ranks_mask.size(); ++w) {
            uint64_t mask = graph.active_ranks_mask[w];
            while (mask > 0) {
                uint32_t r_offset = find_first_set_bit_local(mask);
                uint32_t r = static_cast<uint32_t>(w * 64 + r_offset);

                // 收集该层所有节点
                std::vector<uint32_t> batch;
                uint32_t curr_id = graph.buckets_all_heads[r];
                while (curr_id != NULL_ID) {
                    // 仅处理 draw_order 中可见的节点，或者必须重算的图解节点
                    // 为简化逻辑，这里处理桶中所有节点，但在 render_task 中判断是否可见
                    batch.push_back(curr_id);
                    curr_id = graph.node_pool[curr_id].next_in_bucket;
                }

                if (!batch.empty()) {
                    oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                        GeoNode& node = graph.node_pool[id];
                        if (should_resolve_in_render(node, mode)) {
                            node.solver(node, graph.node_pool);
                        }
                        // 只有在 draw_order 里的才需要渲染，或者根据 is_visible
                        if (node.render_task && node.is_visible) {
                            node.render_task(node, graph.node_pool, view, ndc_map, results_queue);
                        }
                    });
                    collector.Flush(results_queue);
                }

                mask &= ~(1ULL << r_offset);
            }
        }
    }

    // E. 组装输出 Range 元数据
    out_ranges.clear();
    out_ranges.reserve(draw_order.size());
    for (uint32_t id : draw_order) {
        if (id < graph.node_pool.size()) {
            const auto& node = graph.node_pool[id];
            out_ranges.push_back({ node.buffer_offset, node.current_point_count });
        } else {
            out_ranges.push_back({ 0, 0 });
        }
    }
}

