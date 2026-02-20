// --- 文件路径: src/graph/GeoSolver.cpp ---
#include "../../include/graph/GeoSolver.h"
#include "../../include/graph/GeoGraph.h"
#include "../../include/CAS/RPN/SyntaxChecker.h"
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
    self.result.x1_view = p1.x_view; self.result.y1_view = p1.y_view;
    self.result.x2_view = p2.x_view; self.result.y2_view = p2.y_view;


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
    // 1. 获取锚点 (初始位置猜测)
    double anchor_w_x = std::isfinite(self.channels[0].value) ? self.channels[0].value : SolveChannel(self, 0, graph, false);
    double anchor_w_y = std::isfinite(self.channels[1].value) ? self.channels[1].value : SolveChannel(self, 1, graph, false);

    if (!std::isfinite(anchor_w_x) || !std::isfinite(anchor_w_y)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }

    if (self.parents.empty()) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
        return;
    }

    const auto& v = graph.view;
    Vec2i anchor_clip = v.WorldToClip(anchor_w_x, anchor_w_y);

    // 预估哈希表大小（以第一个父节点的点数为基准，防止频繁扩容）
    uint32_t first_parent_count = graph.get_node_by_id(self.parents[0]).current_point_count;
    FlatIntersectionMap hit_map(std::max(first_parent_count, 64u));

    // 2. 核心逻辑：遍历父节点并投票
    // 直接在遍历父节点时投票，省去了存储 all_parent_points 的内存开销
    for (uint16_t i = 0; i < static_cast<uint16_t>(self.parents.size()); ++i) {
        uint32_t parent_id = self.parents[i];
        if (!graph.is_alive(parent_id)) {
            self.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
            return;
        }

        const auto& parent_node = graph.get_node_by_id(parent_id);
        if (parent_node.current_point_count == 0) {
            self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
            return;
        }

        uint32_t start_idx = parent_node.buffer_offset;
        uint32_t end_idx = start_idx + parent_node.current_point_count;

        // 为该对象的每个采样点投票
        for (uint32_t k = start_idx; k < end_idx; ++k) {
            // 使用 i 作为 current_obj_idx
            hit_map.vote(pack_point_data(graph.final_points_buffer[k]), i);
        }
    }

    // 3. 距离筛选阶段：寻找距离锚点最近的“满票”交点
    PointData best_point_clip = {0, 0};
    int64_t min_dist_sq = std::numeric_limits<int64_t>::max();
    bool found_intersection = false;

    for (const auto& entry : hit_map.table) {
        // 票数等于父节点总数，说明所有对象在该像素点均有采样
        if (entry.key != FlatIntersectionMap::EMPTY_KEY && entry.count == self.parents.size()) {
            PointData p = unpack_point_data(entry.key);
            int64_t dx = static_cast<int64_t>(p.x) - anchor_clip.x;
            int64_t dy = static_cast<int64_t>(p.y) - anchor_clip.y;
            int64_t d2 = dx * dx + dy * dy;

            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                best_point_clip = p;
                found_intersection = true;
            }
        }
    }

    if (found_intersection) {
        Vec2 best_world = v.ClipToWorld(best_point_clip.x, best_point_clip.y);
        self.result.x = best_world.x;
        self.result.y = best_world.y;
        self.result.x_view = best_world.x - v.offset_x;
        self.result.y_view = best_world.y - v.offset_y;

        // 更新缓存值
        self.channels[0].value = best_world.x;
        self.channels[1].value = best_world.y;
        self.error_status = GeoErrorStatus::VALID;
    } else {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
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
    res.cx_view = p0.x_view;
    res.cy_view = p0.y_view;
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


void Solver_Arc_3Points_Circumarc(GeoNode& self, GeometryGraph& graph) {
    const auto& v = graph.view;

    // 1. 获取三点父节点的结果
    const auto& p1 = get_parent_res(graph, self.parents[0]);
    const auto& p2 = get_parent_res(graph, self.parents[1]);
    const auto& p3 = get_parent_res(graph, self.parents[2]);

    // 使用视口相对坐标 (View Space) 防止大数值导致的行列式精度崩坏
    double x1 = p1.x_view, y1 = p1.y_view;
    double x2 = p2.x_view, y2 = p2.y_view;
    double x3 = p3.x_view, y3 = p3.y_view;

    // 2. 三点定圆算法计算圆心 (cx, cy)
    // 行列式 D = 2 * (x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2))
    double D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));

    if (std::abs(D) < 1e-12) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT; // 三点共线，无法成弧
        return;
    }

    double s1 = x1 * x1 + y1 * y1;
    double s2 = x2 * x2 + y2 * y2;
    double s3 = x3 * x3 + y3 * y3;

    double cx_v = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / D;
    double cy_v = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / D;
    double r = std::hypot(cx_v - x1, cy_v - y1);

    if (!std::isfinite(cx_v) || !std::isfinite(cy_v) || !std::isfinite(r)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
        return;
    }

    // 3. 计算三点的极角
    double t1 = std::atan2(y1 - cy_v, x1 - cx_v);
    double t2 = std::atan2(y2 - cy_v, x2 - cx_v);
    double t3 = std::atan2(y3 - cy_v, x3 - cx_v);

    // 4. 方向判定与扫描区间映射
    // 我们需要判断 t2 是否在 [t1 -> t3] 的逆时针扫描范围内
    auto is_ccw_between = [](double a, double start, double end) {
        const double TWO_PI = 6.283185307179586;
        auto norm = [&](double angle) {
            double res = std::fmod(angle, TWO_PI);
            if (res < 0) res += TWO_PI;
            return res;
        };
        a = norm(a);
        start = norm(start);
        end = norm(end);

        if (start <= end) return a >= start && a <= end;
        return a >= start || a <= end; // 跨越 PI 边界的情况
    };

    double final_t_start, final_t_end;

    if (is_ccw_between(t2, t1, t3)) {
        // P1 -> P2 -> P3 是逆时针走向
        final_t_start = t1;
        final_t_end = t3;
    } else {
        // P1 -> P2 -> P3 是顺时针走向
        // 在仅支持 CCW 的渲染器中，顺时针的 P1->P3 表现为逆时针的 P3->P1
        final_t_start = t3;
        final_t_end = t1;
    }

    // 5. 写入大一统结果槽位
    auto& res = self.result;
    res.cr = r;
    res.cx_view = cx_v;
    res.cy_view = cy_v;
    res.cx = cx_v + v.offset_x;
    res.cy = cy_v + v.offset_y;
    res.t_start = final_t_start;
    res.t_end = final_t_end;

    self.error_status = GeoErrorStatus::VALID;
}






