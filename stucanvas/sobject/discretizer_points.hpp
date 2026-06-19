// stucanvas/sobject/discretizer_points.hpp
#pragma once

#include <cmath>
#include <vector>
#include <algorithm>
#include "graph.hpp"

namespace StuCanvas
{
    namespace detail
    {
        // ====================================================================
        // 💡 机制复用 1：统一的轴向 Slab 裁剪辅助函数
        // ====================================================================
        template <typename T>
        inline void ClipAxis(T p0, T d, T bmin, T bmax, T& t_enter, T& t_exit) noexcept
        {
            if (std::abs(d) > static_cast<T>(1e-15)) {
                T t0 = (bmin - p0) / d;
                T t1 = (bmax - p0) / d;
                if (t0 > t1) std::swap(t0, t1);
                t_enter = (std::max)(t_enter, t0);
                t_exit  = (std::min)(t_exit,  t1);
            } else if (p0 < bmin || p0 > bmax) {
                t_exit = static_cast<T>(-1); // 视口外，强制标记为无效区间
            }
        }

        // ====================================================================
        // 💡 机制复用 2：统一的 2D 离散化与裁剪引擎
        // ====================================================================
        template <typename T>
        inline void DiscretizeLine2D_Internal(
            SObjectGraph<T>& graph,
            SObject<T>& node,
            T t_enter,
            T t_exit,
            bool should_clip
        ) noexcept {
            T x0 = node.data.line_2d.x0, y0 = node.data.line_2d.y0;
            T x1 = node.data.line_2d.x1, y1 = node.data.line_2d.y1;
            T dx = x1 - x0, dy = y1 - y0;
            T len = static_cast<T>(std::sqrt(dx * dx + dy * dy));

            // 如果是直线/射线，启用边界裁剪
            if (should_clip) {
                ClipAxis(x0, dx, graph.world_xmin, graph.world_xmax, t_enter, t_exit);
                ClipAxis(y0, dy, graph.world_ymin, graph.world_ymax, t_enter, t_exit);
                if (t_enter > t_exit) return; // 裁剪后完全不可见，直接退出
            }

            T step = node.discretization_step_points;
            T clipped_len = len * (t_exit - t_enter);
            int n = static_cast<int>(clipped_len / step) + 1;

            std::vector<Point2D_CPU<T>> pts;
            pts.reserve(static_cast<size_t>(n));

            // 从世界坐标系下的 t_enter 开始顺序离散化
            T step_ratio = step / len;
            T step_x = dx * step_ratio;
            T step_y = dy * step_ratio;
            T base_x = x0 + dx * t_enter;
            T base_y = y0 + dy * t_enter;

            for (int i = 0; i < n; ++i) {
                T t = static_cast<T>(i);
                pts.push_back({base_x + step_x * t, base_y + step_y * t});
            }
            graph.points_2d.insert(&node, std::move(pts));
        }

        // ====================================================================
        // 💡 机制复用 3：统一的 3D 离散化与裁剪引擎
        // ====================================================================
        template <typename T>
        inline void DiscretizeLine3D_Internal(
            SObjectGraph<T>& graph,
            SObject<T>& node,
            T t_enter,
            T t_exit,
            bool should_clip
        ) noexcept {
            T x0 = node.data.line_3d.x0, y0 = node.data.line_3d.y0, z0 = node.data.line_3d.z0;
            T x1 = node.data.line_3d.x1, y1 = node.data.line_3d.y1, z1 = node.data.line_3d.z1;
            T dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;

            if (should_clip) {
                ClipAxis(x0, dx, graph.world_xmin, graph.world_xmax, t_enter, t_exit);
                ClipAxis(y0, dy, graph.world_ymin, graph.world_ymax, t_enter, t_exit);
                ClipAxis(z0, dz, graph.world_zmin, graph.world_zmax, t_enter, t_exit);
                if (t_enter > t_exit) return;
            }

            T len = static_cast<T>(std::sqrt(dx * dx + dy * dy + dz * dz));
            T step = node.discretization_step_points;
            T clipped_len = len * (t_exit - t_enter);
            int n = static_cast<int>(clipped_len / step) + 1;

            std::vector<Point3D_CPU<T>> pts;
            pts.reserve(static_cast<size_t>(n));

            T step_ratio = step / len;
            T step_x = dx * step_ratio;
            T step_y = dy * step_ratio;
            T step_z = dz * step_ratio;
            T base_x = x0 + dx * t_enter;
            T base_y = y0 + dy * t_enter;
            T base_z = z0 + dz * t_enter;

            for (int i = 0; i < n; ++i) {
                T t = static_cast<T>(i);
                pts.push_back({base_x + step_x * t, base_y + step_y * t, base_z + step_z * t});
            }
            graph.points_3d.insert(&node, std::move(pts));
        }
    } // namespace detail

    // ========================================================================
    // 2D 几何图元公开接口（通过参数组合无开销委托）
    // ========================================================================

    template <typename T>
    inline void DiscretizeLine2DSegment_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        // 线段：区间 [0, 1]，无需视口裁剪
        detail::DiscretizeLine2D_Internal(graph, node, static_cast<T>(0), static_cast<T>(1), false);
    }

    template <typename T>
    inline void DiscretizeLine2DStraight_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        // 直线：无限区间，需要视口裁剪
        detail::DiscretizeLine2D_Internal(graph, node, static_cast<T>(-1e30), static_cast<T>(1e30), true);
    }

    template <typename T>
    inline void DiscretizeLine2DRay_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        // 射线：半无限区间 [0, inf]，需要视口裁剪
        detail::DiscretizeLine2D_Internal(graph, node, static_cast<T>(0), static_cast<T>(1e30), true);
    }

    // ========================================================================
    // 3D 几何图元公开接口（通过参数组合无开销委托）
    // ========================================================================

    template <typename T>
    inline void DiscretizeLine3DSegment_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        detail::DiscretizeLine3D_Internal(graph, node, static_cast<T>(0), static_cast<T>(1), false);
    }

    template <typename T>
    inline void DiscretizeLine3DStraight_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        detail::DiscretizeLine3D_Internal(graph, node, static_cast<T>(-1e30), static_cast<T>(1e30), true);
    }

    template <typename T>
    inline void DiscretizeLine3DRay_Points(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        detail::DiscretizeLine3D_Internal(graph, node, static_cast<T>(0), static_cast<T>(1e30), true);
    }
} // namespace StuCanvas