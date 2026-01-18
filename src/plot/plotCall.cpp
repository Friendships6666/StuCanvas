// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/functions/lerp.h"
#include "../../include/graph/GeoSolver.h"
#include "../../pch.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <vector>
#include <algorithm>

namespace {
    /**
     * @brief 硬件级位扫描：辅助定位非空 Rank
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

        // 消费该节点产生的所有 TBB 队列分块
        while (queue.try_pop(res)) {
            if (res.points.empty()) continue;
            out_points.insert(out_points.end(), res.points.begin(), res.points.end());
        }

        // 写入大一统结果槽位外的显存元数据
        node.buffer_offset = start_offset;
        node.current_point_count = static_cast<uint32_t>(out_points.size()) - start_offset;
    }
}

// =========================================================
// 核心渲染调度入口 (去参数化、大一统架构版)
// =========================================================
void calculate_points_core(
    std::vector<PointData>& out_points,      // 物理点 Buffer
    std::vector<FunctionRange>& out_ranges,  // 范围索引 Buffer (给 JS 绘图)
    GeometryGraph& graph,                    // 包含 ViewState, LUT, Pool 的核心对象
    RenderUpdateMode mode
) {
    // 1. 获取当前视口并构建投影映射
    const ViewState& view = graph.view;
    NDCMap ndc_map = BuildNDCMap(view);

    // 预备局部队列，支持单个函数图像内部的并行采样
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> node_queue;
    node_queue.set_capacity(4096);

    // ---------------------------------------------------------
    // 步骤 A: 逻辑解算流 (因果律驱动)
    // ---------------------------------------------------------
    // 💡 自动消费 graph 内部积累的 seeds，扫描出本帧真正受波及的节点 ID
    std::vector<uint32_t> affected_ids = graph.FastScan();

    // 按照 Rank 从小到大执行 Solver
    for (uint32_t id : affected_ids) {
        GeoNode& node = graph.get_node_by_id(id);
        if (node.solver) {
            // 垂直注入：Pool, LUT, 以及 Graph 内部持有的 View
            node.solver(node, graph.node_pool, graph.id_to_index_table, view);
        }
    }

    // ---------------------------------------------------------
    // 步骤 B: 渲染输出流 (物理顺序驱动 = 后来者后画)
    // ---------------------------------------------------------

    // 如果视图变化，清空全局 Buffer 执行全量重绘
    if (mode == RenderUpdateMode::Viewport) {
        out_points.clear();
        out_points.reserve(graph.node_pool.size() * 128); // 预分配减少扩容
    }

    out_ranges.clear();

    // 💡 极致性能：线性扫描物理池。由于 vector::erase 会填补空隙，
    // 物理池的顺序天然就是 ID 的时间顺序 -> 实现“后来者居上”。
    for (GeoNode& node : graph.node_pool) {

        // 核心检查：只有活跃对象才进入渲染判定
        if (node.active) {

            // 1. 视图模式特殊处理：图解点（吸附点）必须针对新视口重新吸附
            if (mode == RenderUpdateMode::Viewport &&
                node.result.check_f(ComputedResult::IS_HEURISTIC))
            {
                node.solver(node, graph.node_pool, graph.id_to_index_table, view);
            }

            // 2. 状态检查：必须有效且可见
            if (node.result.check_f(ComputedResult::VALID) &&
                node.result.check_f(ComputedResult::VISIBLE) &&
                node.render_task)
            {
                // 3. 判定本帧是否需要触发 Plot (采样)?
                // - 情况 1：视图变了，所有人都要重投点。
                // - 情况 2：逻辑变了（在 affected 列表中），需要重采样。
                bool needs_replot = (mode == RenderUpdateMode::Viewport);
                if (!needs_replot) {
                    // FastScan 返回的是有序列表，二分查找 O(log N)
                    needs_replot = std::binary_search(affected_ids.begin(), affected_ids.end(), node.id);
                }

                if (needs_replot) {
                    // 触发节点内部采样任务（可以是并行的）
                    node.render_task(node, graph.node_pool, graph.id_to_index_table, view, ndc_map, node_queue);
                    // 顺序刷入结果 Buffer
                    flush_node_results(node, node_queue, out_points);
                }

                // 4. 封装 Range 信息给前端 (天然有序)
                out_ranges.push_back({ node.buffer_offset, node.current_point_count });
            }
        }
    }
}