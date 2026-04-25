/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <cstdint>

namespace StuCanvas
{
    // --- 前向声明 ---
    template <typename T> struct Point2D;
    template <typename T> struct Point3D;
    template <typename T> struct ColorPoint2D;
    template <typename T> struct ColorPoint3D;

    // ==========================================
    // 1. Point2D (纯几何 2D)
    // ==========================================
    template <typename T>
    struct Point2D
    {
        using value_type = T;
        T x, y;

        Point2D(T _x = 0, T _y = 0) : x(_x), y(_y) {}
    };

    // ==========================================
    // 2. Point3D (纯几何 3D)
    // ==========================================
    template <typename T>
    struct Point3D
    {
        using value_type = T;
        T x, y, z;

        Point3D(T _x = 0, T _y = 0, T _z = 0) : x(_x), y(_y), z(_z) {}

        // 维度提升：从 2D 点构造 3D 点 (z = 0)
        Point3D(const Point2D<T>& p2d)
            : x(p2d.x), y(p2d.y), z(static_cast<T>(0)) {}
    };

    // ==========================================
    // 3. ColorPoint2D (展开颜色分量的 2D 点)
    // ==========================================
    template <typename T>
    struct ColorPoint2D
    {
        using value_type = T;
        T x, y;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f; // 颜色分量直接展开

        ColorPoint2D() = default;
        ColorPoint2D(T _x, T _y, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(_x), y(_y), r(_r), g(_g), b(_b), a(_a) {}

        // 从普通 2D 点构造颜色点
        ColorPoint2D(const Point2D<T>& p, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p.x), y(p.y), r(_r), g(_g), b(_b), a(_a) {}

        // 显式转换为普通 2D 点（丢失颜色数据）
        explicit operator Point2D<T>() const { return { x, y }; }
    };

    // ==========================================
    // 4. ColorPoint3D (展开颜色分量的 3D 点)
    // ==========================================
    template <typename T>
    struct ColorPoint3D
    {
        using value_type = T;
        T x, y, z;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f; // 颜色分量直接展开

        ColorPoint3D() = default;
        ColorPoint3D(T _x, T _y, T _z, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(_x), y(_y), z(_z), r(_r), g(_g), b(_b), a(_a) {}

        // 从普通 3D 点构造颜色点
        ColorPoint3D(const Point3D<T>& p, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p.x), y(p.y), z(p.z), r(_r), g(_g), b(_b), a(_a) {}

        // 维度提升：从 ColorPoint2D 构造 (z = 0, 继承颜色)
        ColorPoint3D(const ColorPoint2D<T>& p2d)
            : x(p2d.x), y(p2d.y), z(static_cast<T>(0)), r(p2d.r), g(p2d.g), b(p2d.b), a(p2d.a) {}

        // 维度提升：从普通 2D 点构造
        ColorPoint3D(const Point2D<T>& p2d, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p2d.x), y(p2d.y), z(static_cast<T>(0)), r(_r), g(_g), b(_b), a(_a) {}

        // 显式转换为普通 3D 点（丢失颜色数据）
        explicit operator Point3D<T>() const { return { x, y, z }; }
    };

} // namespace StuCanvas