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

#include "../graph/GeoCommands.h"

// =========================================================
// 1. 基础宏与常量定义
// =========================================================


#ifndef FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#endif

struct GeoNode;
struct ViewState;
struct GeoFunctionMeta;


namespace GlobalState {
    enum Mask : uint64_t {
        DISABLE_LABELS = 1ULL << 0, // 全局第一位：关闭所有标签显示
    };
}

enum class FontType : uint8_t {
    SANS_SERIF = 0,
    MONOSPACE = 1,
    SERIF = 2
};

struct LabelConfig {
    bool     show = true;
    int16_t  offset_x = 15;   // 屏幕像素偏移
    int16_t  offset_y = -15;
    float    size = 12.0f;
    uint32_t color = 0xFFFFFFFF;
    FontType font = FontType::SANS_SERIF;
};

// 假设项目中定义的类型



struct alignas(64) ViewState {
    // ==========================================
    // 1. 基础配置 (由外部 JS/UI 直接修改)
    // ==========================================
    double offset_x = 0.0;
    double offset_y = 0.0;
    double zoom = 0.1;
    double screen_width = 2560;
    double screen_height = 1600;

    // 极致压缩常量 M (int16_t 的满量程)
    static constexpr double M = 32767.0;
    static constexpr double InvM = 1.0 / 32767.0;

    // ==========================================
    // 2. 预计算派生参数
    // ==========================================
    double half_w, half_h;
    double wpp, inv_wpp;
    double ndc_scale_x, ndc_scale_y;
    double c2w_scale_x, c2w_scale_y;
    double s2c_scale_x, s2c_scale_y;

    // ==========================================
    // 3. 极致优化的 6 大坐标转换成员函数
    // ==========================================

    // ① World → Screen (返回 double，用于 UI 精确排版)
    FORCE_INLINE Vec2 WorldToScreen(double wx, double wy) const noexcept {
        return {
            (wx - offset_x) * inv_wpp + half_w,
            (offset_y - wy) * inv_wpp + half_h
        };
    }

    // ② Screen → World (接收 double 像素坐标)
    FORCE_INLINE Vec2 ScreenToWorld(double sx, double sy) const noexcept {
        return {
            (sx - half_w) * wpp + offset_x,
            offset_y - (sy - half_h) * wpp
        };
    }

    // ③ World → Clip (关键转换：由 double 转换为 int16_t 存储)
    FORCE_INLINE Vec2i WorldToClip(double wx, double wy) const noexcept {
        return {
            static_cast<int16_t>((wx - offset_x) * ndc_scale_x),
            static_cast<int16_t>((wy - offset_y) * ndc_scale_y)
        };
    }

    FORCE_INLINE Vec2i WorldToClipNoOffset(double wx, double wy) const noexcept {
        return {
            static_cast<int16_t>(wx * ndc_scale_x),
            static_cast<int16_t>(wy * ndc_scale_y)
        };
    }

    // ④ Clip → World (逆向还原：从 int16_t 恢复为 double)
    FORCE_INLINE Vec2 ClipToWorld(int16_t cx, int16_t cy) const noexcept {
        return {
            static_cast<double>(cx) * c2w_scale_x + offset_x,
            static_cast<double>(cy) * c2w_scale_y + offset_y
        };
    }

    // ⑤ Screen → Clip (直接投影：像素快速转 int16_t，用于拾取碰撞)
    FORCE_INLINE Vec2i ScreenToClip(double sx, double sy) const noexcept {
        return {
            static_cast<int16_t>(sx * s2c_scale_x - M),
            static_cast<int16_t>(M - sy * s2c_scale_y)
        };
    }

    // ⑥ Clip → Screen (快速映射：int16_t 转 double 像素坐标)
    FORCE_INLINE Vec2 ClipToScreen(int16_t cx, int16_t cy) const noexcept {
        double dcx = static_cast<double>(cx);
        double dcy = static_cast<double>(cy);
        return {
            (dcx * InvM + 1.0) * half_w,
            (1.0 - dcy * InvM) * half_h
        };
    }

    // ==========================================
    // 4. 状态维护与同步函数
    // ==========================================

    /**
     * @brief 更新所有预计算系数 (在 offset, zoom 或 size 改变后调用)
     */
    void Refresh() noexcept {
        half_w = screen_width * 0.5;
        half_h = screen_height * 0.5;
        double aspect = screen_width / screen_height;

        // 根据推导：NDC_ScaleY = M * Zoom
        ndc_scale_y = M * zoom;
        ndc_scale_x = ndc_scale_y / aspect;

        // WPP = 2.0 / (Height * Zoom)
        wpp = 2.0 / (screen_height * zoom);
        inv_wpp = 1.0 / wpp;

        // 预计算反向系数，彻底消除运行时的除法
        c2w_scale_x = 1.0 / ndc_scale_x;
        c2w_scale_y = 1.0 / ndc_scale_y;

        s2c_scale_x = (M * 2.0) / screen_width;
        s2c_scale_y = (M * 2.0) / screen_height;
    }