namespace {
    // 几何约束检查辅助函数 (用于范围过滤)
    bool IsOnLinearSubset(GeoType::Type type, double t) {
        if (type == GeoType::LINE_SEGMENT) return t >= -1e-9 && t <= 1.0 + 1e-9;
        if (type == GeoType::LINE_RAY) return t >= -1e-9;
        return true;
    }

    bool IsOnArcSubset(GeoType::Type type, double px, double py, double cx, double cy, double start, double end) {
        if (type == GeoType::CIRCLE_2POINTS || type == GeoType::CIRCLE_1POINT_1RADIUS ||
            type == GeoType::CIRCLE_3POINTS) {
            return true;
        }

        double angle = std::atan2(py - cy, px - cx);

        const double TWO_PI = 6.283185307179586;
        auto norm = [&](double a) {
            double r = std::fmod(a, TWO_PI);
            if (r < 0) r += TWO_PI;
            return r;
        };

        double a_norm = norm(angle);
        double s_norm = norm(start);
        double e_norm = norm(end);

        if (s_norm <= e_norm) {
            return a_norm >= s_norm - 1e-5 && a_norm <= e_norm + 1e-5;
        }
        return a_norm >= s_norm - 1e-5 || a_norm <= e_norm + 1e-5;
    }

    struct IntersectionSolution {
        double x, y;   // View Space 坐标
        int root_idx;  // 0 或 1，对应方程的两个根（拓扑指纹）
    };
}

