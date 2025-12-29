// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/plot/plotSegment.h"   // 处理直线和线段
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/functions/lerp.h"
#include "../../include/plot/plotCircle.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for_each.h>
#include "../../pch.h"
#include <oneapi/tbb/global_control.h>
#include <variant>
#include <algorithm>
#include <cstring>

namespace {

    // =========================================================
    // 1. 结果收集器 (ResultCollector)
    // 负责将不同线程算出的结果，有序或无序地写入全局大 Buffer
    // =========================================================
    class ResultCollector {
    public:
        ResultCollector(AlignedVector<PointData>& buffer, std::vector<GeoNode>& nodes, bool reset)
            : m_buffer(buffer), m_nodes(nodes)
        {
            // 全局模式：写指针重置为0，覆盖旧数据；增量模式：从末尾追加
            m_write_ptr = reset ? 0 : buffer.size();
        }

        /**
         * @brief 将队列里的数据同步到主显存 Buffer，并更新节点元数据
         */
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

                // 1. 检查物理扩容 (贪婪策略)
                if (current_offset + count > m_buffer.capacity()) {
                    m_buffer.reserve(std::max(m_buffer.capacity() * 2, current_offset + count));
                }

                // 2. 调整逻辑大小 (保证 JS HEAP 视图有效)
                if (current_offset + count > m_buffer.size()) {
                    m_buffer.resize(current_offset + count);
                }

                // 3. 高速拷贝 (POD 数据)
                std::memcpy(
                    m_buffer.data() + current_offset,
                    res.points.data(),
                    count * sizeof(PointData)
                );

                // 4. 更新节点的物理位置记录
                node.buffer_offset = (uint32_t)current_offset;
                node.current_point_count = (uint32_t)count;

                // 5. 推进全局指针
                m_write_ptr += count;
            }
        }

    private:
        AlignedVector<PointData>& m_buffer;
        std::vector<GeoNode>& m_nodes;
        size_t m_write_ptr{};
    };

    // =========================================================
    // 2. 任务分发器 (DispatchNodeTask)
    // 负责解析 GeoNode 的类型并调用对应的底层计算模块
    // =========================================================
    void DispatchNodeTask(
        GeoNode& node,
        const std::vector<GeoNode>& pool,
        const ViewState& view,
        const NDCMap& ndc_map,
        oneapi::tbb::concurrent_bounded_queue<FunctionResult>& queue
    ) {

        switch (node.render_type) {

            case GeoNode::RenderType::Point: {
                // 绘制单一几何点 (自由点、中点、交点、约束点)
                if (std::holds_alternative<Data_Point>(node.data)) {
                    const auto& p = std::get<Data_Point>(node.data);
                    PointData pd{};
                    // 执行 Double -> Float Clip 转换
                    world_to_clip_store(pd, p.x, p.y, ndc_map, node.id);
                    queue.push({ node.id, {pd} });
                }
                break;
            }

            case GeoNode::RenderType::Line: {
                // 情况 A: 依赖两个点 ID 的普通线 (如连接两点)
                if (std::holds_alternative<Data_Line>(node.data)) {
                    const auto& d = std::get<Data_Line>(node.data);
                    const auto& p1 = std::get<Data_Point>(pool[d.p1_id].data);
                    const auto& p2 = std::get<Data_Point>(pool[d.p2_id].data);
                    process_two_point_line(
                        &queue, p1.x, p1.y, p2.x, p2.y,
                        !d.is_infinite, node.id,
                        view.world_origin, view.wppx, view.wppy,
                        view.screen_width, view.screen_height,
                        0.0, 0.0, ndc_map
                    );
                }
                // ★★★ 情况 B: 求解器直接算出的线 (切线) ★★★
                // 直接使用结构体里的坐标，不需要查 ID
                else if (std::holds_alternative<Data_CalculatedLine>(node.data)) {
                    const auto& d = std::get<Data_CalculatedLine>(node.data);
                    process_two_point_line(
                        &queue, d.x1, d.y1, d.x2, d.y2,
                        !d.is_infinite, node.id,
                        view.world_origin, view.wppx, view.wppy,
                        view.screen_width, view.screen_height,
                        0.0, 0.0, ndc_map
                    );
                }
                break;
            }

            case GeoNode::RenderType::Circle: {
                if (std::holds_alternative<Data_Circle>(node.data)) {
                    const auto& d = std::get<Data_Circle>(node.data);
                    process_circle_specialized(
                        &queue,
                        d.cx, d.cy, d.radius,
                        node.id,
                        view.world_origin, view.wppx, view.wppy,
                        view.screen_width, view.screen_height,
                        ndc_map
                    );
                }
                break;
            }

            case GeoNode::RenderType::Explicit: {
                // 绘制显函数 y = f(x)
                if (std::holds_alternative<Data_SingleRPN>(node.data)) {
                    const auto& d = std::get<Data_SingleRPN>(node.data);
                    process_explicit_chunk(
                        view.world_origin.x, view.world_origin.x + view.screen_width * view.wppx,
                        d.tokens, &queue, node.id, view.screen_width, ndc_map
                    );
                }
                break;
            }

            case GeoNode::RenderType::Parametric: {
                // 绘制参数方程 x=f(t), y=g(t)
                if (std::holds_alternative<Data_DualRPN>(node.data)) {
                    const auto& d = std::get<Data_DualRPN>(node.data);
                    process_parametric_chunk(
                        d.tokens_x, d.tokens_y, d.t_min, d.t_max,
                        &queue, node.id, ndc_map
                    );
                }
                break;
            }

            case GeoNode::RenderType::Implicit: {
                // 绘制通用隐函数 f(x,y) = 0
                if (std::holds_alternative<Data_SingleRPN>(node.data)) {
                    const auto& d = std::get<Data_SingleRPN>(node.data);
                    process_implicit_adaptive(
                        &queue, view.world_origin, view.wppx, view.wppy,
                        view.screen_width, view.screen_height,
                        d.tokens, d.tokens, node.id, 0.0, 0.0, ndc_map
                    );
                }
                break;
            }

            default: break;
        }
    }
}

