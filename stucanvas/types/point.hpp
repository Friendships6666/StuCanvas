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

        // 恢复平凡默认构造函数，实现 POD 核心要求
        Point2D() = default;

        // 移除默认参数，将其纯粹作为带参构造函数
        constexpr Point2D(T _x, T _y) : x(_x), y(_y) {}
    };

    // ==========================================
    // 2. Point3D (纯几何 3D)
    // ==========================================
    template <typename T>
    struct Point3D
    {
        using value_type = T;
        T x, y, z;

        Point3D() = default;

        constexpr Point3D(T _x, T _y, T _z) : x(_x), y(_y), z(_z) {}

        // 维度提升：从 2D 点构造 3D 点 (z = 0)
        constexpr Point3D(const Point2D<T>& p2d)
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
        // 注意：移除这里的 "= 1.0f" 默认初始化，保证内存布局的纯粹性
        float r{}, g{}, b{}, a{};

        ColorPoint2D() = default;

        // 在构造函数中补回默认参数
        constexpr ColorPoint2D(T _x, T _y, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(_x), y(_y), r(_r), g(_g), b(_b), a(_a) {}

        // 从普通 2D 点构造颜色点
        constexpr ColorPoint2D(const Point2D<T>& p, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p.x), y(p.y), r(_r), g(_g), b(_b), a(_a) {}

        // 显式转换为普通 2D 点（丢失颜色数据）
        explicit constexpr operator Point2D<T>() const { return { x, y }; }
    };

    // ==========================================
    // 4. ColorPoint3D (展开颜色分量的 3D 点)
    // ==========================================
    template <typename T>
    struct ColorPoint3D
    {
        using value_type = T;
        T x, y, z;
        float r{}, g{}, b{}, a{};

        ColorPoint3D() = default;

        constexpr ColorPoint3D(T _x, T _y, T _z, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(_x), y(_y), z(_z), r(_r), g(_g), b(_b), a(_a) {}

        // 从普通 3D 点构造颜色点
        constexpr ColorPoint3D(const Point3D<T>& p, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p.x), y(p.y), z(p.z), r(_r), g(_g), b(_b), a(_a) {}

        // 维度提升：从 ColorPoint2D 构造 (z = 0, 继承颜色)
        constexpr ColorPoint3D(const ColorPoint2D<T>& p2d)
            : x(p2d.x), y(p2d.y), z(static_cast<T>(0)), r(p2d.r), g(p2d.g), b(p2d.b), a(p2d.a) {}

        // 维度提升：从普通 2D 点构造
        constexpr ColorPoint3D(const Point2D<T>& p2d, float _r = 1.0f, float _g = 1.0f, float _b = 1.0f, float _a = 1.0f)
            : x(p2d.x), y(p2d.y), z(static_cast<T>(0)), r(_r), g(_g), b(_b), a(_a) {}

        // 显式转换为普通 3D 点（丢失颜色数据）
        explicit constexpr operator Point3D<T>() const { return { x, y, z }; }
    };




    // 带颜色的 2D 点 – Vulkan 存储缓冲区标准布局
    struct alignas(16) Point2D_GPU {
        float x, y;              // 位置 (8 字节)
        float r, g, b, a;        // 颜色 (16 字节，对齐 16)
        float _pad[2];           // 填充至 32 字节，保证数组元素起始对齐 16
    };
    // 编译期验证大小
    static_assert(sizeof(Point2D_GPU) == 32, "Point2D_GPU must be 32 bytes");

    // 带颜色的 3D 点 – Vulkan 存储缓冲区标准布局
    struct alignas(16) Point3D_GPU {
        float x, y, z;           // 位置 (12 字节)
        float r, g, b, a;        // 颜色 (16 字节)
        float _pad[1];           // 填充至 32 字节，保证数组元素起始对齐 16
    };
    static_assert(sizeof(Point3D_GPU) == 32, "Point3D_GPU must be 32 bytes");

} // namespace StuCanvas