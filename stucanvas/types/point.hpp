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

    // ==========================================
    // 1. Point2D (纯几何 2D)
    // ==========================================
    template <typename T>
    struct Point2D
    {
        using value_type = T;
        T x, y;

        Point2D() = default;

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
    // GPU 专用浮点坐标 (POD，满足 Vulkan 对齐要求，零拷贝上传)
    // ==========================================

    // 带颜色的 2D 点 – Vulkan 存储缓冲区标准布局
    struct alignas(16) Point2D_GPU {
        float x, y;              // 位置 (8 字节)
        float r, g, b, a;        // 颜色 (16 字节，对齐 16)
        float _pad[2];           // 填充至 32 字节，保证数组元素起始对齐 16
    };
    static_assert(sizeof(Point2D_GPU) == 32, "Point2D_GPU must be 32 bytes");

    // 带颜色的 3D 点 – Vulkan 存储缓冲区标准布局
    struct alignas(16) Point3D_GPU {
        float x, y, z;           // 位置 (12 字节)
        float r, g, b, a;        // 颜色 (16 字节)
        float _pad[1];           // 填充至 32 字节，保证数组元素起始对齐 16
    };
    static_assert(sizeof(Point3D_GPU) == 32, "Point3D_GPU must be 32 bytes");

} // namespace StuCanvas