void Solver_Intersection(GeoNode& self, GeometryGraph& graph) {
    if (self.target_ids.size() < 2) {
        self.error_status = GeoErrorStatus::ERR_ID_NOT_FOUND;
        return;
    }

    const auto& n1 = graph.get_node_by_id(self.target_ids[0]);
    const auto& n2 = graph.get_node_by_id(self.target_ids[1]);

    if (!GeoErrorStatus::ok(n1.error_status) || !GeoErrorStatus::ok(n2.error_status)) {
        self.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
        return;
    }

    std::vector<IntersectionSolution> valid_solutions;

    bool is_line1 = GeoType::is_line(n1.type);
    bool is_line2 = GeoType::is_line(n2.type);
    bool is_circ1 = GeoType::is_circle(n1.type);
    bool is_circ2 = GeoType::is_circle(n2.type);

    // =================================================
    // 计算核心 (View Space)
    // =================================================

    // Case 1: 线 - 线
    if (is_line1 && is_line2) {
        double x1 = n1.result.x1_view, y1 = n1.result.y1_view;
        double x2 = n1.result.x2_view, y2 = n1.result.y2_view;
        double x3 = n2.result.x1_view, y3 = n2.result.y1_view;
        double x4 = n2.result.x2_view, y4 = n2.result.y2_view;

        double denom = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);

        if (std::abs(denom) > 1e-12) {
            double ua = ((x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3)) / denom;
            double ub = ((x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3)) / denom;

            if (IsOnLinearSubset(n1.type, ua) && IsOnLinearSubset(n2.type, ub)) {
                valid_solutions.push_back({x1 + ua * (x2 - x1), y1 + ua * (y2 - y1), 0});
            }
        }
    }
    // Case 2: 线 - 圆
    else if ((is_line1 && is_circ2) || (is_circ1 && is_line2)) {
        const auto& L_node = is_line1 ? n1 : n2;
        const auto& C_node = is_line1 ? n2 : n1;

        double lx1 = L_node.result.x1_view, ly1 = L_node.result.y1_view;
        double lx2 = L_node.result.x2_view, ly2 = L_node.result.y2_view;
        double cx = C_node.result.cx_view;
        double cy = C_node.result.cy_view;
        double r = C_node.result.cr;

        double dx = lx2 - lx1;
        double dy = ly2 - ly1;
        double fx = lx1 - cx;
        double fy = ly1 - cy;

        double a = dx * dx + dy * dy;
        double b = 2 * (fx * dx + fy * dy);
        double c = (fx * fx + fy * fy) - r * r;

        double delta = b * b - 4 * a * c;

        if (delta >= -1e-9 && a > 1e-12) {
            double sqrt_delta = std::sqrt(std::max(0.0, delta));

            double t_candidates[2];
            t_candidates[0] = (-b - sqrt_delta) / (2 * a);
            t_candidates[1] = (-b + sqrt_delta) / (2 * a);

            for (int i = 0; i < 2; ++i) {
                // 范围检查
                if (!IsOnLinearSubset(L_node.type, t_candidates[i])) continue;

                double px = lx1 + t_candidates[i] * dx;
                double py = ly1 + t_candidates[i] * dy;

                if (IsOnArcSubset(C_node.type, px, py, cx, cy, C_node.result.t_start, C_node.result.t_end)) {
                    valid_solutions.push_back({px, py, i}); // i 就是 root_idx (0 或 1)
                }
            }
        }
    }
    // Case 3: 圆 - 圆
    else if (is_circ1 && is_circ2) {
        double c1x = n1.result.cx_view, c1y = n1.result.cy_view, r1 = n1.result.cr;
        double c2x = n2.result.cx_view, c2y = n2.result.cy_view, r2 = n2.result.cr;

        double d2 = (c1x - c2x) * (c1x - c2x) + (c1y - c2y) * (c1y - c2y);
        double d = std::sqrt(d2);

        if (d > 1e-9 && d <= r1 + r2 + 1e-9 && d >= std::abs(r1 - r2) - 1e-9) {
            double a = (r1 * r1 - r2 * r2 + d2) / (2 * d);
            double h = std::sqrt(std::max(0.0, r1 * r1 - a * a));

            double x2 = c1x + a * (c2x - c1x) / d;
            double y2 = c1y + a * (c2y - c1y) / d;

            // Root 0: "右手"侧解
            // Root 1: "左手"侧解
            double pts[2][2] = {
                {x2 + h * (c2y - c1y) / d, y2 - h * (c2x - c1x) / d},
                {x2 - h * (c2y - c1y) / d, y2 + h * (c2x - c1x) / d}
            };

            for (int i = 0; i < 2; ++i) {
                double px = pts[i][0];
                double py = pts[i][1];

                if (IsOnArcSubset(n1.type, px, py, c1x, c1y, n1.result.t_start, n1.result.t_end) &&
                    IsOnArcSubset(n2.type, px, py, c2x, c2y, n2.result.t_start, n2.result.t_end)) {
                    valid_solutions.push_back({px, py, i}); // i 就是 root_idx (0 或 1)
                }
            }
        }
    }

    // =================================================
    // 决策逻辑：Mask锁定 > 距离猜测
    // =================================================
    if (valid_solutions.empty()) {
        self.error_status = GeoErrorStatus::ERR_EMPTY_RESULT;
        self.state_mask &= ~(INTERSECTION_0 | INTERSECTION_1 | INTERSECTION_3 | INTERSECTION_4);
        return;
    }

    int best_index = -1;

    // 1. 检查 state_mask 中的锁定状态
    int locked_root_idx = -1;
    if (self.state_mask & INTERSECTION_0) locked_root_idx = 0;
    else if (self.state_mask & INTERSECTION_1) locked_root_idx = 1;

    if (locked_root_idx != -1) {
        for (int i = 0; i < valid_solutions.size(); ++i) {
            if (valid_solutions[i].root_idx == locked_root_idx) {
                best_index = i;
                break;
            }
        }
    }

    // 2. 如果未锁定或锁定失效，使用初始猜测值 (Nearest Neighbor)
    if (best_index == -1) {
        double guess_x = std::isnan(self.channels[0].value) ? SolveChannel(self, 0, graph, false) : self.channels[0].value;
        double guess_y = std::isnan(self.channels[1].value) ? SolveChannel(self, 1, graph, false) : self.channels[1].value;

        if (std::isfinite(guess_x) && std::isfinite(guess_y)) {
            double min_dist = std::numeric_limits<double>::max();
            const auto& v = graph.view;

            for (int i = 0; i < valid_solutions.size(); ++i) {
                double wx = valid_solutions[i].x + v.offset_x;
                double wy = valid_solutions[i].y + v.offset_y;
                double d2 = (wx - guess_x) * (wx - guess_x) + (wy - guess_y) * (wy - guess_y);

                if (d2 < min_dist) {
                    min_dist = d2;
                    best_index = i;
                }
            }
        } else {
            best_index = 0;
        }
    }

    // =================================================
    // 结果回写与状态锁定
    // =================================================
    const auto& final_sol = valid_solutions[best_index];
    const auto& v = graph.view;

    self.result.x_view = final_sol.x;
    self.result.y_view = final_sol.y;
    self.result.x = final_sol.x + v.offset_x;
    self.result.y = final_sol.y + v.offset_y;

    // 更新锁定状态到 state_mask
    self.state_mask &= ~(INTERSECTION_0 | INTERSECTION_1 | INTERSECTION_3 | INTERSECTION_4);
    if (final_sol.root_idx == 0) {
        self.state_mask |= INTERSECTION_0;
    } else if (final_sol.root_idx == 1) {
        self.state_mask |= INTERSECTION_1;
    }

    // 更新通道缓存
    self.channels[0].value = self.result.x;
    self.channels[1].value = self.result.y;

    self.error_status = GeoErrorStatus::VALID;
}





