#pragma once
#include <cstdint>
#include <vector>

#include "point.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Triangles2D_CPU
    {
        std::vector<Point2D_CPU<T>> vertices;
        std::vector<uint32_t> indices;
    };

    template <typename T>
    struct Triangles3D_CPU
    {
        std::vector<Point3D_CPU<T>> vertices;
        std::vector<uint32_t> indices;
    };
}
