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
    bool should_resolve_in_render(const GeoNode& node, RenderUpdateMode mode) {
        if (!node.solver) return false;

        if (mode == RenderUpdateMode::Viewport) {
            // 视图更新：解析点坐标不动，只有需要回读 Buffer 的吸附点/交点才重算
            return node.is_heuristic;
        } else {
            // 增量更新：凡是涉及到 Buffer 或是吸附点后代的，都必须在此层 Rank 实时同步
            return node.is_heuristic || node.is_buffer_dependent;
        }
    }

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
                node.buffer_offset = static_cast<uint32_t>(current_physical_end);
                node.current_point_count = static_cast<uint32_t>(res.points.size());

            }
        }

    private:
        AlignedVector<PointData>& m_buffer;
        std::vector<GeoNode>& m_nodes;
    };
}

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    std::vector<GeoNode>& node_pool,
    const std::vector<uint32_t>& draw_order,
    const std::vector<uint32_t>& dirty_node_ids,
    const ViewState& view,
    RenderUpdateMode mode
) {
    // A. 环境初始化
    g_global_view_state = view;
    NDCMap ndc_map = BuildNDCMap(view);

    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(4096);
    ResultCollector collector(out_points, node_pool);

    // B. 确定待处理节点集
    // Viewport 模式处理所有 draw_order 中的可见节点
    // Incremental 模式仅处理由 SolveFrame 返回的脏节点
    std::vector<uint32_t> targets = (mode == RenderUpdateMode::Viewport) ? draw_order : dirty_node_ids;
    if (targets.empty()) return;

    // 如果是视图变化，物理清空 Buffer（采样点集已失效）
    if (mode == RenderUpdateMode::Viewport) {
        out_points.clear();
    }

    // C. 拓扑分层处理 (Rank 分桶)
    // 即使是局部更新也必须按 Rank 执行，以处理跨节点的 Buffer 回读依赖
    uint32_t max_rank = 0;
    for (uint32_t id : targets) max_rank = std::max(max_rank, node_pool[id].rank);

    std::vector<std::vector<uint32_t>> rank_batches(max_rank + 1);
    for (uint32_t id : targets) {
        rank_batches[node_pool[id].rank].push_back(id);
    }

    // D. 核心执行循环
    for (const auto& batch : rank_batches) {
        if (batch.empty()) continue;

        // 当前 Rank 层级全员并行执行
        oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
            GeoNode& node = node_pool[id];

            // 1. 【智能二次求解】
            // 如果节点依赖采样 Buffer，在此处实时同步世界坐标
            // 否则，直接信任 SolveFrame 算好的缓存值
            if (should_resolve_in_render(node, mode)) {
                node.solver(node, node_pool);
            }

            // 2. 【渲染投影/采样任务】
            // 此时：
            // - 解析点: 读 wx/wy，仅执行高精度投影 (world_to_clip_store)
            // - 采样对象: 执行高强度 LOD 采样渲染
            if (node.render_task && node.is_visible) {
                node.render_task(node, node_pool, view, ndc_map, results_queue);
            }
        });

        // 【关键同步点】
        // 每一层 Rank 并行结束后立即 Flush，确保下一层 Rank 的图解点能读取到这一层刚生成的像素点
        collector.Flush(results_queue);
    }

    // E. 组装输出元数据 (FunctionRanges)
    // 严格遵循 draw_order 的层级顺序，确保遮挡关系正确
    out_ranges.clear();
    out_ranges.reserve(draw_order.size());
    for (uint32_t id : draw_order) {
        const auto& node = node_pool[id];
        out_ranges.push_back({ node.buffer_offset, node.current_point_count });
    }
}


/**
 * @brief 执行增量更新提交 (Incremental Commit)
 * 场景：鼠标拖拽点、公式更新、创建新对象
 * 逻辑：先跑逻辑图求解，再将受影响的对象追加到 Buffer 末尾
 */
void commit_incremental_updates(GeometryGraph& graph, const ViewState& view, const std::vector<uint32_t>& draw_order) {
    // 1. 逻辑层：执行拓扑求解，计算受影响节点的最新世界坐标
    // SolveFrame 会清理桶并返回本次变动的可视化节点 ID

    std::vector<uint32_t> dirty_ids = graph.SolveFrame();







    // 如果没有任何逻辑变动，直接返回，节省渲染开销
    if (dirty_ids.empty()) return;

    // 2. 渲染层：执行追加渲染
    calculate_points_core(
        wasm_final_contiguous_buffer,
        wasm_function_ranges_buffer,
        graph.node_pool,
        draw_order,
        dirty_ids,
        view,
        RenderUpdateMode::Incremental
    );
}

/**
 * @brief 执行视口更新提交 (Viewport Commit)
 * 场景：Zoom(缩放)、Pan(平移)
 * 逻辑：跳过逻辑求解(世界坐标不变)，全量重刷采样 Buffer
 */
void commit_viewport_update(GeometryGraph& graph, const ViewState& view, const std::vector<uint32_t>& draw_order) {
    // 逻辑层：完全跳过 SolveFrame
    // 因为缩放和平移不改变解析点（如中点、解析交点）的世界坐标。

    // 渲染层：执行视图重刷模式
    calculate_points_core(
        wasm_final_contiguous_buffer,
        wasm_function_ranges_buffer,
        graph.node_pool,
        draw_order,
        {}, // 视图更新不依赖特定的脏 ID，它会遍历 draw_order
        view,
        RenderUpdateMode::Viewport
    );
}