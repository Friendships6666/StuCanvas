// stucanvas/sobject/snap_solver.hpp
#pragma once

#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>
#include "graph.hpp"

namespace StuCanvas
{
    namespace detail
    {
        // ====================================================================
        // 💡 机制复用 1：通用参数范围限制器（线段 & 射线）
        // ====================================================================
        template <typename T>
        inline T ClampParameter(NodeType type, T t) noexcept
        {
            if (type == NodeType::LINE_2D_SEGMENT || type == NodeType::LINE_3D_SEGMENT) {
                return (std::max)(static_cast<T>(0), (std::min)(t, static_cast<T>(1)));
            } else if (type == NodeType::LINE_2D_RAY || type == NodeType::LINE_3D_RAY) {
                return (std::max)(static_cast<T>(0), t);
            }
            return t;
        }

        // ====================================================================
        // 💡 机制复用 2：通用 2D 点线投影
        // ====================================================================
        template <typename T>
        inline T ProjectPointToLine2D(T gx, T gy, T lx0, T ly0, T dx, T dy) noexcept
        {
            T len2 = dx * dx + dy * dy;
            if (len2 < static_cast<T>(1e-12)) return static_cast<T>(0);
            return ((gx - lx0) * dx + (gy - ly0) * dy) / len2;
        }

        // ====================================================================
        // 💡 机制复用 3：通用 3D 点线投影
        // ====================================================================
        template <typename T>
        inline T ProjectPointToLine3D(T gx, T gy, T gz, T lx0, T ly0, T lz0, T dx, T dy, T dz) noexcept
        {
            T len2 = dx * dx + dy * dy + dz * dz;
            if (len2 < static_cast<T>(1e-12)) return static_cast<T>(0);
            return ((gx - lx0) * dx + (gy - ly0) * dy + (gz - lz0) * dz) / len2;
        }

        // ====================================================================
        // 💡 机制复用 4：通用 2D 离散点集最近邻搜索（每帧重复执行，响应猜测位置滑动）
        // ====================================================================
        template <typename PointType, typename T>
        inline void FindNearestPoint2D(const std::vector<PointType>& pts, T gx, T gy, T& out_x, T& out_y) noexcept
        {
            T best = (std::numeric_limits<T>::max)();
            for (const auto& p : pts) {
                T dx = p.x - gx;
                T dy = p.y - gy;
                T d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    out_x = p.x;
                    out_y = p.y;
                }
            }
        }