    /**
     * @brief 极致性能复制 (用于 ViewSnapshot 备份)
     */
    FORCE_INLINE void copy_from(const ViewState& other) noexcept {
        std::memcpy(this, &other, sizeof(ViewState));
    }

    /**
     * @brief 极致性能检测 (用于判别是否触发全量重算)
     */
    FORCE_INLINE bool is_different_from(const ViewState& other) const noexcept {
        return std::memcmp(this, &other, sizeof(ViewState)) != 0;
    }
};


// 统一函数指针签名
using SolverFunc = void(*)(GeoNode& self, GeometryGraph& graph);
using RenderTaskFunc = void(*)(
    GeoNode& self,
    GeometryGraph& graph,
    const ViewState& view, // 修改这里
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData>>& q // 修改这里
);

namespace CAS::Parser {
    // 💡 正确的前向声明方式：必须指定底层类型 (例如 : uint8_t)
    enum class CustomFunctionType : uint8_t;
}

struct RuntimeBindingSlot {
    size_t rpn_index;                          // Bytecode 数组中的下标
    CAS::Parser::CustomFunctionType func_type; // 函数类型 (NONE 代表普通变量)
    std::vector<uint32_t> dependency_ids;      // 依赖的父节点逻辑 ID 列表
};

// =========================================================
// 3. 大一统结果与逻辑槽位 (Fat Slot)
// =========================================================
struct alignas(64) ComputedResult {
    union {
        // --- 语义层 1：纯数学/标量模式 (Calculator Mode) ---
        // 用于非几何节点，如 "2+2" 的结果存储。s4-s6 为以后扩展留出的标量槽。
        struct { double s0, s1, s2, s3, s4, s5, s6; };

        // --- 语义层 2：几何点模式 (World + View Space) ---
        struct {
            // A. 原始世界坐标 (World Space)
            // 保持 x,y,z,w 命名，兼容旧的 ExtractX/Y 函数及所有拓扑逻辑
            double x, y, z, w;

            // B. 相对视口坐标 (View/Relative Space)
            // 存储 (World - ViewOffset)，解决大数值坐标下的渲染抖动与精度丢失
            double x_view, y_view;

            // C. 几何备用槽位 1
            double spare_geo_0;
        };

        // --- 语义层 3：圆与圆锥曲线 ---
        struct {
            double cx, cy, cr, angle; // 世界空间
            double cx_view, cy_view;  // 视口空间
            double spare_conic_0;
        };

        // --- 语义层 4：线段与向量 ---
        struct {
            double x1, y1, x2, y2;    // 世界空间
            double x1_view, y1_view;  // 视口空间
            double spare_line_0;
        };

        // 占位填充：确保 Union 部分占用 56 字节 (7个double)
        double _raw_data[7];
    };

    // --- 状态与元数据 (占据最后 8 字节，凑齐 64 字节) ---
    // 56 (数据) + 4 (flags) + 4 (i0) = 64 Bytes
    uint32_t flags;
    int32_t  i0;    // 备用整数槽位 (如：存储约束点的目标ID)

    enum FlagMask : uint32_t {
        VISIBLE      = 1 << 0,
        DIRTY        = 1 << 1,
        IS_INFINITE  = 1 << 2,
        IS_HEURISTIC = 1 << 3,
        SELECTED     = 1 << 4
    };

    // --- 极致性能工具函数 ---

    FORCE_INLINE void set_f(uint32_t mask, bool val) {
        if (val) flags |= mask;
        else     flags &= ~mask;
    }

    FORCE_INLINE bool check_f(uint32_t mask) const {
        return (flags & mask) != 0;
    }

    /**
     * @brief 仅重置数值区 (32-56字节)，不触动元数据
     */
    FORCE_INLINE void reset_data() {
        // 使用高效的内存归零
        std::memset(&_raw_data, 0, 56);
    }

    /**
     * @brief 彻底重置所有 64 字节（物理清零）
     */
    FORCE_INLINE void reset_all() {
        std::memset(this, 0, 64);
    }

    // 快捷索引访问
    template<int N> FORCE_INLINE double& get() {
        static_assert(N >= 0 && N < 7, "Slot index out of range (0-6)");
        return _raw_data[N];
    }
};

namespace GeoType {
    enum Type : uint32_t {
        MASK_CAT         = 0xFF00,

