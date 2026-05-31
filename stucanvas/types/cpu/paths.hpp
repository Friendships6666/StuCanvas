#pragma once
#include <vector>
#include "point.hpp"
namespace StuCanvas
{

    template <typename T>
    using QuadraticBezier2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using QuadraticBezier3D_CPU = std::vector<Point3D_CPU<T>>;


    template <typename T>
    using CubicBezier2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using CubicBezier3D_CPU = std::vector<Point3D_CPU<T>>;


    template <typename T>
    using CubicBezier2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using CubicBezier3D_CPU = std::vector<Point3D_CPU<T>>;


    template <typename T>
    using QuadraticBasis2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using QuadraticBasis3D_CPU = std::vector<Point3D_CPU<T>>;


    template <typename T>
    using CubicBasis2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using CubicBasis3D_CPU = std::vector<Point3D_CPU<T>>;


    template <typename T>
    using CatmullRom2D_CPU = std::vector<Point2D_CPU<T>>;
    template <typename T>
    using CatmullRom3D_CPU = std::vector<Point3D_CPU<T>>;





}
