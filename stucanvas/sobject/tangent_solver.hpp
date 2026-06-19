// stucanvas/sobject/tangent_solver.hpp
#pragma once

#include <cmath>
#include <limits>
#include "graph.hpp"

namespace StuCanvas
{

    // ========================================================================
    // 2D 切线 — 通过离散化点云找到切点附近两点，计算斜率
    // ========================================================================
    template <typename T>
    inline void SolveTangent2D(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        const SObject<T>* curve = node.parents[0];
        const SObject<T>* pt    = node.parents[1];

        T px = pt->data.point_2d.x, py = pt->data.point_2d.y;

        // 💡 调用目标对象的离散化点云
        if (curve->vptr && curve->vptr->discretize_to_points)
            curve->vptr->discretize_to_points(graph, *const_cast<SObject<T>*>(curve));

        auto it = graph.points_2d.find(curve);
        if (it == graph.points_2d.end() || it->second.size() < 2)
        {
            // 无法计算切线，退化为零长度
            node.data.line_2d.x0 = px;
            node.data.line_2d.y0 = py;
            node.data.line_2d.x1 = px;
            node.data.line_2d.y1 = py;
            return;
        }

        const auto& pts = it->second;

        // 💡 找到离切点最近的两个采样点
        size_t nearest = 0;
        T best = std::numeric_limits<T>::max();
        for (size_t i = 0; i < pts.size(); ++i)
        {
            T d = (pts[i].x - px)*(pts[i].x - px) + (pts[i].y - py)*(pts[i].y - py);
            if (d < best) { best = d; nearest = i; }
        }

        size_t prev = (nearest == 0) ? 1 : nearest - 1;
        size_t next = (nearest == pts.size() - 1) ? nearest - 1 : nearest + 1;

        // 💡 取前后两点中离切点更远的一个做方向计算（避免退化）
        T d_prev = (pts[prev].x - px)*(pts[prev].x - px) + (pts[prev].y - py)*(pts[prev].y - py);
        T d_next = (pts[next].x - px)*(pts[next].x - px) + (pts[next].y - py)*(pts[next].y - py);
        size_t far = (d_prev > d_next) ? prev : next;

        T dx = pts[far].x - pts[nearest].x;
        T dy = pts[far].y - pts[nearest].y;
        T len = static_cast<T>(std::sqrt(dx*dx + dy*dy));
        if (len < static_cast<T>(1e-10))
        {
            node.data.line_2d.x0 = px;
            node.data.line_2d.y0 = py;
            node.data.line_2d.x1 = px;
            node.data.line_2d.y1 = py;
            return;
        }

        // 💡 切线方向，延长 10 倍显示
        T scale = static_cast<T>(10);
        node.data.line_2d.x0 = px;
        node.data.line_2d.y0 = py;
        node.data.line_2d.x1 = px + dx / len * scale;
        node.data.line_2d.y1 = py + dy / len * scale;
    }

    // ========================================================================
    // 3D 切线 — 暂留空，无支持对象
    // ========================================================================
    template <typename T>
    inline void SolveTangent3D(SObjectGraph<T>&, SObject<T>&) noexcept
    {
        // 💡 暂不支持
    }

} // namespace StuCanvas
