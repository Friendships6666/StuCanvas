// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <unordered_set>
#include <algorithm>
#include <vector>

namespace {

    // =========================================================
    // 1. 结果收集器 (ResultCollector)
    // 负责将渲染结果追加到全局 Buffer 的末尾 (Ring Buffer 逻辑)
    // =========================================================
    class ResultCollector {
    public:
        ResultCollector(AlignedVector<PointData>& buffer, std::vector<GeoNode>& nodes)
            : m_buffer(buffer), m_nodes(nodes) {}

        /**
         * @brief 将队列中的渲染结果写入物理 Buffer
         * @note 无论全局还是局部更新，都采用追加到当前末尾的逻辑
         */
        void Flush(oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue) {
            FunctionResult res;
            while (queue.try_pop(res)) {
                GeoNode& node = m_nodes[res.function_index];

                if (res.points.empty()) {
                    node.current_point_count = 0;
                    continue;
                }

                // --- Ring Buffer / Append 核心：获取当前物理 Buffer 的长度作为起始偏移 ---
                size_t current_physical_end = m_buffer.size();

                // 物理追加：老数据不动，新数据直接排在后面
                m_buffer.insert(m_buffer.end(), res.points.begin(), res.points.end());

                // --- 指针重定向：移动对象指针指向新追加的数据块 ---
                node.buffer_offset = (uint32_t)current_physical_end;
                node.current_point_count = (uint32_t)res.points.size();
            }
        }

    private:
        AlignedVector<PointData>& m_buffer;
        std::vector<GeoNode>& m_nodes;
    };
}

// =========================================================
// 2. 主计算入口 (calculate_points_core)
// =========================================================
void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    std::vector<GeoNode>& node_pool,
    const std::vector<uint32_t>& draw_order,
    const std::vector<uint32_t>& dirty_node_ids,
    const ViewState& view,
    bool is_global_update
) {
    // A. 初始化环境
    g_global_view_state = view;
    NDCMap ndc_map = BuildNDCMap(view);

    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(4096);
    ResultCollector collector(out_points, node_pool);

    if (is_global_update) {
        // =====================================================
        // 情况 1: 全局更新 (缩放、平移或初始化)
        // =====================================================

        // 1. 物理清空：重置大 Buffer
        out_points.clear();
        out_points.reserve(draw_order.size() * 100); // 预估空间

        // 2. 依赖追踪：计算所有必须参与的对象
        std::unordered_set<uint32_t> nodes_to_solve;
        std::vector<uint32_t> q;
        for (uint32_t id : draw_order) {
            if (nodes_to_solve.insert(id).second) q.push_back(id);
        }
        size_t head = 0;
        while (head < q.size()) {
            uint32_t curr_id = q[head++];
            for (uint32_t pid : node_pool[curr_id].parents) {
                if (nodes_to_solve.insert(pid).second) q.push_back(pid);
            }
        }

        // 3. 拓扑分层 (Rank 分桶)
        uint32_t max_rank = 0;
        for (uint32_t id : nodes_to_solve) max_rank = std::max(max_rank, node_pool[id].rank);
        std::vector<std::vector<uint32_t>> rank_batches(max_rank + 1);
        for (uint32_t id : nodes_to_solve) rank_batches[node_pool[id].rank].push_back(id);

        // 4. 并行渲染黑名单（只有在 draw_order 里的才真正执行渲染任务）
        std::vector<bool> is_draw_target(node_pool.size(), false);
        for (uint32_t id : draw_order) is_draw_target[id] = true;

        for (const auto& batch : rank_batches) {
            if (batch.empty()) continue;
            oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                GeoNode& node = node_pool[id];
                // 全局模式需要先运行 Solver 确保坐标正确
                if (node.solver) node.solver(node, node_pool);
                // 只有目标对象才推送渲染结果
                if (is_draw_target[id] && node.render_task) {
                    node.render_task(node, node_pool, view, ndc_map, results_queue);
                }
            });
            // 每一层 Rank 结束后同步一次 Buffer
            collector.Flush(results_queue);
        }

    } else {
        // =====================================================
        // 情况 2: 局部增量更新 (Ring Buffer 追加模式)
        // =====================================================
        // 注意：这里绝对不 clear out_points，保留之前的所有点

        if (!dirty_node_ids.empty()) {
            // 在增量模式下，我们假设 SolveFrame 已经跑过了（坐标已更新）
            // 这里只并行运行渲染任务 (Rasterization)
            oneapi::tbb::parallel_for_each(dirty_node_ids.begin(), dirty_node_ids.end(),
                [&](uint32_t id) {
                    GeoNode& node = node_pool[id];
                    // 只处理需要渲染的节点
                    if (node.render_task && node.render_type != GeoNode::RenderType::None) {
                        node.render_task(node, node_pool, view, ndc_map, results_queue);
                    }
                }
            );

            // 追加到 Buffer 末尾，并“重定向”这些脏节点的指针
            collector.Flush(results_queue);
        }
    }

    // =========================================================
    // D. 组装 WebGPU 所需的 Range 信息
    // =========================================================
    // 关键：JS 端拿到的 StartIndex 会随追加而变大，Count 是新的点数
    out_ranges.clear();
    out_ranges.reserve(draw_order.size());
    for (uint32_t id : draw_order) {
        out_ranges.push_back({
            node_pool[id].buffer_offset,
            node_pool[id].current_point_count
        });
    }
}