namespace {
    const double TWO_PI = 6.283185307179586;

    // 辅助：归一化角度到 [0, 2PI)
    inline double norm_angle(double a) {
        double res = std::fmod(a, TWO_PI);
        if (res < 0) res += TWO_PI;
        return res;
    }

    // 判定角度是否在圆弧扫描范围内
    inline bool is_ang_in_arc(double a, double s, double e) {
        a = norm_angle(a);
        s = norm_angle(s);
        e = norm_angle(e);
        if (s <= e) return a >= s && a <= e;
        return a >= s || a <= e;
    }

    // 计算两个角度之间的最短弧长距离（用于吸附最近端点）
    inline double ang_dist(double a, double b) {
        double d = std::abs(norm_angle(a) - norm_angle(b));
        return (d > 3.141592653589793) ? TWO_PI - d : d;
    }
}

void Solver_ConstrainedPoint_Analytic(GeoNode& self, GeometryGraph& graph) {
    const uint32_t target_id = self.target_ids[0];
    const auto& target = graph.get_node_by_id(target_id);
    const auto& v = graph.view;

    // 1. 获取锚点 (如果是第一次运行，计算公式；否则使用保存的参数 t)
    double anchor_x = 0, anchor_y = 0;
    bool is_first_run =! std::isfinite(self.result.t);

    if (is_first_run) {
        anchor_x = SolveChannel(self, 0, graph, false);
        anchor_y = SolveChannel(self, 1, graph, false);
        if (!std::isfinite(anchor_x) || !std::isfinite(anchor_y)) {
            self.error_status = GeoErrorStatus::ERR_OVERFLOW;
            return;
        }
    }

    // 2. 核心计算
    double final_x, final_y, final_t;

    if (GeoType::is_line(target.type)) {
        // --- 线性对象处理 ---
        double x1 = target.result.x1; double y1 = target.result.y1;
        double dx = target.result.x2 - x1; double dy = target.result.y2 - y1;
        double len_sq = dx * dx + dy * dy;

        if (len_sq < 1e-12) { self.error_status = GeoErrorStatus::ERR_MATH_DOMAIN; return; }

        if (is_first_run) {
            final_t = ((anchor_x - x1) * dx + (anchor_y - y1) * dy) / len_sq;

            // 【范围限制】
            if (target.type == GeoType::LINE_SEGMENT) {
                final_t = std::clamp(final_t, 0.0, 1.0);
            } else if (target.type == GeoType::LINE_RAY) {
                final_t = std::max(0.0, final_t);
            }
            self.result.t = final_t;
        } else {
            final_t = self.result.t;
        }

        final_x = x1 + final_t * dx;
        final_y = y1 + final_t * dy;
    }
    else if (GeoType::is_circle(target.type)) {
        // --- 圆/圆弧对象处理 ---
        double cx = target.result.cx; double cy = target.result.cy;
        double r  = target.result.cr;

        if (is_first_run) {
            // 计算投射到圆上的极角
            double current_ang = std::atan2(anchor_y - cy, anchor_x - cx);

            // 【范围限制】判断是否为圆弧 (通过检查 type 或是否有 t_start)
            bool is_arc = (target.type == GeoType::ARC_2POINTS_1RADIUS ||
                           target.type == GeoType::ARC_3POINTS ||
                           target.type == GeoType::ARC_3POINTS_CIRCUMARC);

            if (is_arc) {
                double s = target.result.t_start;
                double e = target.result.t_end;

                if (!is_ang_in_arc(current_ang, s, e)) {
                    // 如果不在范围内，吸附到最近的端点
                    if (ang_dist(current_ang, s) < ang_dist(current_ang, e)) {
                        current_ang = s;
                    } else {
                        current_ang = e;
                    }
                }
            }
            final_t = current_ang;
            self.result.t = final_t;
        } else {
            final_t = self.result.t;
        }

        final_x = cx + r * std::cos(final_t);
        final_y = cy + r * std::sin(final_t);
    }
    else {
        self.error_status = GeoErrorStatus::ERR_TYPE_MISMATCH;
        return;
    }

    // 3. 结果写入
    self.result.x = final_x;
    self.result.y = final_y;
    self.result.x_view = final_x - v.offset_x;
    self.result.y_view = final_y - v.offset_y;

    // 通道缓存更新（用于后续依赖）
    self.channels[0].value = final_x;
    self.channels[1].value = final_y;

    self.error_status = GeoErrorStatus::VALID;
}

