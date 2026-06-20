#pragma once
#include <vector>
#include <utility>
#include "../utils/interval.hpp"
namespace StuCanvas
{
    // ==========================================
    // 🚀 统一以 Asset 为前缀的离散数据包裹结构体
    // ==========================================

    // 1. fonts_2d
    struct AssetFonts2D {
        Outline value;
        template <typename... Args>
        explicit AssetFonts2D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 2. points_2d
    template <typename T>
    struct AssetPoints2D {
        std::vector<Point2D_CPU<T>> value;
        template <typename... Args>
        explicit AssetPoints2D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 3. points_3d
    template <typename T>
    struct AssetPoints3D {
        std::vector<Point3D_CPU<T>> value;
        template <typename... Args>
        explicit AssetPoints3D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 4. segment_stips_2d
    template <typename T>
    struct AssetSegmentStrips2D {
        SegmentStrips2D_CPU<T> value;
        template <typename... Args>
        explicit AssetSegmentStrips2D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 5. segment_stips_3d
    template <typename T>
    struct AssetSegmentStrips3D {
        SegmentStrips3D_CPU<T> value;
        template <typename... Args>
        explicit AssetSegmentStrips3D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 6. triangles_2d
    template <typename T>
    struct AssetTriangles2D {
        Triangles2D_CPU<T> value;
        template <typename... Args>
        explicit AssetTriangles2D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 7. triangles_3d
    template <typename T>
    struct AssetTriangles3D {
        Triangles3D_CPU<T> value;
        template <typename... Args>
        explicit AssetTriangles3D(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 8. visual_points
    template <typename T>
    struct AssetVisualPoints {
        std::vector<Point2D_CPU<T>> value;
        template <typename... Args>
        explicit AssetVisualPoints(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 9. visual_segments
    template <typename T>
    struct AssetVisualSegments {
        SegmentStrips2D_CPU<T> value;
        template <typename... Args>
        explicit AssetVisualSegments(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // 10. visual_triangles
    template <typename T>
    struct AssetVisualTriangles {
        std::vector<Point3D_CPU<T>> value;
        template <typename... Args>
        explicit AssetVisualTriangles(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // ==========================================
    // 🚀 三个步长资产也同步更改为 Asset 前缀
    // ==========================================
    template <typename T>
    struct AssetStepPoints {
        T value;
        explicit AssetStepPoints(T v) noexcept : value(v) {}
    };

    template <typename T>
    struct AssetStepStrips {
        T value;
        explicit AssetStepStrips(T v) noexcept : value(v) {}
    };

    template <typename T>
    struct AssetStepTriangles {
        T value;
        explicit AssetStepTriangles(T v) noexcept : value(v) {}
    };


    // ==========================================
    // 🚀 1. 标量数学函数资产 (AssetScalar)
    // ==========================================
    template <typename T>
    struct AssetScalarUnary {
        utils::FfiFunction<T(T)> value;
        template <typename... Args>
        explicit AssetScalarUnary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetScalarBinary {
        utils::FfiFunction<T(T, T)> value;
        template <typename... Args>
        explicit AssetScalarBinary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetScalarTernary {
        utils::FfiFunction<T(T, T, T)> value;
        template <typename... Args>
        explicit AssetScalarTernary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // ==========================================
    // 🚀 2. 积分数学函数资产 (AssetIntegral)
    // ==========================================
    template <typename T>
    struct AssetIntegralUnary {
        utils::FfiFunction<T(T)> value;
        template <typename... Args>
        explicit AssetIntegralUnary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetIntegralBinary {
        utils::FfiFunction<T(T, T)> value;
        template <typename... Args>
        explicit AssetIntegralBinary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetIntegralTernary {
        utils::FfiFunction<T(T, T, T)> value;
        template <typename... Args>
        explicit AssetIntegralTernary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // ==========================================
    // 🚀 3. 导数/微分数学函数资产 (AssetDerivative)
    // ==========================================
    template <typename T>
    struct AssetDerivativeUnary {
        utils::FfiFunction<T(T)> value;
        template <typename... Args>
        explicit AssetDerivativeUnary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetDerivativeBinary {
        utils::FfiFunction<T(T, T)> value;
        template <typename... Args>
        explicit AssetDerivativeBinary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetDerivativeTernary {
        utils::FfiFunction<T(T, T, T)> value;
        template <typename... Args>
        explicit AssetDerivativeTernary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    // ==========================================
    // 🚀 4. 区间运算数学函数资产 (AssetInterval)
    // ==========================================
    template <typename T>
    struct AssetIntervalUnary {
        utils::FfiFunction<IntervalSet<T>(Interval<T>)> value;
        template <typename... Args>
        explicit AssetIntervalUnary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetIntervalBinary {
        utils::FfiFunction<IntervalSet<T>(Interval<T>, Interval<T>)> value;
        template <typename... Args>
        explicit AssetIntervalBinary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template <typename T>
    struct AssetIntervalTernary {
        utils::FfiFunction<IntervalSet<T>(Interval<T>, Interval<T>, Interval<T>)> value;
        template <typename... Args>
        explicit AssetIntervalTernary(Args&&... args) : value(std::forward<Args>(args)...) {}
    };


} // namespace StuCanvas