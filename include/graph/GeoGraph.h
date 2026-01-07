// --- 文件路径: include/graph/GeoGraph.h ---
#ifndef GEOGRAPH_H
#define GEOGRAPH_H

#include "../../pch.h"
#include "../functions/lerp.h"
#include "../graph/GeoSolver.h"
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
struct Data_TextLabel {
    double world_x = 0.0; // 锚点的世界坐标缓存
    double world_y = 0.0;
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


struct ObjectStyle {


    enum class Line : uint32_t {
        Solid = 0x1001, Dashed = 0x1002, Dotted = 0x1003
    };


    enum class Point : uint32_t {
        Free = 0x2001, Intersection = 0x2002, Constraint = 0x2003,
        Circle = 0x2004, Square = 0x2005, Diamond = 0x2006
    };



    static bool IsLine(uint32_t s) { return s >= 0x1000 && s < 0x2000; }
    static bool IsPoint(uint32_t s) { return s >= 0x2000 && s < 0x3000; }
};



struct GeoNode {
    uint32_t id{};



    enum class RenderType {
        None,
        Point,      // 包含自由、约束、交点、中点
        Line,       // 直线/线段
        Circle,     // 圆
        Explicit,   // 显函数
        Parametric, // 参数方程
        Implicit,   // 隐函数
        Scalar,      // 纯数值
        Text
    } render_type = RenderType::None;

    struct {
        double x = 0.0;      // 存储点坐标或圆心X
        double y = 0.0;      // 存储点坐标或圆心Y
        double scalar = 0.0; // 存储标量值或半径
        bool is_valid = false;
    } result;




    static constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8)  |
               (static_cast<uint32_t>(a));
    }
    // 视觉配置包
    struct VisualConfig {
        std::string name = "BasicObject";
        float thickness = 2.0f;           // 默认粗细
        uint32_t color = PackRGBA(77, 77, 77);
        uint32_t style = static_cast<uint32_t>(ObjectStyle::Line::Solid); // 统一存储为 u32
        float opacity = 1.0f;              // 默认不透明



        // --- 文字标签控制 (新增) ---
        bool show_label = true;           // 开关：若为false，关闭昂贵的图解搜索
        float label_offset_x = 15.0f;     // 像素偏移
        float label_offset_y = -15.0f;
        float label_size = 12.0f;         // 字号
        float label_rotation = 0.0f;      // 旋转弧度
        uint32_t label_color =PackRGBA(77, 77, 77);// 标签独立颜色



    };



    VisualConfig config; // 每个节点自带一份视觉配置

    // 拓扑
    uint32_t rank{0};
    std::vector<uint32_t> parents{};
    std::vector<uint32_t> children{};
    bool is_in_bucket = false;            // 标记当前是否已挂载到 buckets_all 中


    uint32_t prev_in_bucket = 0xFFFFFFFF;
    uint32_t next_in_bucket = 0xFFFFFFFF;

    // 逻辑判定位
    bool is_heuristic = false;


    bool active = true;
    bool is_visible = true;






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
        Data_RatioPoint,
        Data_TextLabel
    >;

    GeoPayload data;
    SolverFunc solver = nullptr;

    GeoNode() = default;
    explicit GeoNode(uint32_t _id) : id(_id) {}

    // 类型安全检查辅助
    [[nodiscard]] bool check_parent_type(const std::vector<GeoNode>& pool, size_t parent_idx, RenderType expected) const {
        if (parent_idx >= parents.size()) return false;
        return pool[parents[parent_idx]].render_type == expected;
    }
    using RenderTaskFunc = void(*)(GeoNode&, const std::vector<GeoNode>&, const ViewState&, const NDCMap&, oneapi::tbb::concurrent_bounded_queue<FunctionResult>&);
    RenderTaskFunc render_task = nullptr;

};

class GeometryGraph {
public:
    // 命名计数器，记录当前已分配了多少个自动生成的名称
    uint32_t next_name_index = 0;

    // 每一层只存储链表头部的 ID
    std::vector<uint32_t> buckets_all_heads;

    // 位图：记录哪些 Rank 是非空的
    std::vector<uint64_t> active_ranks_mask;


    void MoveNodeInBuckets(uint32_t id, uint32_t new_rank);





    std::string GenerateNextName();
    std::vector<GeoNode> node_pool{};
    std::vector<std::vector<uint32_t>> buckets{};
    uint32_t current_frame_index = 1;
    uint32_t min_dirty_rank = 10000, max_dirty_rank = 0;
    std::vector<uint8_t> m_dirty_mask;

    uint32_t max_graph_rank = 0; // 记录当前图中最大的 Rank 级别


    GeometryGraph();
    uint32_t allocate_node();

    [[nodiscard("必须检查循环依赖，否则 SolveFrame 会崩溃！")]] bool DetectCycle(uint32_t child_id, uint32_t parent_id) const;

    void UpdateRankRecursive(uint32_t node_id);
    void DetachFromBucket(uint32_t id);
    std::vector<uint32_t> FastScan(const std::vector<uint32_t>& moved_ids);
    void LinkAndRank(uint32_t child_id, const std::vector<uint32_t>& new_parent_ids);

private:
    void UpdateBit(uint32_t rank, bool has_elements);
};

#endif