void Solver_VerticalLine(GeoNode& self, GeometryGraph& graph) {
    // parents[0] 是点 P，parents[1] 是目标直线 L
    const auto& p_node = graph.get_node_by_id(self.parents[0]);
    const auto& l_node = graph.get_node_by_id(self.parents[1]);
    const auto& v = graph.view;

    // 1. 安全检查
    if (!GeoErrorStatus::ok(p_node.error_status) || !GeoErrorStatus::ok(l_node.error_status)) {
        self.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
        return;
    }

    // 2. 提取基础几何数据
    // 点 P
    double px = p_node.result.x;
    double py = p_node.result.y;

    // 目标直线 L (通过两个定义点 A, B)
    double x1 = l_node.result.x1;
    double y1 = l_node.result.y1;
    double x2 = l_node.result.x2;
    double y2 = l_node.result.y2;

    // 3. 计算投影点 (垂足 H)
    // 向量 AB = (dx, dy)
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len_sq = dx * dx + dy * dy;

    // 如果定义直线的两点重合，无法计算垂线
    if (len_sq < 1e-12) {
        self.error_status = GeoErrorStatus::ERR_MATH_DOMAIN;
        return;
    }

    // 计算投影参数 t = (PA · AB) / |AB|^2
    // 其中 PA = (px - x1, py - y1)
    double t = ((px - x1) * dx + (py - y1) * dy) / len_sq;

    // 垂足 H = A + t * AB
    double hx = x1 + t * dx;
    double hy = y1 + t * dy;

    // 4. 特殊情况：如果点 P 就在直线上，垂足 H 会与 P 重合，无法定义唯一垂线方向
    // 此时我们需要构造一个法向量来确定方向
    if (std::abs(px - hx) < 1e-10 && std::abs(py - hy) < 1e-10) {
        // 构造垂直于 (dx, dy) 的法向量 (-dy, dx)
        hx = px - dy;
        hy = py + dx;
    }

    // 5. 填充结果槽位
    // 定义垂线的两点：P (原点) 和 H (辅助垂足点)
    self.result.x1 = px;
    self.result.y1 = py;
    self.result.x2 = hx;
    self.result.y2 = hy;

    // 计算视口相对坐标 (View Space) 用于渲染
    self.result.x1_view = px - v.offset_x;
    self.result.y1_view = py - v.offset_y;
    self.result.x2_view = hx - v.offset_x;
    self.result.y2_view = hy - v.offset_y;

    // 6. 最终校验
    if (!std::isfinite(hx) || !std::isfinite(hy)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
    } else {
        self.error_status = GeoErrorStatus::VALID;
    }
}


