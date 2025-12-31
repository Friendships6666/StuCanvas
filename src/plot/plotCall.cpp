// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <unordered_set>
#include <algorithm>
#include <cstring>

namespace {

    // =========================================================
    // 1. 结果收集器 (ResultCollector)
    // 负责将不同线程算出的结果，高效同步到全局大 Buffer
    // =========================================================
    class ResultCollector {
    public:
        ResultCollector(AlignedVector<PointData>& buffer, std::vector<GeoNode>& nodes, bool reset)
            : m_buffer(buffer), m_nodes(nodes)
        {
            m_write_ptr = reset ? 0 : buffer.size();
        }

        void Flush(oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue) {
            FunctionResult res;
            while (queue.try_pop(res)) {
                size_t count = res.points.size();
                GeoNode& node = m_nodes[res.function_index];

                if (count == 0) {
                    node.buffer_offset = 0;
                    node.current_point_count = 0;
                    continue;
                }

                size_t current_offset = m_write_ptr;

                // 物理扩容
                if (current_offset + count > m_buffer.capacity()) {
                    m_buffer.reserve(std::max(m_buffer.capacity() * 2, current_offset + count));
                }

                // 逻辑调整
                if (current_offset + count > m_buffer.size()) {
                    m_buffer.resize(current_offset + count);
                }

                // 高速拷贝
                std::memcpy(m_buffer.data() + current_offset, res.points.data(), count * sizeof(PointData));

                // 元数据回写
                node.buffer_offset = (uint32_t)current_offset;
                node.current_point_count = (uint32_t)count;

                m_write_ptr += count;
            }
        }

    private:
        AlignedVector<PointData>& m_buffer;
        std::vector<GeoNode>& m_nodes;
        size_t m_write_ptr;
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
    // A. 环境同步
    g_global_view_state = view;
    NDCMap ndc_map = BuildNDCMap(view);

    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(2048);
    ResultCollector collector(out_points, node_pool, is_global_update);

    // B. 依赖管理与执行序列准备
    if (is_global_update) {
        // 1. BFS 找出所有必须参与计算的节点 (包含隐形依赖，如垂足)
        std::unordered_set<uint32_t> nodes_to_solve;
        std::vector<uint32_t> q;
        q.reserve(draw_order.size());

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

        // 2. 拓扑分层 (Rank 分桶)
        uint32_t max_rank = 0;
        for (uint32_t id : nodes_to_solve) max_rank = std::max(max_rank, node_pool[id].rank);

        std::vector<std::vector<uint32_t>> rank_batches(max_rank + 1);
        for (uint32_t id : nodes_to_solve) rank_batches[node_pool[id].rank].push_back(id);

        // 3. 标记渲染白名单
        std::vector<bool> is_draw_target(node_pool.size(), false);
        for (uint32_t id : draw_order) is_draw_target[id] = true;

        // C. 执行计算核心
        for (const auto& batch : rank_batches) {
            if (batch.empty()) continue;

            // Batch 内全员并行
            oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                GeoNode& node = node_pool[id];

                // [JIT Solver] 如果节点有求解器，先运行
                if (node.solver) {
                    node.solver(node, node_pool);
                }

                // [Delegated Rendering] 节点自带渲染代理函数
                if (is_draw_target[id] && node.render_task) {
                    node.render_task(node, node_pool, view, ndc_map, results_queue);
                }
            });

            // Rank 层间同步：所有父级点数据必须全部写回 Buffer，下一层 Rank 才能安全回读
            collector.Flush(results_queue);
        }

    } else {
        // --- 局部增量模式 ---
        if (!dirty_node_ids.empty()) {
            oneapi::tbb::parallel_for_each(dirty_node_ids.begin(), dirty_node_ids.end(), [&](uint32_t id) {
                GeoNode& node = node_pool[id];
                if (node.solver) node.solver(node, node_pool);
                if (node.render_task) node.render_task(node, node_pool, view, ndc_map, results_queue);
            });
            collector.Flush(results_queue);
        }
    }

    // D. 组装 WebGPU FunctionRanges
    // 这里的顺序严格遵循 draw_order（即“画布层级”），决定物体遮挡关系
    out_ranges.clear();
    out_ranges.reserve(draw_order.size());
    for (uint32_t id : draw_order) {
        out_ranges.push_back({ node_pool[id].buffer_offset, node_pool[id].current_point_count });
    }
}