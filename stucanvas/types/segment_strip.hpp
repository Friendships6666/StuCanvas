// stucanvas/types/segment_strip.hpp
#pragma once

#include <vector>
#include "../types/point.hpp"

namespace StuCanvas {

    /// 由连续线段组成的2D折线带
    /// - 如果 closed == false，vertices 构成了一个开折线
    /// - 如果 closed == true，首尾顶点相连形成闭合环
    template <typename T>
    struct SegmentStrip2D {
        using Scalar = T;
        std::vector<Point2D<T>> vertices;
        bool closed = false;

        SegmentStrip2D() = default;
        SegmentStrip2D(std::vector<Point2D<T>> verts, bool is_closed = false)
            : vertices(std::move(verts)), closed(is_closed) {}
    };

} // namespace StuCanvas