        // --- 1. 点类 (CAT_POINT) ---
        CAT_POINT        = 0x0100,
        POINT_FREE       = 0x0101,
        POINT_CONSTRAINED= 0x0102,
        POINT_INTERSECT  = 0x0103,
        POINT_MID        = 0x0104,

        // --- 2. 线类 (CAT_LINE) ---
        CAT_LINE         = 0x0200,
        LINE_SEGMENT     = 0x0201,
        LINE_STRAIGHT    = 0x0202,
        LINE_RAY         = 0x0203,
        LINE_TANGENT     = 0x0204,
        LINE_PERP        = 0x0205,
        LINE_PARALLEL    = 0x0206,

        // --- 3. 圆锥曲线类 (CAT_CONIC) ---
        CAT_CONIC        = 0x0300,
        CIRCLE_FULL      = 0x0301,
        CIRCLE_ARC       = 0x0302,

        // --- 4. 函数/高级曲线类 (CAT_CURVE) ---
        CAT_CURVE        = 0x0400,
        FUNC_EXPLICIT    = 0x0401,
        FUNC_IMPLICIT    = 0x0402,
        FUNC_PARAMETRIC  = 0x0403,

        // --- 5. 标量/测量类 (CAT_SCALAR) ---
        CAT_SCALAR       = 0x0500,
        SCALAR_INTERNAL  = 0x0501,
        SCALAR_MEASURE   = 0x0502,

        UNKNOWN          = 0x0000
    };

    // 聚合判断辅助
    FORCE_INLINE inline bool is_point(uint32_t t)  { return (t & MASK_CAT) == CAT_POINT; }
    FORCE_INLINE inline bool is_line(uint32_t t)   { return (t & MASK_CAT) == CAT_LINE; }
    FORCE_INLINE inline bool is_conic(uint32_t t)  { return (t & MASK_CAT) == CAT_CONIC; }
    FORCE_INLINE inline bool is_curve(uint32_t t)  { return (t & MASK_CAT) == CAT_CURVE; }
    FORCE_INLINE inline bool is_scalar(uint32_t t) { return (t & MASK_CAT) == CAT_SCALAR; }
}
// --- include/graph/GeoGraph.h ---

namespace GeoStatus {
    enum Code : uint32_t {
        VALID            = 0,          // 完美状态

        // --- 类别掩码 ---
        MASK_CAT         = 0xF000,
        CAT_LINK         = 0x1000,     // 链接/创建错误 (硬伤)
        CAT_MATH         = 0x2000,     // 数学计算错误 (运行时)
        CAT_DEPENDENCY   = 0x4000,     // 依赖失效 (级联)

        // --- 1. 链接错误 (Creation Time) ---
        ERR_ID_NOT_FOUND = 0x1100,     // 找不到指定的父节点 ID
        ERR_TYPE_MISMATCH= 0x1200,     // 类型不匹配（比如线段需要点，你传了函数）
        ERR_SYNTAX       = 0x1300,     // 公式语法错误
        ERR_CIRCULAR     = 0x1400,     // 循环引用检测

        // --- 2. 数学错误 (Runtime) ---
        ERR_DIV_ZERO     = 0x2100,     // 除以零
        ERR_MATH_DOMAIN  = 0x2200,     // 数学定义域错误（负数开根号等）
        ERR_OVERFLOW     = 0x2300,     // 数值溢出 (Infinity)
        ERR_EMPTY_RESULT = 0x2400,     // 求解器无解（如两条平行线求交点）

        // --- 3. 级联错误 (Propagation) ---
        ERR_PARENT_INVALID = 0x4100,   // 因为父节点无效导致我也无法计算
    };

    // 💡 极其迅速的判断函数
    FORCE_INLINE inline bool ok(uint32_t s) { return s == VALID; }
}


struct LogicChannel {
    std::string original_infix;   // ASCIIMATH 源码
    RPNToken*   bytecode_ptr = nullptr;
    RuntimeBindingSlot* patch_ptr = nullptr;
    uint32_t    bytecode_len = 0;
    uint32_t    patch_len = 0;
    double      value = 0.0;      // 计算出的实时数值

    // 释放内存
    FORCE_INLINE void clear() {
        if (bytecode_ptr) {
            delete[] bytecode_ptr;
            bytecode_ptr = nullptr;
        }
        if (patch_ptr) {
            delete[] patch_ptr;
            patch_ptr = nullptr;
        }
        bytecode_len = 0;
        patch_len = 0;
        value = 0.0;
        original_infix.clear(); // string 自带内存管理，这里只是清空内容
    }
};

struct GeoNode {
    uint64_t state_mask = 0;