        // ====================================================================
        // 💡 机制复用 5：通用 3D 离散点集最近邻搜索（每帧重复执行，响应猜测位置滑动）
        // ====================================================================
        template <typename PointType, typename T>
        inline void FindNearestPoint3D(const std::vector<PointType>& pts, T gx, T gy, T gz, T& out_x, T& out_y, T& out_z) noexcept
        {
            T best = (std::numeric_limits<T>::max)();
            for (const auto& p : pts) {
                T dx = p.x - gx;
                T dy = p.y - gy;
                T dz = p.z - gz;
                T d = dx * dx + dy * dy + dz * dz;
                if (d < best) {
                    best = d;
                    out_x = p.x;
                    out_y = p.y;
                    out_z = p.z;
                }
            }
        }
    } // namespace detail

    // ========================================================================
    // 2D 吸附点
    // ========================================================================
    template <typename T>
    inline void SolvePoint2DSnap(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        const SObject<T>* target = node.parents[0];
        T& lock = node.data.snap_2d.lock;
        T gx = node.data.snap_2d.x;
        T gy = node.data.snap_2d.y;

        // 💡 1. 离散几何体吸附（无参数锁定的不规则图形）：每一帧基于用户最新鼠标猜测位置重新执行滑移吸附
        if (target->type != NodeType::LINE_2D_SEGMENT &&
            target->type != NodeType::LINE_2D_STRAIGHT &&
            target->type != NodeType::LINE_2D_RAY &&
            target->type != NodeType::LINE_2D_PERPENDICULAR &&
            target->type != NodeType::LINE_2D_PARALLEL &&
            target->type != NodeType::TANGENT_2D &&
            target->type != NodeType::CIRCLE_2D &&
            target->type != NodeType::CIRCLE_2D_THREE_POINTS)
        {
            if (target->vptr && target->vptr->discretize_to_points) {
                target->vptr->discretize_to_points(graph, *const_cast<SObject<T>*>(target));
            }
            auto it = graph.points_2d.find(target);
            if (it != graph.points_2d.end() && !it->second.empty()) {
                detail::FindNearestPoint2D(it->second, gx, gy, node.data.snap_2d.x, node.data.snap_2d.y);
            }
            return;
        }

        // 💡 2. 解析几何吸附：首次计算参数锁（只算一次，后续帧直接解算）
        if (lock < static_cast<T>(0))
        {
            if (target->type == NodeType::CIRCLE_2D || target->type == NodeType::CIRCLE_2D_THREE_POINTS) {
                T cx = target->data.circle_2d.cx, cy = target->data.circle_2d.cy;
                lock = static_cast<T>(std::atan2(gy - cy, gx - cx));
            } else {
                T lx0 = target->data.line_2d.x0, ly0 = target->data.line_2d.y0;
                T dx  = target->data.line_2d.x1 - lx0;
                T dy  = target->data.line_2d.y1 - ly0;
                T t = detail::ProjectPointToLine2D(gx, gy, lx0, ly0, dx, dy);
                lock = detail::ClampParameter(target->type, t);
            }
        }

        // 💡 3. 应用参数锁解出本帧位置
        if (target->type == NodeType::CIRCLE_2D || target->type == NodeType::CIRCLE_2D_THREE_POINTS) {
            T cx = target->data.circle_2d.cx, cy = target->data.circle_2d.cy;
            T r  = target->data.circle_2d.r;
            node.data.snap_2d.x = cx + r * static_cast<T>(std::cos(lock));
            node.data.snap_2d.y = cy + r * static_cast<T>(std::sin(lock));
        } else {
            T lx0 = target->data.line_2d.x0, ly0 = target->data.line_2d.y0;
            T dx  = target->data.line_2d.x1 - lx0;
            T dy  = target->data.line_2d.y1 - ly0;
            node.data.snap_2d.x = lx0 + dx * lock;
            node.data.snap_2d.y = ly0 + dy * lock;
        }
    }

    // ========================================================================
    // 3D 吸附点
    // ========================================================================
    template <typename T>
    inline void SolvePoint3DSnap(SObjectGraph<T>& graph, SObject<T>& node) noexcept
    {
        const SObject<T>* target = node.parents[0];
        T& lock_a = node.data.snap_3d.a;
        T& lock_b = node.data.snap_3d.b;
        T gx = node.data.snap_3d.x;
        T gy = node.data.snap_3d.y;
        T gz = node.data.snap_3d.z;

        // 💡 1. 离散几何体吸附（无参数锁定的不规则图形）：每一帧基于用户最新鼠标猜测位置重新执行滑移吸附
        if (target->type != NodeType::LINE_3D_SEGMENT &&
            target->type != NodeType::LINE_3D_STRAIGHT &&
            target->type != NodeType::LINE_3D_RAY &&
            target->type != NodeType::LINE_3D_PERPENDICULAR &&
            target->type != NodeType::LINE_3D_PARALLEL &&
            target->type != NodeType::SPHERE_3D &&
            target->type != NodeType::SPHERE_3D_FOUR_POINTS &&
            target->type != NodeType::CYLINDER_3D)
        {
            if (target->vptr && target->vptr->discretize_to_points) {
                target->vptr->discretize_to_points(graph, *const_cast<SObject<T>*>(target));
            }
            auto it = graph.points_3d.find(target);
            if (it != graph.points_3d.end() && !it->second.empty()) {
                detail::FindNearestPoint3D(it->second, gx, gy, gz, node.data.snap_3d.x, node.data.snap_3d.y, node.data.snap_3d.z);
            }
            return;
        }

        // 💡 2. 解析几何吸附：首次计算参数锁
        if (lock_a < static_cast<T>(0))
        {
            if (target->type == NodeType::SPHERE_3D || target->type == NodeType::SPHERE_3D_FOUR_POINTS) {
                T cx = target->data.sphere_3d.cx, cy = target->data.sphere_3d.cy, cz = target->data.sphere_3d.cz;
                T r  = target->data.sphere_3d.r;
                T dx = gx - cx, dy = gy - cy, dz = gz - cz;
                lock_b = static_cast<T>(std::atan2(dy, dx));
                lock_a = static_cast<T>(std::acos(dz / r));
            }
            else if (target->type == NodeType::CYLINDER_3D) {
                T x0 = target->data.cylinder_3d.x0, y0 = target->data.cylinder_3d.y0, z0 = target->data.cylinder_3d.z0;
                T x1 = target->data.cylinder_3d.x1, y1 = target->data.cylinder_3d.y1, z1 = target->data.cylinder_3d.z1;
                T r  = target->data.cylinder_3d.r;
                T adx = x1 - x0, ady = y1 - y0, adz = z1 - z0;
                T t_axis = detail::ProjectPointToLine3D(gx, gy, gz, x0, y0, z0, adx, ady, adz);
                if (t_axis < static_cast<T>(0)) t_axis = static_cast<T>(0);
                if (t_axis > static_cast<T>(1)) t_axis = static_cast<T>(1);

                T px = x0 + adx * t_axis, py = y0 + ady * t_axis, pz = z0 + adz * t_axis;
                T rdx = gx - px, rdy = gy - py, rdz = gz - pz;
                T rad = static_cast<T>(std::sqrt(rdx*rdx + rdy*rdy + rdz*rdz));
                if (rad > r) {
                    lock_a = t_axis;
                    lock_b = static_cast<T>(std::atan2(rdy, rdx));
                } else {
                    T db = (gx-x0)*(gx-x0)+(gy-y0)*(gy-y0)+(gz-z0)*(gz-z0);
                    T dt = (gx-x1)*(gx-x1)+(gy-y1)*(gy-y1)+(gz-z1)*(gz-z1);
                    lock_a = (db <= dt) ? static_cast<T>(-2) : static_cast<T>(-3);
                    lock_b = static_cast<T>(std::atan2((db <= dt) ? (gy-y0) : (gy-y1),
                                                       (db <= dt) ? (gx-x0) : (gx-x1)));
                }
            }
            else {
                T lx0 = target->data.line_3d.x0, ly0 = target->data.line_3d.y0, lz0 = target->data.line_3d.z0;
                T dx  = target->data.line_3d.x1 - lx0;
                T dy  = target->data.line_3d.y1 - ly0;
                T dz  = target->data.line_3d.z1 - lz0;
                T t = detail::ProjectPointToLine3D(gx, gy, gz, lx0, ly0, lz0, dx, dy, dz);
                lock_a = detail::ClampParameter(target->type, t);
            }
        }

        // 💡 3. 应用参数锁解出本帧位置
        if (target->type == NodeType::SPHERE_3D || target->type == NodeType::SPHERE_3D_FOUR_POINTS) {
            T cx = target->data.sphere_3d.cx, cy = target->data.sphere_3d.cy, cz = target->data.sphere_3d.cz;
            T r  = target->data.sphere_3d.r;
            node.data.snap_3d.x = cx + r * static_cast<T>(std::cos(lock_b)) * static_cast<T>(std::sin(lock_a));
            node.data.snap_3d.y = cy + r * static_cast<T>(std::sin(lock_b)) * static_cast<T>(std::sin(lock_a));
            node.data.snap_3d.z = cz + r * static_cast<T>(std::cos(lock_a));
        }
        else if (target->type == NodeType::CYLINDER_3D) {
            T x0 = target->data.cylinder_3d.x0, y0 = target->data.cylinder_3d.y0, z0 = target->data.cylinder_3d.z0;
            T x1 = target->data.cylinder_3d.x1, y1 = target->data.cylinder_3d.y1, z1 = target->data.cylinder_3d.z1;
            T r  = target->data.cylinder_3d.r;
            if (lock_a >= static_cast<T>(0)) {
                T adx = x1 - x0, ady = y1 - y0, adz = z1 - z0;
                T px = x0 + adx * lock_a, py = y0 + ady * lock_a, pz = z0 + adz * lock_a;
                node.data.snap_3d.x = px + r * static_cast<T>(std::cos(lock_b));
                node.data.snap_3d.y = py + r * static_cast<T>(std::sin(lock_b));
                node.data.snap_3d.z = pz;
            } else {
                T bx = (lock_a < static_cast<T>(-2.5)) ? x1 : x0;
                T by = (lock_a < static_cast<T>(-2.5)) ? y1 : y0;
                T bz = (lock_a < static_cast<T>(-2.5)) ? z1 : z0;
                node.data.snap_3d.x = bx + r * static_cast<T>(std::cos(lock_b));
                node.data.snap_3d.y = by + r * static_cast<T>(std::sin(lock_b));
                node.data.snap_3d.z = bz;
            }
        }
        else {
            T lx0 = target->data.line_3d.x0, ly0 = target->data.line_3d.y0, lz0 = target->data.line_3d.z0;
            T dx  = target->data.line_3d.x1 - lx0;
            T dy  = target->data.line_3d.y1 - ly0;
            T dz  = target->data.line_3d.z1 - lz0;
            node.data.snap_3d.x = lx0 + dx * lock_a;
            node.data.snap_3d.y = ly0 + dy * lock_a;
            node.data.snap_3d.z = lz0 + dz * lock_a;
        }
    }
} // namespace StuCanvas