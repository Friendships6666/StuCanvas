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
    double CalculateGridStep(double wpp) {
        double target_world_step = 90.0 * wpp;
        double exponent = std::floor(std::log10(target_world_step));
        double power_of_10 = std::pow(10.0, exponent);
        double fraction = target_world_step / power_of_10;

        if (fraction < 1.5)      return 1.0 * power_of_10;
        if (fraction < 3.5)      return 2.0 * power_of_10;
        if (fraction < 7.5)      return 5.0 * power_of_10;
        return 10.0 * power_of_10;
    }

    /**
     * @brief 极致性能：单次循环生成所有网格线，并区分 Major 和 Axis
     */
    void GenerateCartesianLines(
        std::vector<GridLineData>& buffer,
        std::vector<AxisIntersectionData>* intersection_buffer,
        const ViewState& v,
        uint64_t global_mask,
        double min_w, double max_w, double minor_step, double major_step,
        double ndc_scale, double offset,
        bool horizontal
    ) {
        // 预计算 16.16 增量
        int32_t step_fp = static_cast<int32_t>((minor_step * ndc_scale) * 65536.0);

        // 计算起始线的对齐世界坐标
        double first_w = std::floor(min_w / minor_step) * minor_step;
        int32_t cur_fp = static_cast<int32_t>(((first_w - offset) * ndc_scale) * 65536.0);
        double cur_w = first_w;

        // 终点限制
        int32_t end_fp = static_cast<int32_t>(32767) << 16;
        int32_t start_limit_fp = static_cast<int32_t>(-32767) << 16;

        // 判定阈值（使用 minor_step 的 10% 作为浮点数容差）
        double eps = minor_step * 0.1;

        while (cur_fp <= end_fp) {
            if (cur_fp >= start_limit_fp) {
                int16_t pos = static_cast<int16_t>(cur_fp >> 16);

                // 1. 判定是否为 Axis (坐标接近 0)
                bool is_axis = (std::abs(cur_w) < eps);

                // 2. 判定是否为 Major (坐标是 major_step 的倍数)
                double major_rem = std::abs(std::remainder(cur_w, major_step));
                bool is_major = (major_rem < eps);

                // 只有非轴线才放入网格 buffer（轴线在外部单独绘制以保证最高精度）
                if (!is_axis) {
                    if (horizontal)
                        buffer.push_back({ {-32767, pos}, {32767, pos}});
                    else
                        buffer.push_back({ {pos, -32767}, {pos, 32767}});

                    // 如果是 Major 线且有交点容器，记录交点信息 (受 DISABLE_GRID_NUMBER 控制)
                    if (is_major && intersection_buffer && !(global_mask & DISABLE_GRID_NUMBER)) {
                        Vec2i intersection_pos = horizontal ? v.WorldToClip(0, cur_w) : v.WorldToClip(cur_w, 0);
                        intersection_buffer->push_back({intersection_pos, cur_w});
                    }
                }
            }
            cur_fp += step_fp;
            cur_w += minor_step;
        }
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

    bool viewport_changed = graph.detect_view_change();
    if (viewport_changed) {
        out_points.clear();
        for (auto& n : graph.node_pool) {
            graph.mark_as_seed(n.id);
        }
    }


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
                if (node.error_status = GeoErrorStatus::VALID) {


                    bool is_logic_dirty = std::ranges::binary_search(affected_ids, node.id);
                    bool is_heuristic = node.state_mask & IS_GRAPHICAL;

                    if (is_logic_dirty || (viewport_changed && is_heuristic)) {
                        if (is_heuristic) {
                            for (uint32_t pid : node.parents) {
                                GeoNode& parent = graph.get_node_by_id(pid);
                                if (parent.current_point_count == 0 && parent.render_task) {
                                    parent.render_task(parent, graph, view, node_queue);
                                    flush_node_results(parent, node_queue, graph);
                                }
                            }
                        }

                        bool parents_ok = true;
                        for (uint32_t pid : node.parents) {
                            if (!GeoErrorStatus::ok(graph.get_node_by_id(pid).error_status)) {
                                parents_ok = false; break;
                            }
                        }

                        if (parents_ok) {
                            node.solver(node, graph);
                        } else {
                            node.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
                        }
                    }

                    if (GeoErrorStatus::ok(node.error_status)) {
                        bool child_needs = check_children_need_plot(node, graph);

                        if ((node.state_mask & IS_VISIBLE) || child_needs) {
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
                        node.state_mask |= IS_DIRTY;
                    }

                    curr_id = node.next_in_bucket;
                }
                mask &= ~(1ULL << r_offset);
            }
        }
    }

    out_meta.clear();
    for (const auto& node : graph.node_pool) {
        if (GeoErrorStatus::ok(node.error_status) && (node.state_mask & IS_VISIBLE) && node.current_point_count > 0) {
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
    if (graph.detect_view_change()) {
        RefreshGridSystem(graph);
    }

    graph.sync_view_snapshot();
    graph.m_pending_seeds.clear();
    std::ranges::fill(graph.m_dirty_mask, 0);
}