// =========================================================
// 3. 主计算入口 (calculate_points_core)
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
    g_global_view_state = view;

    // A. 准备坐标转换器 (NDCMap)
    double half_w = view.screen_width * 0.5;
    double half_h = view.screen_height * 0.5;
    Vec2 center_world = screen_to_world_inline({half_w, half_h}, view.world_origin, view.wppx, view.wppy);

    NDCMap ndc_map{};
    ndc_map.center_x = center_world.x;
    ndc_map.center_y = center_world.y;
    ndc_map.scale_x = 2.0 / (view.screen_width * view.wppx);
    ndc_map.scale_y = 2.0 / (view.screen_height * view.wppy);

    // B. 初始化并发队列与收集器
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(2048);
    ResultCollector collector(out_points, node_pool, is_global_update);

    /// C. 执行计算调度
    if (is_global_update) {
        // --- 阶段 1: 依赖发现 (BFS) ---
        // 目的：找出 draw_order 中所有节点依赖的祖先节点（包括不渲染的中间节点，如垂足）
        std::unordered_set<uint32_t> nodes_to_solve;
        std::vector<uint32_t> q;
        q.reserve(draw_order.size());

        // 种子：将所有渲染目标加入队列
        for (uint32_t id : draw_order) {
            if (nodes_to_solve.insert(id).second) {
                q.push_back(id);
            }
        }

        // 扩散：向上查找父节点
        size_t head = 0;
        while(head < q.size()){
            uint32_t curr_id = q[head++];
            const GeoNode& node = node_pool[curr_id];
            for (uint32_t pid : node.parents) {
                // 如果父节点还没被加入，则加入队列
                if (nodes_to_solve.insert(pid).second) {
                    q.push_back(pid);
                }
            }
        }

        // --- 阶段 2: 拓扑分层 (Rank Bucketing) ---

        // 1. 扫描最大 Rank (注意：必须遍历 nodes_to_solve 全集)
        uint32_t max_rank = 0;
        for (uint32_t id : nodes_to_solve) {
            if (node_pool[id].rank > max_rank) max_rank = node_pool[id].rank;
        }

        // 2. 创建 Rank 桶 (注意：必须遍历 nodes_to_solve 全集)
        std::vector<std::vector<uint32_t>> rank_batches(max_rank + 1);
        for (uint32_t id : nodes_to_solve) {
            rank_batches[node_pool[id].rank].push_back(id);
        }

        // 3. 标记绘制目标 (用于在执行时区分只算不画的节点)
        std::vector<bool> is_draw_target(node_pool.size(), false);
        for(uint32_t id : draw_order) is_draw_target[id] = true;

        // --- 阶段 3: 逐层执行 (Layer-by-Layer Execution) ---
        for (const auto& batch : rank_batches) {
            if (batch.empty()) continue;

            // 并行处理当前 Rank 层的所有节点
            oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                GeoNode& node = node_pool[id];

                // [Step A: JIT 求解]
                // 无论是否需要绘制，只要有 Solver 就必须运行。
                // 这确保了下一层节点能读取到正确的数据（例如：先算好垂足坐标，才能算垂线）。
                if (node.solver) {
                    node.solver(node, node_pool);
                }

                // [Step B: 绘制分发]
                // 只有在 draw_order 里的节点才生成 PointData
                if (is_draw_target[id]) {
                    DispatchNodeTask(node, node_pool, view, ndc_map, results_queue);
                }
            });

            // [Barrier] 关键同步点：
            // 必须等待当前层所有数据写回 Buffer，才能开始下一层计算。
            collector.Flush(results_queue);
        }
    }
}