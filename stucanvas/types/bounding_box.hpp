#pragma once
#include "point.hpp"

namespace StuCanvas
{
    template <typename T>
    struct BoundingBox2D
    {
        Point2D<T> min;
        Point2D<T> max;

        Point2D<T> Size() const { return {max.x - min.x, max.y - min.y}; }
        Point2D<T> Center() const { return {min.x + (max.x - min.x) / 2, min.y + (max.y - min.y) / 2}; }
    };

    template <typename T>
    struct BoundingBox3D
    {
        Point3D<T> min;
        Point3D<T> max;

        Point3D<T> Size() const { return {max.x - min.x, max.y - min.y, max.z - min.z}; }

        Point3D<T> Center() const
        {
            return {min.x + (max.x - min.x) / 2, min.y + (max.y - min.y) / 2, min.z + (max.z - min.z) / 2};
        }
    };
}
