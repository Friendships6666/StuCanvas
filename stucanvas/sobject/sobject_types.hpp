/***************************************************************************
* This file is AUTO-GENERATED from JSON. DO NOT MODIFY.                    *
***************************************************************************/
#pragma once
#include <cstdint>
#include <type_traits>

namespace StuCanvas {

    // ==========================================
    // 64位高能位图类型标识 (支持多类别并存与编译期聚类检查)
    // ==========================================
    enum class NodeType : uint64_t {
        // ---- 核心检索掩码 ----
        MASK_WORLD    = 0xFF00'0000'0000'0000ULL,
        MASK_CATEGORY = 0x00FF'0000'0000'0000ULL,
        MASK_SPECIFIC = 0x0000'00FF'FFFF'FFFFULL,

        // ---- 空间维度区间 ----
        WORLD_2D              = 0x0100000000000000ULL,
        WORLD_3D              = 0x0200000000000000ULL,
        WORLD_META            = 0x0400000000000000ULL,

        // ---- 聚类大分类 (16位独立位标志) ----
        CAT_POINT             = 0x0000010000000000ULL,
        CAT_LINEAR            = 0x0000020000000000ULL,
        CAT_CURVE             = 0x0000040000000000ULL,
        CAT_SURFACE           = 0x0000080000000000ULL,
        CAT_FUNCTION          = 0x0000100000000000ULL,
        CAT_SCALAR            = 0x0000200000000000ULL,
        CAT_TEST              = 0x0000400000000000ULL,

        // ---- 具体物理图元类型 (支持多类别并存) ----
        POINT_2D_FREE         = 0x0100010000000001ULL,
        POINT_2D_MID          = 0x0100010000000002ULL,
        POINT_2D_SNAP         = 0x0100010000000003ULL,
        POINT_2D_SECTION      = 0x0100010000000004ULL,
        POINT_2D_INTERSECT    = 0x0100010000000005ULL,
        LINE_2D_SEGMENT       = 0x0100020000000001ULL,
        LINE_2D_STRAIGHT      = 0x0100020000000002ULL,
        LINE_2D_RAY           = 0x0100020000000003ULL,
        LINE_2D_PERPENDICULAR = 0x0100020000000004ULL,
        LINE_2D_PARALLEL      = 0x0100020000000005ULL,
        CIRCLE_2D             = 0x0100050000000001ULL,
        ARC_2D                = 0x0100040000000002ULL,
        ELLIPSE_2D            = 0x0100050000000003ULL,
        HYPERBOLA_2D          = 0x0100050000000004ULL,
        PARABOLA_2D           = 0x0100050000000005ULL,
        POLY_2D_GENERATIVE    = 0x0100070000000006ULL,
        POINT_3D_FREE         = 0x0200010000000001ULL,
        POINT_3D_MID          = 0x0200010000000002ULL,
        POINT_3D_SNAP         = 0x0200010000000003ULL,
        POINT_3D_SECTION      = 0x0200010000000004ULL,
        POINT_3D_INTERSECT    = 0x0200010000000005ULL,
        LINE_3D_SEGMENT       = 0x0200020000000001ULL,
        LINE_3D_STRAIGHT      = 0x0200020000000002ULL,
        LINE_3D_RAY           = 0x0200020000000003ULL,
        LINE_3D_PERPENDICULAR = 0x0200020000000004ULL,
        LINE_3D_PARALLEL      = 0x0200020000000005ULL,
        PLANE_3D              = 0x02000A0000000001ULL,
        PLANE_3D_PARALLEL     = 0x02000A0000000002ULL,
        PLANE_3D_PERPENDICULAR = 0x02000A0000000003ULL,
        SPHERE_3D             = 0x0200090000000004ULL,
        CYLINDER_3D           = 0x0200080000000005ULL,
        CONE_3D               = 0x0200080000000006ULL,
        CUBOID_3D             = 0x0200080000000007ULL,
        PLATONIC_SOLID_3D     = 0x0200080000000008ULL,
        CIRCLE_3D             = 0x0200050000000001ULL,
        ARC_3D                = 0x0200040000000002ULL,
        ELLIPSE_3D            = 0x0200050000000003ULL,
        HYPERBOLA_3D          = 0x0200050000000004ULL,
        PARABOLA_3D           = 0x0200050000000005ULL,
        POLY_3D_GENERATIVE    = 0x0200070000000006ULL,
        SCALAR                = 0x0400200000000001ULL,
        FUNC_EXPLICIT_2D      = 0x0400100000000001ULL,
        FUNC_EXPLICIT_3D      = 0x0400100000000002ULL,
        FUNC_IMPLICIT_2D      = 0x0400100000000003ULL,
        FUNC_IMPLICIT_3D      = 0x0400100000000004ULL,
        FUNC_PARAMETRIC_2D    = 0x0400100000000005ULL,
        FUNC_PARAMETRIC_3D    = 0x0400100000000006ULL,
        UNKNOWN               = 0x040001000000FFFFULL,
    };

