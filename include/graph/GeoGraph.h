// --- 文件路径: include/graph/GeoGraph.h ---
#ifndef GEOGRAPH_H
#define GEOGRAPH_H

#include "../../pch.h"
#include "../functions/lerp.h"

#include "../CAS/RPN/RPN.h"
#include <variant>
#include <vector>


struct RPNBinding {
    uint32_t token_index{};   // RPN 指令数组中的下标 (需要被填写的 PUSH_CONST 位置)
    uint32_t parent_index{};  // 在当前节点的 parents 列表中的下标 (注意是局部下标，不是全局ID)

    // 从父节点提取什么属性？
    enum Property {
        VALUE, // 标量值 (默认)
        POS_X, // 点的 X 坐标
        POS_Y  // 点的 Y 坐标
    } prop = VALUE;
};
// =========================================================
// 1. 核心对象组件 (Components)
// =========================================================
enum class ScalarType {
    Manual,      // 手动输入的数值 (滑动条)
    Length,      // 两点距离或线段长度
    Angle,       // 三点夹角或两条直线夹角
    Slope,       // 直线斜率
    Area,        // 多边形面积
    Expression   // 其他标量运算结果 (如 a + b)
};

struct Data_Scalar {
    double value = 0.0; // 最终计算结果，供其他节点读取
    ScalarType type = ScalarType::Expression;

    // RPN 核心指令与绑定
    AlignedVector<RPNToken> tokens{};
    std::vector<RPNBinding> bindings{};
};

struct Data_Point {
    double x = 0.0, y = 0.0; // 缓存坐标
    // 规定：parents[0] 为 X 标量节点，parents[1] 为 Y 标量节点
};

// 约束点额外信息: 依赖一个对象 ID (Line/Circle/Function)
struct Data_ConstrainedPoint {
    uint32_t target_obj_id;
};

struct Data_AnalyticalIntersection {
    // 1 代表使用 +sqrt(delta)，-1 代表使用 -sqrt(delta)
    // 0 用于只有唯一解的情况（如线线交点）
    int branch_sign = 0;

    double x = 0.0;
    double y = 0.0;
    bool is_found = false;
};

struct Data_IntersectionPoint {
    double x = 0.0; // ★ 新增：最终交点 X
    double y = 0.0; // ★ 新增：最终交点 Y

    double anchor_x = 0.0;
    double anchor_y = 0.0;
    bool is_found = false;
    uint32_t num_targets = 0;
};



// 显函数/隐函数
struct Data_SingleRPN {
    AlignedVector<RPNToken> tokens{};
    std::vector<RPNBinding> bindings{};
};

// 参数方程
struct Data_DualRPN {
    AlignedVector<RPNToken> tokens_x{};
    AlignedVector<RPNToken> tokens_y{};
    std::vector<RPNBinding> bindings_x{}; // 处理 x(t) 的动态参数
    std::vector<RPNBinding> bindings_y{}; // 处理 y(t) 的动态参数
    double t_min{}, t_max{};
};

// 直线/线段: 依赖两个点 ID
struct Data_Line {
    uint32_t p1_id;
    uint32_t p2_id;
    bool is_infinite; // true=直线, false=线段
};

// 圆: 依赖圆心点 ID 和半径 (固定值或标量 ID)
struct Data_Circle {

    double cx = 0.0, cy = 0.0; // 缓存圆心
    double radius = 0.0;       // 缓存半径
    // 规定：parents[0] 为圆心点节点，parents[1] 为半径标量节点
};
struct Data_CalculatedLine {
    double x1, y1;
    double x2, y2;
    bool is_infinite; // true=直线, false=线段
};
struct Data_AnalyticalConstrainedPoint {
    double t = 0.0;              // 锁定的参数 (线段比例或圆弧角度)
    bool is_initialized = false; // 是否已锁定 t
    double x = 0.0, y = 0.0;     // 计算出的世界坐标缓存
};
struct Data_RatioPoint {
    double x = 0.0;
    double y = 0.0;
};

struct GeoNode;
using SolverFunc = void(*)(GeoNode& self, const std::vector<GeoNode>& pool);

struct GeoNode {
    uint32_t id{};
    uint32_t rank = 0;
    bool active = true;
    bool is_visible = true;

    // 拓扑
    std::vector<uint32_t> parents{};
    std::vector<uint32_t> children{};

    enum class RenderType {
        None,
        Point,      // 包含自由、约束、交点、中点
        Line,       // 直线/线段
        Circle,     // 圆
        Explicit,   // 显函数
        Parametric, // 参数方程
        Implicit,   // 隐函数
        Scalar      // 纯数值
    } render_type = RenderType::None;

    // 显存/范围管理 (用于传给 plotCall)
    uint32_t buffer_offset = 0;
    uint32_t current_point_count = 0;
    uint32_t last_update_frame = 0;


    // 数据变体
    using GeoPayload = std::variant<
        std::monostate,
        Data_Point,             // 自由点、中点、计算后的最终坐标
        Data_ConstrainedPoint,  // 约束点逻辑
        Data_IntersectionPoint, // 交点逻辑
        Data_Line,              // 直线逻辑
        Data_CalculatedLine,    // ★ 新增：直接存坐标的线 (新)
        Data_Circle,            // 圆逻辑
        Data_SingleRPN,         // 函数
        Data_DualRPN,           // 参数方程
        Data_Scalar,             // 标量
        Data_AnalyticalIntersection,
        Data_AnalyticalConstrainedPoint,
        Data_RatioPoint
    >;

    GeoPayload data;
    SolverFunc solver = nullptr;

    GeoNode() = default;
    explicit GeoNode(uint32_t _id) : id(_id) {}

    // 类型安全检查辅助
    bool check_parent_type(const std::vector<GeoNode>& pool, size_t parent_idx, RenderType expected) const {
        if (parent_idx >= parents.size()) return false;
        return pool[parents[parent_idx]].render_type == expected;
    }
    using RenderTaskFunc = void(*)(GeoNode&, const std::vector<GeoNode>&, const ViewState&, const NDCMap&, oneapi::tbb::concurrent_bounded_queue<FunctionResult>&);
    RenderTaskFunc render_task = nullptr;
    bool is_heuristic = false;       // 是否是图解点（直接读 Buffer）
    bool is_buffer_dependent = false; // 是否依赖于任何图解点（间接依赖 Buffer）
};

class GeometryGraph {
public:
    std::vector<GeoNode> node_pool{};
    std::vector<std::vector<uint32_t>> buckets{};
    uint32_t current_frame_index = 1;
    uint32_t min_dirty_rank = 10000, max_dirty_rank = 0;


    GeometryGraph();
    uint32_t allocate_node();
    void TouchNode(uint32_t id);
    std::vector<uint32_t> SolveFrame();
    [[nodiscard("必须检查循环依赖，否则 SolveFrame 会崩溃！")]] bool DetectCycle(uint32_t child_id, uint32_t parent_id) const;
    std::vector<std::vector<uint32_t>> GetRequiredRankedBatches(const std::vector<uint32_t>& targets);

private:
    void Enqueue(GeoNode& node);
};

#endif