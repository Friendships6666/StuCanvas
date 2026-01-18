// --- 文件路径: include/graph/GeoGraph.h ---
#ifndef GEOGRAPH_H
#define GEOGRAPH_H

#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <cmath>

#include "../../pch.h"
#include "../CAS/RPN/RPN.h"
#include "../CAS/RPN/ShuntingYard.h"


// =========================================================
// 1. 基础宏与常量定义
// =========================================================
#ifndef NULL_ID
#define NULL_ID 0xFFFFFFFF
#endif

#ifndef FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#endif

struct GeoNode;
struct ViewState;
struct NDCMap;
struct FunctionResult;


struct alignas(64) ViewState {
    double offset_x = 0.0;
    double offset_y = 0.0;
    double zoom = 0.1;
    double screen_width = 1920.0;
    double screen_height = 1080.0;
    double wppx = 0.0;
    double wppy = 0.0;
    Vec2   world_origin = {0.0, 0.0};

    // ---------------------------------------------------------
    // C++23 极致性能比对函数
    // ---------------------------------------------------------
    FORCE_INLINE bool is_different_from(const ViewState& other) const noexcept {
        // 在 Clang 21 开启 -O3 时，对于这种对齐的固定大小结构体，
        // std::memcmp 会被编译器直接内联为 2-4 条连续的 SIMD 比较指令（如 VPCMPEQQ）。
        // 它的速度比逐个字段判断快一个量级，且能捕获任何细微的位变动。
        return std::memcmp(this, &other, sizeof(ViewState)) != 0;
    }

    // 提供一个快速拷贝函数用于 Ping-Pong 切换
    FORCE_INLINE void copy_from(const ViewState& other) noexcept {
        std::memcpy(this, &other, sizeof(ViewState));
    }
};


// 统一函数指针签名
using SolverFunc = void(*)(GeoNode& self, std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, const ViewState& view);
using RenderTaskFunc = void(*)(
    GeoNode& self,
    const std::vector<GeoNode>& pool,
    const std::vector<int32_t>& id_map, // <--- 新增此参数
    const ViewState& v,
    const NDCMap& m,
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>& q
);

// =========================================================
// 2. 运行时补丁结构 (Resolved IDs)
// =========================================================
struct RuntimeBindingSlot {
    size_t rpn_index;                          // Bytecode 数组中的下标
    CAS::Parser::CustomFunctionType func_type; // 函数类型 (NONE 代表普通变量)
    std::vector<uint32_t> dependency_ids;      // 依赖的父节点逻辑 ID 列表
};

// =========================================================
// 3. 大一统结果与逻辑槽位 (Fat Slot)
// =========================================================
struct alignas(64) ComputedResult {
    // --- 物理数据区 (32 字节) ---
    union {
        // 通用标量/坐标槽
        struct { double s0, s1, s2, s3; };
        // 几何语义：点(x,y) | 圆(cx,cy,cr) | 线(x1,y1,x2,y2)
        struct { double x, y, z, w; };
        struct { double cx, cy, cr, angle; };
        struct { double x1, y1, x2, y2; };
        // 数学语义：复数 | 测量值
        struct { double re, im, mag, phase; };
        struct { double area, length, slope, val; };
    };

    // --- RPN 逻辑层 (16 字节) ---
    RPNToken*           bytecode_ptr = nullptr;
    RuntimeBindingSlot* patch_ptr = nullptr;
    uint32_t            bytecode_len = 0;
    uint32_t            patch_len = 0;

    // --- 状态与元数据 (16 字节) ---
    uint32_t flags = 0;
    int32_t  i0 = 0, i1 = 0, i2 = 0;

    enum FlagMask : uint32_t {
        VALID        = 1 << 0,
        VISIBLE      = 1 << 1,
        DIRTY        = 1 << 2,
        IS_INFINITE  = 1 << 3,
        IS_HEURISTIC = 1 << 4,
        SELECTED     = 1 << 5
    };

    FORCE_INLINE void set_f(uint32_t mask, bool val) {
        if (val) flags |= mask;
        else flags &= ~mask;
    }
    FORCE_INLINE bool check_f(uint32_t mask) const {
        return (flags & mask) != 0;
    }
    FORCE_INLINE void reset() {
        std::memset(this, 0, sizeof(ComputedResult));
    }

    template<int N> FORCE_INLINE double& get() {
        static_assert(N >= 0 && N < 4, "Slot index range 0-3");
        return *(&s0 + N);
    }
    template<int N> FORCE_INLINE const double& get() const {
        return *(&s0 + N);
    }
};

