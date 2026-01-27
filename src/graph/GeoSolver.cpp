// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
    /**
     * @brief 快速获取指定父节点结果引用
     */
    FORCE_INLINE const ComputedResult& get_parent_res(const GeometryGraph& graph, uint32_t pid) {
        return graph.get_node_by_id(pid).result;
    }

    /**
     * @brief 数学结果安全性检查
     */
    FORCE_INLINE bool validate_math(GeoNode& self, double val) {
        if (!std::isfinite(val)) {
            self.error_status = GeoErrorStatus::ERR_OVERFLOW;
            return false;
        }
        return true;
    }

    /**
     * @brief [核心辅助] 解算单个逻辑通道
     * 变量引用永远读取父节点的物理世界坐标
     */
    double SolveChannel(GeoNode& self, int idx, const GeometryGraph& graph) {
        auto& ch = self.channels[idx];
        if (ch.bytecode_len == 0) return ch.value;

        for (uint32_t k = 0; k < ch.patch_len; ++k) {
            auto& p = ch.patch_ptr[k];
            const auto& parent_res = get_parent_res(graph, p.dependency_ids[0]);
            double val = 0.0;

            switch (p.func_type) {
                case CAS::Parser::CustomFunctionType::NONE:
                    val = parent_res.s0;
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_X:
                    val = parent_res.x;
                    break;
                case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_Y:
                    val = parent_res.y;
                    break;
                case CAS::Parser::CustomFunctionType::LENGTH: {
                    const auto& r2 = get_parent_res(graph, p.dependency_ids[1]);
                    val = std::hypot(parent_res.x - r2.x, parent_res.y - r2.y);
                    break;
                }
                default: break;
            }
            ch.bytecode_ptr[p.rpn_index].type = RPNTokenType::PUSH_CONST;
            ch.bytecode_ptr[p.rpn_index].value = val;
        }

        ch.value = evaluate_rpn<double>(ch.bytecode_ptr, ch.bytecode_len);
        return ch.value;
    }
}

// =========================================================
// 1. RPN 通用解算器 (标量)
// =========================================================
void Solver_ScalarRPN(GeoNode& self, GeometryGraph& graph) {
    auto& res = self.result;
    for (int i = 0; i < 4; ++i) {
        if (self.channels[i].bytecode_len == 0 && i > 0) continue;
        double abs_val = SolveChannel(self, i, graph);
        if (!std::isfinite(abs_val)) {
            self.error_status = GeoErrorStatus::ERR_OVERFLOW;
            return;
        }
        res._raw_data[i] = abs_val;
    }
    self.error_status = GeoErrorStatus::VALID;
}

// =========================================================
// 2. 标准点求解器 (计算世界坐标 + 视口相对坐标)
// =========================================================
void Solver_StandardPoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    double abs_x = SolveChannel(self, 0, graph);
    double abs_y = SolveChannel(self, 1, graph);

    if (std::isfinite(abs_x) && std::isfinite(abs_y)) {
        self.result.x = abs_x;
        self.result.y = abs_y;

        // 维护视口相对坐标 (Floating Origin)
        self.result.x_view = abs_x - v.offset_x;
        self.result.y_view = abs_y - v.offset_y;
        self.error_status = GeoErrorStatus::VALID;
    } else {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
    }
}

// =========================================================
// 3. 中点求解器
// =========================================================
void Solver_Midpoint(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    double mx = (p1.x + p2.x) * 0.5;
    double my = (p1.y + p2.y) * 0.5;

    if (std::isfinite(mx) && std::isfinite(my)) {
        self.result.x = mx;
        self.result.y = my;
        self.result.x_view = mx - v.offset_x;
        self.result.y_view = my - v.offset_y;
        self.error_status = GeoErrorStatus::VALID;
    } else {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
    }
}

