/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string_view>

// 前向声明不透明的 Outline 和 CompileResult 结构体
struct Outline;
struct CompileResult;

// =====================================================================
// 1. 导出 C 风格函数声明 (C 链接性，前置以支持 Outline 析构器调用) [1.2.2]
// =====================================================================
#ifdef __cplusplus
extern "C" {
#endif

CompileResult stucanvas_compile_typst(const char* markup_str, const char* fonts_dir);
void stucanvas_free_outline(Outline* outline);
void stucanvas_free_outline_members(Outline* outline); // 💡 【核心新增】：仅回收成员堆资源的 FFI 接口
void stucanvas_free_string(char* err_str);
void stucanvas_print_detailed_outline(const Outline* outline);

#ifdef __cplusplus
}
#endif

// =====================================================================
// 2. 底层 C 兼容数据结构定义 (C++ 链接性，支持模板与对齐)
// =====================================================================

/// FFI 安全的动态数组容器 (与 Rust 的 CVec<T> 完全一致)
template <typename T>
struct CVector {
    T* ptr;           ///< 指向堆内存首地址的指针 [1]
    uint64_t len;     ///< 数组有效元素数量 [1]
    uint64_t cap;     ///< 数组的最大分配容量 [1]

    // 提供 C++ 友好的迭代器和切片视图支持
    const T* begin() const noexcept { return ptr; }
    const T* end() const noexcept { return ptr + len; }
    bool empty() const noexcept { return len == 0; }
};

/// 2D 物理坐标点
struct Point2D {
    double x;
    double y;
};

/// 线性颜色
struct RGBA {
    float r;
    float g;
    float b;
    float a;
};

/// 列主序 2D 仿射变换矩阵（与 Eigen / GPU Shader 天然 1-to-1 物理对齐） [1.1.1]
struct Transform2D {
    double sx, ky; // 第一列 (Column 0) [1.1.1]
    double kx, sy; // 第二列 (Column 1) [1.1.1]
    double tx, ty; // 第三列 (Column 2) [1.1.1]
};

// =====================================================================
// 枚举类型定义 (强制使用 uint8_t 占用 1 字节) [1]
// =====================================================================

enum class PathVerb : uint8_t {
    MoveTo = 0,
    LineTo = 1,
    CubicTo = 2, // 已精简 QuadTo 分支，紧凑对齐
    Close = 3
};

enum class FillRule : uint8_t {
    None = 0,
    NonZero = 1,
    EvenOdd = 2
};

enum class LineCap : uint8_t {
    Butt = 0,
    Round = 1,
    Square = 2
};

enum class LineJoin : uint8_t {
    Miter = 0,
    Round = 1,
    Bevel = 2
};

enum class GradientType : uint8_t {
    Linear = 0,
    Radial = 1,
    Conic = 2
};

enum class SpreadMethod : uint8_t {
    Pad = 0,
    Repeat = 1,
    Reflect = 2
};

enum class RelativeTo : uint8_t {
    Self_ = 0,
    Parent = 1
};

enum class PaintType : uint8_t {
    None = 0,
    Solid = 1,
    Gradient = 2,
    Tiling = 3
};

enum class GeometryType : uint8_t {
    Path = 0,
    Line = 1,
    Rect = 2
};

// =====================================================================
// 画笔材质 (Paint) 数据结构
// =====================================================================

struct GradientStop {
    double offset;
    RGBA color;
};

struct GradientPaint {
    GradientType ty;
    Point2D start;
    Point2D end;
    double radius;
    double angle;
    SpreadMethod spread;
    RelativeTo relative;
    CVector<GradientStop> stops;
};

struct TilingPaint {
    Outline* pattern; ///< 递归嵌套的矢量平铺子 Outline [1]
    double width;
    double height;
    double spacing_x;
    double spacing_y;
};

struct Paint {
    PaintType ty;
    RGBA solid_color;
    GradientPaint gradient;
    TilingPaint tiling;
};

// =====================================================================
// 多态几何体 (Geometry) 数据结构
// =====================================================================

struct PathGeometry {
    CVector<Point2D> points;
    CVector<PathVerb> verbs;
};

struct LineGeometry {
    Point2D start;
    Point2D end;
};

struct RectGeometry {
    Point2D origin;
    double width;
    double height;
};

/// 几何体内存合并联合体
union GeometryUnion {
    PathGeometry path;
    LineGeometry line;
    RectGeometry rect;
};

struct Geometry {
    GeometryType ty;
    GeometryUnion data;
};

/// 共享几何体 [3.2.1]
struct SharedGeometry {
    uint32_t geometry_id;
    Geometry geometry;
};

/// 实例化绘制指令 [3.2.1]
struct DrawInstance {
    uint32_t geometry_id;       ///< 引用上面的 SharedGeometry ID [3.2.1]
    Transform2D transform;      ///< 独立的列主序仿射变换矩阵 [1.1.1, 3.2.1]
    double opacity;
    bool clip;

    Paint fill_paint;
    FillRule fill_rule;

    Paint stroke_paint;
    double stroke_width;
    LineCap stroke_cap;
    LineJoin stroke_join;
    double miter_limit;
    CVector<double> dash_array;
    double dash_offset;
};

// =====================================================================
// 💡 【核心重构】：支持五法则、移动独占型的高性能 Outline 值类型 [1.1.2]
// =====================================================================
struct Outline {
    CVector<SharedGeometry> geometries; ///< 共享几何池 [3.2.1]
    CVector<DrawInstance> instances;   ///< 实例化绘制指令队列 [3.2.1]

