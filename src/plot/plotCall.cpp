// --- 文件路径: src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/plot/plotSegment.h"   // 处理直线和线段
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/functions/lerp.h"
#include "../../include/plot/plotCircle.h" // ★★★ 新增：包含特化圆绘制头文件
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/parallel_for_each.h>
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
        size_t m_write_ptr;
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
                    PointData pd;
                    // 执行 Double -> Float Clip 转换
                    world_to_clip_store(pd, p.x, p.y, ndc_map, node.id);
                    queue.push({ node.id, {pd} });
                }
                break;
            }

            case GeoNode::RenderType::Line: {
                // 绘制直线或线段
                if (std::holds_alternative<Data_Line>(node.data)) {
                    const auto& d = std::get<Data_Line>(node.data);
                    // 动态提取父节点(端点)的最新数学坐标
                    const auto& p1 = std::get<Data_Point>(pool[d.p1_id].data);
                    const auto& p2 = std::get<Data_Point>(pool[d.p2_id].data);

                    // 调用 Liang-Barsky 裁剪算法
                    process_two_point_line(
                        &queue, p1.x, p1.y, p2.x, p2.y,
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

                    // ★★★ 核心修改：调用特化圆绘制函数 ★★★
                    process_circle_specialized(
                        &queue,
                        d.cx, d.cy, d.radius, // 直接使用 Solver 更新后的缓存数据
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
    // A. 准备坐标转换器 (NDCMap)
    // 即使是局部更新，视图中心也可能微调，每一帧刷新一次 map 保证一致性
    double half_w = view.screen_width * 0.5;
    double half_h = view.screen_height * 0.5;
    Vec2 center_world = screen_to_world_inline({half_w, half_h}, view.world_origin, view.wppx, view.wppy);

    NDCMap ndc_map;
    ndc_map.center_x = center_world.x;
    ndc_map.center_y = center_world.y;
    ndc_map.scale_x = 2.0 / (view.screen_width * view.wppx);
    ndc_map.scale_y = 2.0 / (view.screen_height * view.wppy);

    // B. 初始化并发队列与收集器
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(2048); // 限制容量以控制背压
    ResultCollector collector(out_points, node_pool, is_global_update);

    // C. 执行计算调度
    if (is_global_update) {
        // --- 全局模式：严格批次串行，批内并行 ---

        // 1. 分类整理 ID
        std::vector<uint32_t> batch_geom, batch_explicit, batch_parametric, batch_implicit;
        for (uint32_t id : draw_order) {
            auto type = node_pool[id].render_type;
            if (type == GeoNode::RenderType::Point || type == GeoNode::RenderType::Line)
                batch_geom.push_back(id);
            else if (type == GeoNode::RenderType::Explicit)
                batch_explicit.push_back(id);
            else if (type == GeoNode::RenderType::Parametric)
                batch_parametric.push_back(id);
            else if (type == GeoNode::RenderType::Implicit || type == GeoNode::RenderType::Circle)
                batch_implicit.push_back(id);
        }

        // 2. 依次运行批次（阻塞刷新）
        auto ExecuteBatch = [&](const std::vector<uint32_t>& batch) {
            if (batch.empty()) return;

            // 阶段内全员并行
            oneapi::tbb::parallel_for_each(batch.begin(), batch.end(), [&](uint32_t id) {
                DispatchNodeTask(node_pool[id], node_pool, view, ndc_map, results_queue);
            });

            // ★★★ 关键阻塞点：该类函数必须全部写回显存，才开始下一类 ★★★
            collector.Flush(results_queue);
        };

        ExecuteBatch(batch_geom);       // 1. 几何
        ExecuteBatch(batch_explicit);   // 2. 显函数
        ExecuteBatch(batch_parametric); // 3. 参数方程
        ExecuteBatch(batch_implicit);   // 4. 隐函数 & 圆

    } else {
        // --- 局部模式：仅针对脏节点增量计算 ---
        if (!dirty_node_ids.empty()) {
            oneapi::tbb::parallel_for_each(dirty_node_ids.begin(), dirty_node_ids.end(), [&](uint32_t id) {
                DispatchNodeTask(node_pool[id], node_pool, view, ndc_map, results_queue);
            });
            // 追加写入末尾
            collector.Flush(results_queue);
        }
    }

    // D. 组装 WebGPU 绘制范围 (FunctionRanges)
    // 无论局部还是全局更新，Ranges 必须每一帧完整重建，以维持画家算法顺序
    out_ranges.clear();
    out_ranges.reserve(draw_order.size());
    for (uint32_t id : draw_order) {
        const GeoNode& node = node_pool[id];
        // 这里的 node.buffer_offset 已经由 collector 刷新为最新物理地址
        out_ranges.push_back({ node.buffer_offset, node.current_point_count });
    }
}