    FORCE_INLINE void set_state(uint64_t bit_index, bool val) {
        if (val) state_mask |= (1ULL << bit_index);
        else     state_mask &= ~(1ULL << bit_index);
    }

    FORCE_INLINE bool check_state(uint64_t bit_index) const {
        return (state_mask & (1ULL << bit_index)) != 0;
    }

    FORCE_INLINE void toggle_state(uint64_t bit_index) {
        state_mask ^= (1ULL << bit_index);
    }


    LogicChannel channels[4];

    /**
     * @brief 渲染类型枚举：决定了该节点在画面中如何呈现
     */
    GeoType::Type type = GeoType::UNKNOWN;

    /**
     * @brief 视觉配置：存储节点的静态样式信息
     */
    struct VisualConfig {
        std::string name = "BasicObject";
        float    thickness = 2.0f;           // 线宽或点径
        uint32_t color = 0x4D4DFFFF;         // 主体颜色 (RGBA)
        bool     is_visible = true;          // 总开关
        LabelConfig label;
    };

    // --- 核心属性 ---
    uint32_t id = NULL_ID;
    uint32_t rank = 0;

    uint32_t status = GeoStatus::VALID; // 💡 节点生命周期状态
    FORCE_INLINE bool is_compute_ready() const {
        // 只有没有链接错误的节点才值得进入 Solver
        return (status & GeoStatus::MASK_CAT) != GeoStatus::CAT_LINK;
    }


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
    GeoNode()
            : id(NULL_ID),
              type(GeoType::UNKNOWN),
              status(GeoStatus::VALID), // 默认状态为 OK (0)
              active(false)             // 初始不激活
        {
        // 彻底清空大一统计算槽位（物理清零数据和 RPN 指令指针）
        result.reset_all();
        }

    /**
     * @brief 显式构造函数：用于 allocate_node 时的初始化
     */
    explicit GeoNode(uint32_t _id)
        : id(_id),
          type(GeoType::UNKNOWN),
          status(GeoStatus::VALID),
          active(false)
    {
        result.reset_all();
    }


    // --- 辅助工具 ---
    static constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8)  | (static_cast<uint32_t>(a));
    }

    FORCE_INLINE const ComputedResult& get_parent_res(const std::vector<GeoNode>& pool, const std::vector<int32_t>& lut, uint32_t p_idx) const {
        return pool[lut[parents[p_idx]]].result;
    }
};


struct HistoryNode {
    uint32_t id;
    int32_t  parent_id = -1;
    std::vector<GeoCommand::CommandPacket> recipe; // 完整状态配方
    std::vector<uint32_t> children;                // 分支指向
};
namespace GraphStatus {
    enum Code : uint32_t {
        READY = 0,
        ERR_OUT_OF_MEMORY = 0x5001, // 内存溢出
        ERR_INTERNAL_HALT = 0x5002  // 内部严重错误中止
    };
}
struct LabelRenderData;
class GeometryGraph {
public:
    uint32_t status = GraphStatus::READY;
    uint64_t global_state_mask = 0; // 全局开关掩码
    std::vector<LabelRenderData> final_labels_buffer; // 标签容器

    // 默认最大缓冲区设置为 1.7GB (约 1.7 * 1024^3 字节)
    // 注意：PointData 约 12-16 字节，1.7GB 大约能存 1.1 亿到 1.4 亿个点
    size_t max_buffer_bytes = static_cast<size_t>(1.7 * 1024 * 1024 * 1024);
    FORCE_INLINE bool is_healthy() const { return status == GraphStatus::READY; }
    std::vector<PointData> final_points_buffer;
    std::vector<GeoFunctionMeta> final_meta_buffer;
    std::vector<HistoryNode> history_tree;
    int32_t head_version_id = -1;      // 当前 HEAD 指向的版本 ID
    uint32_t version_id_counter = 0;   // 版本自增计数器
    void ClearEverything(); // 💡 新增
    ViewState view;          // 当前活跃视口 (由 JS/Factory 修改)
    ViewState m_last_view;   // 上一帧计算后的视口备份
    uint32_t next_internal_index = 0; // 💡 新增：内部标量计数器
    std::string GenerateInternalName(); // 💡 新增：生成 _internal_scalar_n

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
struct GeoFunctionMeta {
    uint32_t start_index;           // 4 字节
    uint32_t count;                 // 4 字节
    uint32_t id;                    // 4 字节
    GeoType::Type type;             // 4 字节 (因为指定了底层类型为 uint32_t)
    GeoNode::VisualConfig config;   // 视觉配置
};
struct LabelRenderData {
    Vec2i    position;   // 计算后的最终 Clip 坐标
    uint32_t func_id;    // 关联的函数 ID

};

#endif // GEOGRAPH_H