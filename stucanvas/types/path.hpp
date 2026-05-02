// stucanvas/types/path.hpp
#pragma once

#include <vector>
#include "point.hpp"

namespace StuCanvas {

    template <typename T>
    struct Path2D {
        using Point = Point2D<T>;

        // 控制点序列：每 4 个连续点构成一段三阶贝塞尔（起点、控制点1、控制点2、终点）
        std::vector<Point> control_points;
    };

    template <typename T>
    struct Path3D {
        using Point = Point3D<T>;

        std::vector<Point> control_points;
    };

} // namespace StuCanvas