void Solver_ParallelLine(GeoNode& self, GeometryGraph& graph) {
    // parents[0]: 过点 P, parents[1]: 目标参考线 L
    const auto& p_node = graph.get_node_by_id(self.parents[0]);
    const auto& l_node = graph.get_node_by_id(self.parents[1]);
    const auto& v = graph.view;

    if (!GeoErrorStatus::ok(p_node.error_status) || !GeoErrorStatus::ok(l_node.error_status)) {
        self.error_status = GeoErrorStatus::ERR_PARENT_INVALID;
        return;
    }

    // 1. 提取参考点 P
    double px = p_node.result.x;
    double py = p_node.result.y;

    // 2. 提取参考线 L 的方向向量
    double x1 = l_node.result.x1;
    double y1 = l_node.result.y1;
    double x2 = l_node.result.x2;
    double y2 = l_node.result.y2;

    double dx = x2 - x1;
    double dy = y2 - y1;

    // 3. 安全检查：如果参考线退化为点
    if (std::abs(dx) < 1e-12 && std::abs(dy) < 1e-12) {
        self.error_status = GeoErrorStatus::ERR_MATH_DOMAIN;
        return;
    }

    // 4. 构造平行线
    // 平行线过 P，方向向量与 L 一致，所以第二个点为 P + (dx, dy)
    double p2x = px + dx;
    double p2y = py + dy;

    // 5. 写入结果槽位
    self.result.x1 = px;
    self.result.y1 = py;
    self.result.x2 = p2x;
    self.result.y2 = p2y;

    self.result.x1_view = px - v.offset_x;
    self.result.y1_view = py - v.offset_y;
    self.result.x2_view = p2x - v.offset_x;
    self.result.y2_view = p2y - v.offset_y;

    if (!std::isfinite(p2x) || !std::isfinite(p2y)) {
        self.error_status = GeoErrorStatus::ERR_OVERFLOW;
    } else {
        self.error_status = GeoErrorStatus::VALID;
    }
}