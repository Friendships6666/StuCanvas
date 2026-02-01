// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/ShuntingYard.h"
#include <cmath>
#include <limits>
#include <algorithm>



double SolveChannel(GeoNode& self, int idx, GeometryGraph& graph, bool is_preview) {
    // 1. 根据是否预览选择目标通道
    auto& ch = is_preview ? graph.preview_channels[idx] : self.channels[idx];

    // 2. 如果没有字节码，直接返回缓存的常数值
    if (ch.bytecode_len == 0) return ch.value;

    // 3. 运行时绑定 (Patching)：将依赖的对象属性填入 RPN 指令流中
    for (uint32_t k = 0; k < ch.patch_len; ++k) {
        // 使用智能指针 patches (std::unique_ptr<RuntimeBindingSlot[]>)
        auto& p = ch.patches[k];

        // 获取父节点结果 (dependency_ids[0] 存储了变量对应的逻辑 ID)
        const auto& parent_res = get_parent_res(graph, p.dependency_ids[0]);
        double val = 0.0;

        // 根据绑定类型提取具体数值
        switch (p.func_type) {
            case CAS::Parser::CustomFunctionType::NONE:
                val = parent_res.s0; // 纯标量
                break;
            case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_X:
                val = parent_res.x;  // 提取点/对象的 X
                break;
            case CAS::Parser::CustomFunctionType::EXTRACT_VALUE_Y:
                val = parent_res.y;  // 提取点/对象的 Y
                break;
            case CAS::Parser::CustomFunctionType::LENGTH: {
                // 长度函数依赖两个点
                const auto& r2 = get_parent_res(graph, p.dependency_ids[1]);
                val = std::hypot(parent_res.x - r2.x, parent_res.y - r2.y);
                break;
            }
            default: break;
        }

        // 修改字节码：将对应的 PUSH 操作改为最新的常数值 PUSH_CONST
        // 使用智能指针 bytecode (std::unique_ptr<RPNToken[]>)
        ch.bytecode[p.rpn_index].type = RPNTokenType::PUSH_CONST;
        ch.bytecode[p.rpn_index].value = val;
    }

    // 4. 执行 RPN 虚拟机解算
    // 使用 .get() 传递原始指针给 evaluate_rpn 函数
    ch.value = evaluate_rpn<double>(ch.bytecode.get(), ch.bytecode_len);

    return ch.value;
}


