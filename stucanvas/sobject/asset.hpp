#pragma once
#include <cstdint>
#include "tiny_vector.hpp"
#include "block_deque.hpp"

namespace StuCanvas
{

        // utils::FlatMap<const SObject<T>*, Outline> fonts_2d;
        // utils::FlatMap<const SObject<T>*, std::vector<Point2D_CPU<T>>> points_2d;
        // utils::FlatMap<const SObject<T>*, std::vector<Point3D_CPU<T>>> points_3d;
        // utils::FlatMap<const SObject<T>*, SegmentStrips2D_CPU<T>> segment_stips_2d;
        // utils::FlatMap<const SObject<T>*, SegmentStrips3D_CPU<T>> segment_stips_3d;
        // utils::FlatMap<const SObject<T>*, Triangles2D_CPU<T>> triangles_2d;
        // utils::FlatMap<const SObject<T>*, Triangles3D_CPU<T>> triangles_3d;
        // utils::FlatMap<const SObject<T>*, std::vector<Point2D_CPU<T>>> visual_points;
        // utils::FlatMap<const SObject<T>*, SegmentStrips2D_CPU<T>> visual_segments;
        // utils::FlatMap<const SObject<T>*, std::vector<Point3D_CPU<T>>> visual_triangles;
        //
        //
        //
        // // 1. 标量函数 (Scalar Functions)
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T)>>       scalar_unary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T)>>    scalar_binary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T,T)>> scalar_ternary_funcs;
        //
        // // 2. 积分函数 (Integral Functions)
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T)>>       integral_unary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T)>>    integral_binary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T,T)>> integral_ternary_funcs;
        //
        // // 3. 导数/微分函数 (Derivative Functions)
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T)>>       derivative_unary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T)>>    derivative_binary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<T(T,T,T)>> derivative_ternary_funcs;
        //
        // // 4. 区间函数 (Interval Functions)
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<IntervalSet<T>(Interval<T>)>>       interval_unary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<IntervalSet<T>(Interval<T>,Interval<T>)>>    interval_binary_funcs;
        // utils::FlatMap<const SObject<T>*, utils::FfiFunction<IntervalSet<T>(Interval<T>,Interval<T>,Interval<T>)>> interval_ternary_funcs;


    enum class SObjectAssetType : uint32_t
    {
        UNKNOWN,
        TPYST_OUTLINES,
        POINTS_2D,
        POINTS_3D,
        STRIPS_2D,
        STRIPS_3D,
        TRIANGLES_2D,
        TRIANGLES_3D,

    };


    struct SObjectAssetHeader
    {

    };
}