// =========================================================
// 4. 约束点求解器 (极致优化的 int16 空间吸附搜索)
// =========================================================
void Solver_ConstrainedPoint(GeoNode& self, GeometryGraph& graph) {
    double anchor_w_x = std::isfinite(self.channels[0].value) ? self.channels[0].value : SolveChannel(self, 0, graph);
    double anchor_w_y = std::isfinite(self.channels[1].value) ? self.channels[1].value : SolveChannel(self, 1, graph);

    // 2. 统一检查
    if (!std::isfinite(anchor_w_x) || !std::isfinite(anchor_w_y)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }
    const auto& v = graph.view;
    const uint32_t target_id = self.target_ids[0];
    const auto& target = graph.get_node_by_id(target_id);

    if (target.current_point_count == 0) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
        return;
    }



    // 2. 将锚点投影到 int16 压缩 Clip 空间
    Vec2i anchor_clip = v.WorldToClip(anchor_w_x, anchor_w_y);

    // 3. 在 Ring Buffer 中搜索最近的 int16 采样点
    int32_t min_dist_sq = std::numeric_limits<int32_t>::max();
    int16_t best_cx = anchor_clip.x;
    int16_t best_cy = anchor_clip.y;

    const auto& pts = graph.final_points_buffer; // std::vector<PointData> (int16_t x, y)
    uint32_t start = target.buffer_offset;
    uint32_t end = start + target.current_point_count;

    for (uint32_t i = start; i < end; ++i) {
        const auto& pt = pts[i];




        // 整数减法
        int32_t dx = static_cast<int32_t>(pt.x) - anchor_clip.x;
        int32_t dy = static_cast<int32_t>(pt.y) - anchor_clip.y;
        // 整数平方累加，防止 float 转换开销
        uint64_t d2 = static_cast<uint64_t>(dx * dx) + static_cast<uint64_t>(dy * dy);

        if (d2 < min_dist_sq) {
            min_dist_sq = d2;
            best_cx = pt.x;
            best_cy = pt.y;
        }
    }

    Vec2 best_world = v.ClipToWorld(best_cx, best_cy);

    self.result.x = best_world.x;
    self.result.y = best_world.y;
    self.result.x_view = best_world.x - v.offset_x;
    self.result.y_view = best_world.y - v.offset_y;
    self.channels[0].value = best_world.x;
    self.channels[1].value = best_world.y;

    self.error_status = GeoErrorStatus::VALID;
}

// =========================================================
// 5. 标准线段求解器
// =========================================================
void Solver_StandardLine(GeoNode& self, GeometryGraph& graph) {
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);

    self.result.x1 = p1.x; self.result.y1 = p1.y;
    self.result.x2 = p2.x; self.result.y2 = p2.y;

    double dx = p1.x - p2.x;
    double dy = p1.y - p2.y;
    if ((dx * dx + dy * dy) < 1e-15) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
    } else {
        self.error_status = GeoErrorStatus::VALID;
    }
}

// =========================================================
// 6. 图解交点求解器 (高性能线性探测哈希表)
// =========================================================
namespace {
    // 打包/解包 PointData 保持高效位操作
    // 注意：PointData 是 {int16_t x, int16_t y}
    inline uint32_t pack_point_data(PointData p) {
        return (static_cast<uint32_t>(static_cast<uint16_t>(p.x)) << 16) |
               static_cast<uint16_t>(p.y);
    }

    inline PointData unpack_point_data(uint32_t packed) {
        return { static_cast<int16_t>(packed >> 16),
                 static_cast<int16_t>(packed & 0xFFFF) };
    }

    // 极致优化的线性探测哈希表
    struct FlatIntersectionMap {
        static constexpr uint32_t EMPTY_KEY = 0xFFFFFFFF; // 使用一个不可能的Packed值作为空标记
        struct Entry {
            uint32_t key = EMPTY_KEY;
            uint16_t count = 0; // 投票计数
        };

        std::vector<Entry> table;
        uint32_t mask;
        uint32_t current_size = 0;

        FlatIntersectionMap(size_t expected_elements) {
            size_t capacity = 1;
            // 保持低负载因子 (0.5) 以减少线性探测冲突
            while (capacity < expected_elements * 2) capacity <<= 1;
            table.resize(capacity);
            mask = capacity - 1;
        }

        // 核心插入逻辑：每个对象只贡献一票
        inline void vote(uint32_t key, uint16_t current_obj_idx) {
            uint32_t h = key & mask; // Identity Hash
            while (table[h].key != EMPTY_KEY) {
                if (table[h].key == key) {
                    // 只有当投票者不同时才增加计数
                    if (table[h].count == current_obj_idx) { // 如果已经用当前对象ID投过票
                        table[h].count++; // 递增，确保多个不同对象投票时正确计数
                    }
                    return;
                }
                h = (h + 1) & mask;
            }
            // 第一次插入
            if (current_obj_idx == 0) { // 只有第一个对象可以初始化新条目
                table[h].key = key;
                table[h].count = 1;
                current_size++;
            }
        }
    };
}