// =========================================================
// 4. 几何节点 (Fat Entity)
// =========================================================
struct GeoNode {
    /**
     * @brief 渲染类型枚举：决定了该节点在画面中如何呈现
     */
    enum class RenderType : uint8_t {
        None = 0,
        Point,      // 自由点、中点、交点
        Line,       // 直线、线段、射线
        Circle,     // 圆、圆弧
        Explicit,   // 显函数 y=f(x)
        Implicit,   // 隐函数 F(x,y)=0
        Parametric, // 参数方程 x=f(t), y=g(t)
        Scalar,     // 纯数值(滑杆/中间计算)
        Text        // 文字标签
    };

    /**
     * @brief 视觉配置：存储节点的静态样式信息
     */
    struct VisualConfig {
        std::string name = "BasicObject";
        float    thickness = 2.0f;           // 线宽或点径
        uint32_t color = 0x4D4DFFFF;         // 主体颜色 (RGBA)
        bool     is_visible = true;          // 总开关
        bool     show_label = true;          // 是否显示文字
        float    label_offset_x = 15.0f;     // 像素偏移
        float    label_offset_y = -15.0f;
        float    label_size = 12.0f;         // 字号
        uint32_t label_color = 0x4D4DFFFF;   // 标签颜色
    };

    // --- 核心属性 ---
    uint32_t id = NULL_ID;
    uint32_t rank = 0;
    RenderType render_type = RenderType::None;

    ComputedResult result;  // 大一统计算槽位
    VisualConfig   config;  // 视觉样式配置

    // --- 拓扑树 ---
    std::vector<uint32_t> parents;
    std::vector<uint32_t> children;

    // --- 桶索引 (物理索引) ---
    uint32_t prev_in_bucket = NULL_ID;
    uint32_t next_in_bucket = NULL_ID;

    // --- 行为挂载 ---
    SolverFunc     solver = nullptr;
    RenderTaskFunc render_task = nullptr;

    // --- 状态与缓存属性 ---
    uint32_t buffer_offset = 0;
    uint32_t current_point_count = 0;
    bool     active = false;
    bool     is_in_bucket = false;

    // --- 构造函数 ---
    GeoNode() { result.reset(); }
    explicit GeoNode(uint32_t _id) : id(_id) { result.reset(); }

    // --- 辅助工具 ---
    static constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8)  | (static_cast<uint32_t>(a));
    }

    FORCE_INLINE const ComputedResult& get_parent_res(const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, uint32_t p_idx) const {
        return pool[lut[parents[p_idx]]].result;
    }
};

// =========================================================
// 5. 几何图管理核心 (GeometryGraph)
// =========================================================
class GeometryGraph {
public:
    ViewState view;          // 当前活跃视口 (由 JS/Factory 修改)
    ViewState m_last_view;   // 上一帧计算后的视口备份

    std::vector<uint32_t> m_pending_seeds;
    void mark_as_seed(uint32_t id) {
        // 使用简单的 push_back，FastScan 内部会处理重复
        m_pending_seeds.push_back(id);
    }
    std::vector<GeoNode> node_pool;
    std::vector<int32_t> id_to_index_table;
    std::atomic<uint32_t> id_generator;

    uint32_t next_name_index = 0;
    std::unordered_map<std::string, uint32_t> name_to_id_map;

    std::vector<uint32_t> buckets_all_heads;
    std::vector<uint64_t> active_ranks_mask;
    uint32_t max_graph_rank = 0;

    std::vector<uint8_t> m_dirty_mask;

    GeometryGraph();

    uint32_t allocate_node();
    void physical_delete(uint32_t id);

    FORCE_INLINE bool is_alive(uint32_t id) const {
        return id < id_to_index_table.size() && id_to_index_table[id] != -1;
    }

    FORCE_INLINE GeoNode& get_node_by_id(uint32_t id) {
        return node_pool[id_to_index_table[id]];
    }
    FORCE_INLINE const GeoNode& get_node_by_id(uint32_t id) const {
        return node_pool[id_to_index_table[id]];
    }

    std::string GenerateNextName();
    void RegisterNodeName(const std::string& name, uint32_t id);
    void UnregisterNodeName(const std::string& name);
    uint32_t GetNodeID(const std::string& name) const;

    void LinkAndRank(uint32_t child_id, const std::vector<uint32_t>& new_parent_ids);
    void DetachFromBucket(uint32_t id);
    void MoveNodeInBuckets(uint32_t id, uint32_t new_rank);
    void UpdateRankRecursive(uint32_t start_node_id);

    FORCE_INLINE bool detect_view_change() const {
        return view.is_different_from(m_last_view);
    }


    FORCE_INLINE void sync_view_snapshot() {
        m_last_view.copy_from(view);
    }

    [[nodiscard]] bool DetectCycle(uint32_t child_id, uint32_t parent_id) const;
    std::vector<uint32_t> FastScan();



private:
    void UpdateBit(uint32_t rank, bool has_elements);
    void update_mapping_after_erase(size_t start_index);
};

#endif // GEOGRAPH_H