void Solver_ScalarRPN(GeoNode& self, GeometryGraph& graph) {
    auto& res = self.result;
    for (int i = 0; i < 4; ++i) {
        if (self.channels[i].bytecode_len == 0 && i > 0) continue;
        double abs_val = SolveChannel(self, i, graph,false);
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
    double abs_x = SolveChannel(self, 0, graph,false);
    double abs_y = SolveChannel(self, 1, graph,false);

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
void Solver_Circle_1Point_1Radius(GeoNode& self, GeometryGraph& graph) {
    // 1. 核心计算：解算半径通道
    double radius = SolveChannel(self, 0, graph,false);

    // 2. 健壮性检查：半径必须有效且通常应为非负
    if (!std::isfinite(radius)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return; // 早期返回，避免后续无效计算
    }

    if (radius < 0) {
        self.error_status = GeoErrorStatus::ERR_INVALID_RADIUS;
        return; // 早期返回，避免后续无效计算
    }

    // 3. 性能优化：只获取一次父节点引用
    // 假设 parents[0] 是圆心点
    const auto& center_node = graph.get_node_by_id(self.parents[0]);

    // 4. 批量赋值：利用已缓存的引用，这些操作在汇编层面只是简单的内存偏移
    auto& res = self.result;
    const auto& c_res = center_node.result;

    res.cr = radius;
    res.cx = c_res.x;
    res.cy = c_res.y;
    res.cx_view = c_res.x_view;
    res.cy_view = c_res.y_view;

    // 5. 最后更新状态
    self.error_status = GeoErrorStatus::VALID;
}
void Solver_Circle_2Points(GeoNode& self, GeometryGraph& graph) {
    // 1. 一次性获取两个父节点的引用，避免后续重复查表
    // 假设 parents[0] 是圆心，parents[1] 是圆周上的一点
    const auto& p1 = graph.get_node_by_id(self.parents[0]);
    const auto& p2 = graph.get_node_by_id(self.parents[1]);

    const auto& r1 = p1.result;
    const auto& r2 = p2.result;

    // 2. 计算半径：使用相对坐标 (view space) 保证大数值下的精度
    double dx = r1.x_view - r2.x_view;
    double dy = r1.y_view - r2.y_view;

    // 使用 std::hypot 防止 dx*dx + dy*dy 过程中可能出现的中间溢出
    double distance = std::hypot(dx, dy);

    // 3. 安全性校验：检查计算结果是否有效
    if (!std::isfinite(distance)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }

    // 4. 批量同步结果
    // 直接利用已经拿到的 r1 引用，避免再次调用 get_node_by_id
    auto& res = self.result;
    res.cr = distance;
    res.cx = r1.x;
    res.cy = r1.y;
    res.cx_view = r1.x_view;
    res.cy_view = r1.y_view;

    // 5. 更新状态
    self.error_status = GeoErrorStatus::VALID;
}

void Solver_Circle_3Points(GeoNode& self, GeometryGraph& graph) {
    // 1. 获取三点父节点的结果
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);
    const auto& p3 = get_parent_res(graph, self.parents[2]);

    // 使用相对坐标 (x_view, y_view) 防止大数值下的精度抖动
    double x1 = p1.x_view, y1 = p1.y_view;
    double x2 = p2.x_view, y2 = p2.y_view;
    double x3 = p3.x_view, y3 = p3.y_view;

    // 2. 计算行列式 D (Det)
    // D = 2 * (x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2))
    // 如果 D 为 0，说明三点共线，无法构成圆
    double D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));

    if (std::abs(D) < 1e-12) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT; // 三点共线，无解
        return;
    }

    // 3. 计算各点到原点的距离平方（在相对空间下）
    double s1 = x1 * x1 + y1 * y1;
    double s2 = x2 * x2 + y2 * y2;
    double s3 = x3 * x3 + y3 * y3;

    // 4. 根据公式求解圆心坐标 (cx_view, cy_view)
    double cx_v = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / D;
    double cy_v = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / D;

    // 5. 计算半径 r
    // 使用 std::hypot 比 sqrt(dx*dx + dy*dy) 更能防止中间过程溢出
    double r = std::hypot(cx_v - x1, cy_v - y1);

    // 6. 安全性校验
    if (!std::isfinite(cx_v) || !std::isfinite(cy_v) || !std::isfinite(r)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }

    // 7. 写入结果槽位 (ComputedResult)
    // A. 视口空间结果 (用于渲染和吸附)
    self.result.cx_view = cx_v;
    self.result.cy_view = cy_v;
    self.result.cr = r;

    // B. 还原世界空间结果 (用于拓扑逻辑和持久化)
    const auto& v = graph.view;
    self.result.cx = cx_v + v.offset_x;
    self.result.cy = cy_v + v.offset_y;

    // 8. 更新状态
    self.error_status = GeoErrorStatus::VALID;
}
void Solver_ConstrainedPoint(GeoNode& self, GeometryGraph& graph) {
    double anchor_w_x = std::isfinite(self.channels[0].value) ? self.channels[0].value : SolveChannel(self, 0, graph,false);
    double anchor_w_y = std::isfinite(self.channels[1].value) ? self.channels[1].value : SolveChannel(self, 1, graph,false);

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


    self.error_status = GeoErrorStatus::VALID;

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
    double anchor_w_x = std::isfinite(self.channels[0].value) ? self.channels[0].value : SolveChannel(self, 0, graph,false);
    double anchor_w_y = std::isfinite(self.channels[1].value) ? self.channels[1].value : SolveChannel(self, 1, graph,false);

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


void Solver_Arc_2Points_1Radius(GeoNode& self, GeometryGraph& graph) {
    // 1. 获取输入数据
    const auto& v = graph.view;
    const auto& p1 = get_parent_res(graph, self.parents[0]); // 起点 P1
    const auto& p2 = get_parent_res(graph, self.parents[1]); // 终点 P2

    // 解算半径（从通道 0 获取用户输入的公式结果）
    double r_input = SolveChannel(self, 0, graph, false);

    // 2. 基础健壮性检查
    if (!std::isfinite(r_input)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }

    // 计算两点间距 L
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double L_sq = dx * dx + dy * dy;
    double L = std::sqrt(L_sq);

    // 几何检查：半径必须大于等于两点距离的一半
    if (std::abs(r_input) < L / 2.0) {
        self.error_status = GeoErrorStatus::ERR_INVALID_RADIUS;
        return;
    }

    // 3. 计算圆心 C
    // 中点 M
    double mx = (p1.x + p2.x) * 0.5;
    double my = (p1.y + p2.y) * 0.5;

    // 半径绝对值
    double abs_r = std::abs(r_input);

    // 计算中点到圆心的距离 h (勾股定理: h^2 + (L/2)^2 = r^2)
    double h = std::sqrt(abs_r * abs_r - L_sq / 4.0);

    // 计算单位垂直向量 (垂直于 P1->P2)
    // 向量 V = (dx, dy), 垂直向量 U = (-dy, dx)
    double ux = -dy / L;
    double uy = dx / L;

    // 确定圆心坐标
    // 约定：r_input > 0 时圆心偏向一边，r_input < 0 时偏向另一边
    // 这决定了它是优弧还是劣弧（因为 P1->P2 始终逆时针绘制）
    double cx, cy;
    if (r_input > 0) {
        cx = mx + h * ux;
        cy = my + h * uy;
    } else {
        cx = mx - h * ux;
        cy = my - h * uy;
    }

    // 4. 计算极角 (Angles)
    // 利用 atan2 计算 P1 和 P2 相对于圆心的角度
    double t_start = std::atan2(p1.y - cy, p1.x - cx);
    double t_end = std::atan2(p2.y - cy, p2.x - cx);

    // 5. 更新结果槽位
    auto& res = self.result;
    res.cr = abs_r;
    res.cx = cx;
    res.cy = cy;

    // 同步视口空间坐标（用于渲染精度）
    res.cx_view = cx - v.offset_x;
    res.cy_view = cy - v.offset_y;

    // 设置弧度范围
    // 注意：PlotCircle 逻辑中，圆弧是从 t_start 逆时针画到 t_end
    res.t_start = t_start;
    res.t_end = t_end;

    self.error_status = GeoErrorStatus::VALID;
}


void Solver_Arc_3Points(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;

    // 语义约定：
    // parents[0]: 圆心 (Center)
    // parents[1]: 起点 (Start Point) -> 决定半径和起始角度
    // parents[2]: 终点指示器 (End Indicator) -> 决定终止角度的方向

    const auto& p0 = get_parent_res(graph, self.parents[0]);
    const auto& p1 = get_parent_res(graph, self.parents[1]);
    const auto& p2 = get_parent_res(graph, self.parents[2]);

    // 1. 计算半径 (严格以圆心到起点的距离为准)
    double dx1 = p1.x - p0.x;
    double dy1 = p1.y - p0.y;
    double r = std::hypot(dx1, dy1);

    // 健壮性检查：半径不能为 0
    if (!std::isfinite(r) || r < 1e-10) {
        self.error_status = GeoErrorStatus::ERR_INVALID_RADIUS;
        return;
    }

    // 2. 计算终止方向的有效性
    double dx2 = p2.x - p0.x;
    double dy2 = p2.y - p0.y;
    double dist2 = std::hypot(dx2, dy2);

    // 健壮性检查：指示点不能与圆心重合
    if (!std::isfinite(dist2) || dist2 < 1e-10) {
        self.error_status = GeoErrorStatus::ERR_MATH_DOMAIN;
        return;
    }

    // 3. 计算起始和终止弧度
    // atan2 返回 (-PI, PI]，完美处理所有象限
    double t_start = std::atan2(dy1, dx1);
    double t_end = std::atan2(dy2, dx2);

    // 4. 填充结果槽位 (ComputedResult)
    auto& res = self.result;

    // 世界空间数据
    res.cx = p0.x;
    res.cy = p0.y;
    res.cr = r;
    res.t_start = t_start;
    res.t_end = t_end;

    // 视口空间数据 (用于渲染器 PlotCircle 补点，防止大坐标抖动)
    res.cx_view = p0.x_view;
    res.cy_view = p0.y_view;

    // 5. 状态更新
    // 逻辑提示：底层 PlotCircle 的 is_angle_in_arc 会处理跨越 PI 的回绕。
    // 如果 t_start 逆时针到 t_end 的距离小于 PI，则表现为劣弧。
    // 如果距离大于 PI，则表现为优弧。
    // 用户只需调整 P2 的位置，即可覆盖 [0, 2PI] 全范围。
    self.error_status = GeoErrorStatus::VALID;
}