    // ① 默认构造：零初始化
    Outline() noexcept {
        geometries = { nullptr, 0, 0 };
        instances = { nullptr, 0, 0 };
    }

    // ② 析构函数：生命周期结束（如从 FlatMap 中清空/移除）时，自动调用 FFI 仅回收成员，绝不释放 Outline 自身 [1.1.2]
    ~Outline() noexcept {
        ::stucanvas_free_outline_members(this);
    }

    // ③ 禁用拷贝：从编译期完全杜绝 FFI 裸指针发生浅拷贝引起的 Double Free 灾难 [1.1.2]
    Outline(const Outline&) = delete;
    Outline& operator=(const Outline&) = delete;

    // ④ 移动构造：极速接管原 Outline 拥有的堆资源，并将原对象安全置空（耗时仅 0.01 微秒） [1.1.2, 1.1.7]
    Outline(Outline&& other) noexcept {
        geometries = other.geometries;
        instances = other.instances;
        other.geometries = { nullptr, 0, 0 };
        other.instances = { nullptr, 0, 0 };
    }

    // ⑤ 移动赋值
    Outline& operator=(Outline&& other) noexcept {
        if (this != &other) {
            ::stucanvas_free_outline_members(this); // 先行深度释放自己旧的内部数据 [1.1.2]
            geometries = other.geometries;
            instances = other.instances;
            other.geometries = { nullptr, 0, 0 };
            other.instances = { nullptr, 0, 0 };
        }
        return *this;
    }
};

/// FFI 裸复合返回体 [1.2.2]
struct CompileResult {
    Outline* outline;   ///< 成功时指向堆内存中的 Outline，失败时为 nullptr [1.2.2]
    char* error_msg;    ///< 失败时指向编译报错字符串，成功时为 nullptr [1.2.2]
};

// =====================================================================
// 3. 现代 C++ 自动化内存管理包装 (RAII 核心层) [1.1.2]
// =====================================================================
namespace StuCanvas {

/// 专门用于管理 FFI 传出 Outline 指针寿命的智能析构器
struct OutlineDeleter {
    void operator()(Outline* ptr) const noexcept {
        if (ptr) {
            // 💡 提示：智能指针指向的 Outline 是由 Rust 在堆上动态申请的，
            // 必须调用 stucanvas_free_outline 深度回收其内部成员并彻底销毁该 Outline 指针自身 [1.1.2]。
            ::stucanvas_free_outline(ptr);
        }
    }
};

/// 专门用于管理 FFI 传出报错 String 指针寿命的智能析构器
struct StringDeleter {
    void operator()(char* ptr) const noexcept {
        if (ptr) {
            ::stucanvas_free_string(ptr);
        }
    }
};

/// 现代 C++ 托管型智能指针定义
using OutlineUniquePtr = std::unique_ptr<Outline, OutlineDeleter>;
using FfiStringUniquePtr = std::unique_ptr<char, StringDeleter>;

/// 编译结果高层 RAII 托管类 (具备自动、安全的双向生命周期析构能力)
class Result {
public:
    // 强制独占移动语义 (与 unique_ptr 契合，防止浅拷贝导致二次释放)
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    ~Result() = default; // 自动调用独特析构器，无内存泄露风险

    /// 判断编译是否成功
    bool is_success() const noexcept { return m_outline != nullptr; }

    /// 判断编译是否失败
    bool is_error() const noexcept { return m_error_msg != nullptr; }

    /// 安全获取矢量数据根节点只读指针
    const Outline* get_outline() const noexcept { return m_outline.get(); }

    /// 安全获取零拷贝式错误文本视图（无错误时返回空视图）
    std::string_view get_error() const noexcept {
        return m_error_msg ? std::string_view(m_error_msg.get()) : std::string_view();
    }

    /// 支持类似 std::optional 的快捷真值条件测试
    explicit operator bool() const noexcept { return is_success(); }

    /// 提供快捷的一等公民指针访问运算符 ->
    const Outline* operator->() const noexcept { return m_outline.get(); }
    const Outline& operator*() const noexcept { return *m_outline; }

private:
    // 仅允许 compile 函数进行内部构造
    friend Result compile(const char* markup_str, const char* fonts_dir);

    Result(Outline* out, char* err) noexcept
        : m_outline(out), m_error_msg(err) {}

    OutlineUniquePtr m_outline;
    FfiStringUniquePtr m_error_msg;
};

/**
 * @brief 现代 C++ 风格的高层编译入口
 * @return 一个托管好的、作用域结束自动释放物理堆内存的 Result 实例
 */
inline Result compile(const char* markup_str, const char* fonts_dir) {
    CompileResult raw = ::stucanvas_compile_typst(markup_str, fonts_dir);
    return Result(raw.outline, raw.error_msg);
}

} // namespace StuCanvas

// =====================================================================
// 4. 静态断言 (保证 64 位平台上的 FFI 对齐万无一失) [1]
// =====================================================================
#ifdef __cplusplus
static_assert(sizeof(Point2D) == 16, "Mismatched size of Point2D");
static_assert(sizeof(RGBA) == 16, "Mismatched size of RGBA");
static_assert(sizeof(Transform2D) == 48, "Mismatched size of Transform2D");
static_assert(sizeof(CVector<double>) == 24, "Mismatched size of CVec");
// 💡 升级：精简无效几何并融入五法则后，SharedGeometry 在 64 位平台对齐大小已精确固定为 64 字节
static_assert(sizeof(SharedGeometry) == 64, "SharedGeometry alignment failure");
static_assert(sizeof(CompileResult) == 16, "Mismatched size of CompileResult");
#endif