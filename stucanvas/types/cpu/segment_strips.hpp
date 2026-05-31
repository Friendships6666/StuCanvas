#pragma once
#include <vector>
#include "point.hpp"
namespace StuCanvas
{

    template <typename T>
    using SegmentStrips2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using SegmentStrips3D_CPU = std::vector<Point3D_CPU<T>>;



}
