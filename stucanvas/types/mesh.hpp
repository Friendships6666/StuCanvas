#pragma once
#include <cstdint>
#include <vector>

#include "point.hpp"
namespace StuCanvas
{
    struct RGBA
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        // 静态便捷构造
        static RGBA White() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
        static RGBA Red()   { return {1.0f, 0.0f, 0.0f, 1.0f}; }
        static RGBA Green() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
        static RGBA Blue()  { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    };

    // ==========================================
    // 2D 顶点与网格
    // ==========================================
    template <typename T>
    struct Vertex2D
    {
        Point2D<T> position;
        RGBA color;

        Vertex2D() = default;
        Vertex2D(Point2D<T> p, RGBA c = RGBA::White()) : position(p), color(c) {}
    };

    template <typename T>
    struct Mesh2D
    {
        std::vector<Vertex2D<T>> vertices;
        std::vector<uint32_t> indices; // 每3个索引构成一个三角形

        void AddTriangle(uint32_t i1, uint32_t i2, uint32_t i3)
        {
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }

        void Clear() { vertices.clear(); indices.clear(); }
    };

    // ==========================================
    // 3D 顶点与网格
    // ==========================================
    template <typename T>
    struct Vertex3D
    {
        Point3D<T> position;
        RGBA color;

        Vertex3D() = default;
        Vertex3D(Point3D<T> p, RGBA c = RGBA::White()) : position(p), color(c) {}
    };

    template <typename T>
    struct Mesh3D
    {
        std::vector<Vertex3D<T>> vertices;
        std::vector<uint32_t> indices;

        /**
         * @brief 添加一个三角形面片
         */
        void AddTriangle(uint32_t i1, uint32_t i2, uint32_t i3)
        {
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }

        /**
         * @brief 添加一个四边形面片 (自动拆分为两个三角形)
         * 顺序: v1-v2-v3-v4 逆时针
         */
        void AddQuad(uint32_t i1, uint32_t i2, uint32_t i3, uint32_t i4)
        {
            // 第一个三角形
            AddTriangle(i1, i2, i3);
            // 第二个三角形
            AddTriangle(i1, i3, i4);
        }

        void Clear() { vertices.clear(); indices.clear(); }
    };
}