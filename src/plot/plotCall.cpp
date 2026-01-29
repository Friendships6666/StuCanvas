// --- src/plot/plotCall.cpp ---
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/graph/GeoSolver.h"
#include "../../pch.h"
#include <../include/grids/grids.h>
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
        if (graph.global_state_mask & DISABLE_LABELS) {
            return;
        }

        const auto& points = graph.final_points_buffer;

        // 2. 快速遍历所有活跃节点
        for (const auto& node : graph.node_pool) {

            if (node.current_point_count == 0 || !node.config.label.show_label) {
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

    }
 // 匿名空间结束

/**
 * @brief 缓冲区压缩 (GC)
 */
void CompactBuffer(GeometryGraph& graph) {
    auto& buffer = graph.final_points_buffer; // 类型为 std::vector<PointData>

    // 修正 1: 这里统一使用 PointData
    std::vector<PointData> new_buffer;
    new_buffer.reserve(buffer.capacity());

    for (auto& node : graph.node_pool) {
        if (node.current_point_count > 0) {
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

namespace {
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



        // Mark old points as garbage before assigning new offset and count
        if (node.current_point_count > 0) {
            for (uint32_t i = 0; i < node.current_point_count; ++i) {
                out_points[node.buffer_offset + i].x = graph.view.MAGIC_CLIP_X;
            }
        }

        node.buffer_offset = static_cast<uint32_t>(out_points.size());
        node.current_point_count = new_count;
        out_points.insert(out_points.end(), all_new_points.begin(), all_new_points.end());
    }

    bool check_children_need_plot(const GeoNode& node, const GeometryGraph& graph) {
        for (uint32_t cid : node.children) {
            const auto& child = graph.get_node_by_id(cid);
            if (child.state_mask & (IS_GRAPHICAL | IS_VISIBLE)) {
                return true;
            }
        }
        return false;
    }

}

void RefreshPolarGrid(GeometryGraph& graph) {
    // 极坐标通常生成放射状直线和同心圆（同心圆由多段线组成）
    // 目前按架构清空即可
}

void RefreshCartesianGrid(GeometryGraph& graph) {
    const auto& v = graph.view;
    auto& buffer = graph.final_grid_buffer;
    auto* intersection_buffer = &graph.final_axis_intersection_buffer;

    double major_step = CalculateGridStep(v.wpp);
    double minor_step = major_step / 5.0;

    Vec2 w_min = v.ScreenToWorld(0, v.screen_height);
    Vec2 w_max = v.ScreenToWorld(v.screen_width, 0);

    // 绘制横线 (传入 global_state_mask)
    GenerateCartesianLines(buffer, intersection_buffer, v, graph.global_state_mask,
                           std::min(w_min.y, w_max.y), std::max(w_min.y, w_max.y),
                           minor_step, major_step, v.ndc_scale_y, v.offset_y, true);

    // 绘制纵线 (传入 global_state_mask)
    GenerateCartesianLines(buffer, intersection_buffer, v, graph.global_state_mask,
                           std::min(w_min.x, w_max.x), std::max(w_min.x, w_max.x),
                           minor_step, major_step, v.ndc_scale_x, v.offset_x, false);

    // 轴线始终单独精准计算 (pos=0 的位置)
    Vec2i axis = v.WorldToClip(0, 0);
    buffer.push_back({ {-32767, axis.y}, {32767, axis.y}});
    buffer.push_back({ {axis.x, -32767}, {axis.x, 32767}});
}

void RefreshGridSystem(GeometryGraph& graph) {
    graph.final_grid_buffer.clear();
    graph.final_axis_intersection_buffer.clear();

    // 1. 全局开关检查 (Bit 1)
    if (graph.global_state_mask & DISABLE_GRID) {
        return;
    }

    // 2. 根据枚举分发逻辑
    switch (graph.grid_type) {
        case GridSystemType::CARTESIAN:
            RefreshCartesianGrid(graph);
            break;
        case GridSystemType::POLAR:
            RefreshPolarGrid(graph);
            break;
    }
}



void calculate_points_core(GeometryGraph& graph) {
    if (!graph.is_healthy()) return;

    auto& out_points = graph.final_points_buffer;
    auto& out_meta = graph.final_meta_buffer;
    const ViewState& view = graph.view;

    // 1. 视口变化全局处理
    bool viewport_changed = graph.detect_view_change();
    if (viewport_changed) {
        out_points.clear();
        out_meta.clear();
        // 视口变化时，所有节点都要检查是否需要重绘
        for (auto& n : graph.node_pool) {
            graph.mark_as_seed(n.id);
        }
    }

    // 2. 运行 FastScan 进行脏位和图解属性传染
    // 此时 affected_ids 包含所有 logic_dirty 的 ID
    std::vector<uint32_t> affected_ids = graph.FastScan();

    // 准备并发队列
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>> node_queue;
    node_queue.set_capacity(8192);

    // 临时任务容器 (复用内存以优化性能)
    std::vector<GeoNode*> solver_tasks;
    std::vector<GeoNode*> plot_tasks;
    solver_tasks.reserve(1024);
    plot_tasks.reserve(1024);

    // 3. 拓扑 Rank 大循环/**/
    for (size_t w = 0; w < graph.active_ranks_mask.size(); ++w) {
        uint64_t mask = graph.active_ranks_mask[w];
        while (mask > 0) {
            uint32_t r_offset = find_first_set_bit_local(mask);
            auto r = static_cast<uint32_t>(w * 64 + r_offset);
            mask &= ~(1ULL << r_offset); // 移向下一个 Rank

            solver_tasks.clear();
            plot_tasks.clear();

            // --- 阶段 A: 任务收集 ---
            uint32_t curr_id = graph.buckets_all_heads[r];
            while (curr_id != NULL_ID) {
                GeoNode& node = graph.get_node_by_id(curr_id);

                // 跳过无效节点 (FastScan 已经处理了 link 错误和 parent 错误)
                if (GeoErrorStatus::ok(node.error_status)) {
                    bool is_dirty = std::ranges::binary_search(affected_ids, node.id);
                    bool children_has_graphic = std::ranges::any_of(node.children, [&](const auto& child) {
                        return graph.get_node_by_id(child).state_mask & (IS_GRAPHICAL | IS_GRAPHICAL_INFECTED);
                    });


                    if (is_dirty) {
                        solver_tasks.push_back(&node);

                        if ((node.state_mask & IS_VISIBLE) || children_has_graphic) {
                            plot_tasks.push_back(&node);
                        }
                    }


                }
                curr_id = node.next_in_bucket;
            }

            if (solver_tasks.empty() && plot_tasks.empty()) continue;

            // --- 阶段 B: 执行 Solver ---
            if (!solver_tasks.empty()) {
                if (solver_tasks.size() > 500) {
                    oneapi::tbb::parallel_for(size_t(0), solver_tasks.size(), [&](size_t i) {
                        GeoNode* n = solver_tasks[i];
                        n->solver(*n, graph);
                    });
                } else {
                    for (auto* n : solver_tasks) {
                        n->solver(*n, graph);
                    }
                }

                // 核心：无效状态传染。如果有节点计算失败，立即标记子孙
                for (auto* n : solver_tasks) {
                    if (!GeoErrorStatus::ok(n->error_status)) {
                        // 级联标记无效（利用拓扑序，只需标记直接孩子，后面的 Rank 会继续传）
                        for (uint32_t cid : n->children) {
                            graph.get_node_by_id(cid).error_status = GeoErrorStatus::ERR_PARENT_INVALID;
                        }
                    }
                }
            }

            // --- 阶段 C: 执行 Plot ---
            // 过滤掉因为刚刚 Solver 失败而变得无效的 Plot 任务
            auto it = std::ranges::remove_if(plot_tasks, [](GeoNode* n) {
                return !GeoErrorStatus::ok(n->error_status);
            }).begin();
            plot_tasks.erase(it, plot_tasks.end());

            if (!plot_tasks.empty()) {
                if (plot_tasks.size() > 100) {
                    oneapi::tbb::parallel_for(size_t(0), plot_tasks.size(), [&](size_t i) {
                        GeoNode* n = plot_tasks[i];
                        if (n->render_task) {
                            n->render_task(*n, graph, view, node_queue);
                        }
                    });
                } else {
                    for (auto* n : plot_tasks) {
                        if (n->render_task) {
                            n->render_task(*n, graph, view, node_queue);
                        }
                    }
                }

                // 串行收割数据入主缓冲区（保持线程安全和内存连续性）
                // 注意：flush_node_results 内部会处理 node_queue 的 try_pop
                for (auto* n : plot_tasks) {
                    flush_node_results(*n, node_queue, graph);
                }
            }
        }
    }

    // 4. 后处理与元数据构建
    out_meta.clear();
    for (auto& node : graph.node_pool) {


        out_meta.push_back({
            node.buffer_offset,
            node.current_point_count,
            node.id,
            node.type,
            node.config,
            node.state_mask
        });


        // 5. 清理临时掩码：图解传染标记
        node.state_mask &= ~(IS_GRAPHICAL_INFECTED);
    }

    // 6. 辅助系统刷新
    RefreshAllLabels(graph);
    if (viewport_changed) {
        RefreshGridSystem(graph);
    }

    // 7. 同步状态，准备下一帧
    graph.sync_view_snapshot();
    graph.m_pending_seeds.clear();
    std::ranges::fill(graph.m_dirty_mask, 0);

    if (graph.preview_func != nullptr && graph.preview_type != GeoType::UNKNOWN) {
        graph.preview_points.clear();
        graph.preview_func(graph);
    }
}