    // ==========================================
    // 零运行时开销的编译期聚类自检 (支持多类别交集检测)
    // ==========================================
    [[nodiscard]] constexpr bool is_2d(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::MASK_WORLD)) == static_cast<uint64_t>(NodeType::WORLD_2D);
    }

    [[nodiscard]] constexpr bool is_3d(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::MASK_WORLD)) == static_cast<uint64_t>(NodeType::WORLD_3D);
    }

    [[nodiscard]] constexpr bool is_point(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_POINT)) != 0;
    }

    [[nodiscard]] constexpr bool is_linear(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_LINEAR)) != 0;
    }

    [[nodiscard]] constexpr bool is_curve(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_CURVE)) != 0;
    }

    [[nodiscard]] constexpr bool is_surface(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_SURFACE)) != 0;
    }

    [[nodiscard]] constexpr bool is_function(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_FUNCTION)) != 0;
    }

    [[nodiscard]] constexpr bool is_scalar(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_SCALAR)) != 0;
    }

    [[nodiscard]] constexpr bool is_test(NodeType t) noexcept {
        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::CAT_TEST)) != 0;
    }

    // ========================================================
    // 编译期静态虚函数表 (VTable) 内部模板定义 (支持按需延迟解算)
    // ========================================================
    template <typename T> struct SObjectGraph;
    template <typename T> struct SObject;
    template <typename T>
    struct SObjectVTable
    {
    void (*solver)(SObjectGraph<T>&, SObject<T>&) = nullptr;
    void (*discretize_to_points)(SObjectGraph<T>&, SObject<T>&) = nullptr;
    void (*discretize_to_strips)(SObjectGraph<T>&, SObject<T>&) = nullptr;
    void (*discretize_to_paths)(SObjectGraph<T>&, SObject<T>&) = nullptr;
    void (*discretize_to_triangles)(SObjectGraph<T>&, SObject<T>&) = nullptr;
    };

    // ---- 物理存在的解算与离散化模板函数前向声明 ----
    template <typename T> void SolveLine2DRay(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolveLine2DSegment(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolveLine2DStraight(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolveLine3DRay(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolveLine3DSegment(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolveLine3DStraight(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolvePlane3D(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolvePoint2DMid(SObjectGraph<T>&, SObject<T>&);
    template <typename T> void SolvePoint3DMid(SObjectGraph<T>&, SObject<T>&);

    // ---- 自动拼装完毕的 C++17 内联全局虚表定义 (声明与定义合一) ----
    template <typename T>
    inline const SObjectVTable<T> Point2DFree_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point2DMid_VTable = {
        .solver = &SolvePoint2DMid<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point2DSnap_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point2DSection_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point2DIntersect_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line2DSegment_VTable = {
        .solver = &SolveLine2DSegment<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line2DStraight_VTable = {
        .solver = &SolveLine2DStraight<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line2DRay_VTable = {
        .solver = &SolveLine2DRay<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line2DPerpendicular_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line2DParallel_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Circle2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Arc2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Ellipse2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Hyperbola2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Parabola2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Poly2DGenerative_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point3DFree_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point3DMid_VTable = {
        .solver = &SolvePoint3DMid<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point3DSnap_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point3DSection_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Point3DIntersect_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line3DSegment_VTable = {
        .solver = &SolveLine3DSegment<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line3DStraight_VTable = {
        .solver = &SolveLine3DStraight<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line3DRay_VTable = {
        .solver = &SolveLine3DRay<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line3DPerpendicular_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Line3DParallel_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Plane3D_VTable = {
        .solver = &SolvePlane3D<T>,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Plane3DParallel_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Plane3DPerpendicular_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Sphere3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Cylinder3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Cone3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Cuboid3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> PlatonicSolid3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Circle3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Arc3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Ellipse3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Hyperbola3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Parabola3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Poly3DGenerative_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Scalar_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncExplicit2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncExplicit3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncImplicit2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncImplicit3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncParametric2D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> FuncParametric3D_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

    template <typename T>
    inline const SObjectVTable<T> Unknown_VTable = {
        .solver = nullptr,
        .discretize_to_points = nullptr,
        .discretize_to_strips = nullptr,
        .discretize_to_triangles = nullptr,
        .discretize_to_paths = nullptr
    };

} // namespace StuCanvas