void Solver_GraphicalIntersectionPoint(GeoNode& self, GeometryGraph& graph) {
    double anchor_w_x = std::isfinite(self.channels[0].value) ? self.channels[0].value : SolveChannel(self, 0, graph);
    double anchor_w_y = std::isfinite(self.channels[1].value) ? self.channels[1].value : SolveChannel(self, 1, graph);

    // 2. 统一检查
    if (!std::isfinite(anchor_w_x) || !std::isfinite(anchor_w_y)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }



    // 2. 将锚点世界坐标转换为 Clip 坐标
    const auto& v = graph.view;
    Vec2i anchor_clip = v.WorldToClip(anchor_w_x, anchor_w_y);
    PointData anchor_pt_data = {anchor_clip.x, anchor_clip.y};

    // 收集所有父节点的点数据和偏移量
    std::vector<PointData> all_parent_points;
    std::vector<size_t> parent_offsets;
    uint16_t current_obj_idx = 0; // 用于FlatIntersectionMap的投票计数

    for (uint32_t parent_id : self.parents) {
        if (!graph.is_alive(parent_id)) {
            self.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
            return;
        }
        const auto& parent_node = graph.get_node_by_id(parent_id);
        if (parent_node.current_point_count == 0) {
            // 如果任何一个父节点没有点，则无法求交
            self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
            return;
        }
        // 记录当前父节点点数据在 all_parent_points 中的起始偏移
        parent_offsets.push_back(all_parent_points.size());
        
        uint32_t start_idx = parent_node.buffer_offset;
        uint32_t end_idx = start_idx + parent_node.current_point_count;
        for (uint32_t i = start_idx; i < end_idx; ++i) {
            all_parent_points.push_back(graph.final_points_buffer[i]);
        }
    }

    if (self.parents.empty()) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
        return;
    }

    // 3. 使用优化的哈希表算法寻找交点
    // 预估哈希表大小，以第一个父节点的点数作为基准，或者一个合理的小值
    size_t estimated_points = (parent_offsets.empty() || all_parent_points.empty()) ? 10 : 
                              (parent_offsets.size() > 1 ? parent_offsets[1] : all_parent_points.size()) - parent_offsets[0];
    
    FlatIntersectionMap hit_map(estimated_points);

    // 投票阶段
    for (uint16_t i = 0; i < self.parents.size(); ++i) {
        size_t start = parent_offsets[i];
        size_t end = (i + 1 < self.parents.size()) ? parent_offsets[i + 1] : all_parent_points.size();
        //auto segment = std::span(all_parent_points.data() + start, end - start);

        // 为每个段的每个点投票
        for (size_t k = start; k < end; ++k) {
            hit_map.vote(pack_point_data(all_parent_points[k]), i);
        }
    }

    // 距离筛选阶段
    PointData best_point_clip = {0, 0};
    int64_t min_dist_sq = std::numeric_limits<int64_t>::max();
    bool found_intersection = false;

    for (const auto& entry : hit_map.table) {
        // 如果这个Entry是有效的（非EMPTY_KEY），并且投票计数等于父节点数量，说明它是一个交点
        if (entry.key != FlatIntersectionMap::EMPTY_KEY && entry.count == self.parents.size()) {
            PointData p = unpack_point_data(entry.key);
            int64_t dx = static_cast<int64_t>(p.x) - anchor_pt_data.x;
            int64_t dy = static_cast<int64_t>(p.y) - anchor_pt_data.y;
            int64_t d2 = dx * dx + dy * dy;

            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                best_point_clip = p;
                found_intersection = true;
            }
        }
    }

    if (found_intersection) {
        // 4. 将找到的最佳 Clip 坐标逆向转换回世界坐标
        Vec2 best_world = v.ClipToWorld(best_point_clip.x, best_point_clip.y);

        self.result.x = best_world.x;
        self.result.y = best_world.y;
        self.result.x_view = best_world.x - v.offset_x;
        self.result.y_view = best_world.y - v.offset_y;
        self.channels[0].value = best_world.x;
        self.channels[1].value = best_world.y;
        self.error_status = GeoErrorStatus::VALID;
    } else {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT; // 没有找到交点
    }
}