/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <stdexcept>
#include "sobject.hpp"
#include "graph.hpp"

namespace StuCanvas
{
    template <typename T>
    inline void InternalSolveLinear2D(SObject<T>& self) noexcept {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];

        // 极速寄存器搬运：2D -> 2D
        self.data.line_2d.x0 = p0->data.point_2d.x;
        self.data.line_2d.y0 = p0->data.point_2d.y;
        self.data.line_2d.x1 = p1->data.point_2d.x;
        self.data.line_2d.y1 = p1->data.point_2d.y;
    }

    template <typename T>
    inline void InternalSolveLinear3D(SObject<T>& self) noexcept {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];

        // 极速寄存器搬运：3D -> 3D
        self.data.line_3d.x0 = p0->data.point_3d.x;
        self.data.line_3d.y0 = p0->data.point_3d.y;
        self.data.line_3d.z0 = p0->data.point_3d.z;
        self.data.line_3d.x1 = p1->data.point_3d.x;
        self.data.line_3d.y1 = p1->data.point_3d.y;
        self.data.line_3d.z1 = p1->data.point_3d.z;
    }

    // ========================================================================
    // 💡 3D 体解算器
    // ========================================================================

    template <typename T>
    void SolveSphere3D(SObjectGraph<T>&, SObject<T>& self) noexcept
    {
        const SObject<T>* center = self.parents[0];
        const SObject<T>* radius = self.parents[1];
        self.data.sphere_3d.cx = center->data.point_3d.x;
        self.data.sphere_3d.cy = center->data.point_3d.y;
        self.data.sphere_3d.cz = center->data.point_3d.z;
        self.data.sphere_3d.r  = radius->data.scalar.value;
    }

    template <typename T>
    void SolveSphere3DFourPoints(SObjectGraph<T>&, SObject<T>& self) noexcept
    {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];
        const SObject<T>* p2 = self.parents[2];
        const SObject<T>* p3 = self.parents[3];

        T ax = p0->data.point_3d.x, ay = p0->data.point_3d.y, az = p0->data.point_3d.z;
        T bx = p1->data.point_3d.x, by = p1->data.point_3d.y, bz = p1->data.point_3d.z;
        T cx = p2->data.point_3d.x, cy = p2->data.point_3d.y, cz = p2->data.point_3d.z;
        T dx = p3->data.point_3d.x, dy = p3->data.point_3d.y, dz = p3->data.point_3d.z;

        T ux = bx - ax, uy = by - ay, uz = bz - az;
        T vx = cx - ax, vy = cy - ay, vz = cz - az;
        T wx = dx - ax, wy = dy - ay, wz = dz - az;

        T uv = ux*vx + uy*vy + uz*vz;
        T uw = ux*wx + uy*wy + uz*wz;
        T vw = vx*wx + vy*wy + vz*wz;

        T nu = ux*ux + uy*uy + uz*uz;
        T nv = vx*vx + vy*vy + vz*vz;
        T nw = wx*wx + wy*wy + wz*wz;

        T det = nu*(nv*nw - vw*vw) - uv*(uv*nw - uw*vw) + uw*(uv*vw - uw*nv);

        T a_s = (nv*nw - vw*vw) / det;
        T b_s = (uv*nw - uw*vw) / det;
        T c_s = (uv*vw - uw*nv) / det;

        T cx_ = static_cast<T>(0.5) * (a_s*nu + b_s*uv + c_s*uw);
        T cy_ = static_cast<T>(0.5) * (a_s*uv + b_s*nv + c_s*vw);
        T cz_ = static_cast<T>(0.5) * (a_s*uw + b_s*vw + c_s*nw);

        self.data.sphere_3d.cx = ax + cx_;
        self.data.sphere_3d.cy = ay + cy_;
        self.data.sphere_3d.cz = az + cz_;
        self.data.sphere_3d.r  = static_cast<T>(sqrt(cx_*cx_ + cy_*cy_ + cz_*cz_));
    }

    template <typename T>
    void SolveCylinder3D(SObjectGraph<T>&, SObject<T>& self) noexcept
    {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];
        const SObject<T>* pr = self.parents[2];
        self.data.cylinder_3d.x0 = p0->data.point_3d.x;
        self.data.cylinder_3d.y0 = p0->data.point_3d.y;
        self.data.cylinder_3d.z0 = p0->data.point_3d.z;
        self.data.cylinder_3d.x1 = p1->data.point_3d.x;
        self.data.cylinder_3d.y1 = p1->data.point_3d.y;
        self.data.cylinder_3d.z1 = p1->data.point_3d.z;
        self.data.cylinder_3d.r  = pr->data.scalar.value;
    }

    // ========================================================================
    // 💡 圆解算器
    // ========================================================================

    template <typename T>
    void SolveCircle2D(SObjectGraph<T>&, SObject<T>& self) noexcept
    {
        const SObject<T>* center = self.parents[0];
        const SObject<T>* radius = self.parents[1];
        self.data.circle_2d.cx = center->data.point_2d.x;
        self.data.circle_2d.cy = center->data.point_2d.y;
        self.data.circle_2d.r  = radius->data.scalar.value;
    }

    template <typename T>
    void SolveCircle2DThreePoints(SObjectGraph<T>&, SObject<T>& self) noexcept
    {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];
        const SObject<T>* p2 = self.parents[2];

        T ax = p0->data.point_2d.x, ay = p0->data.point_2d.y;
        T bx = p1->data.point_2d.x, by = p1->data.point_2d.y;
        T cx = p2->data.point_2d.x, cy = p2->data.point_2d.y;

        T d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        T ux = ((ax*ax + ay*ay) * (by - cy) + (bx*bx + by*by) * (cy - ay) + (cx*cx + cy*cy) * (ay - by)) / d;
        T uy = ((ax*ax + ay*ay) * (cx - bx) + (bx*bx + by*by) * (ax - cx) + (cx*cx + cy*cy) * (bx - ax)) / d;

        T dx = ax - ux, dy = ay - uy;
        T r = static_cast<T>(sqrt(dx*dx + dy*dy));

        self.data.circle_2d.cx = ux;
        self.data.circle_2d.cy = uy;
        self.data.circle_2d.r  = r;
    }

    // ========================================================================
    // 💡 6 个公开解算器实现 (100% 代码复用)
    // ========================================================================

    // --- 2D 系列 ---
    template <typename T>
    void SolveLine2DStraight(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear2D(self); }

    template <typename T>
    void SolveLine2DRay(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear2D(self); }

    template <typename T>
    void SolveLine2DSegment(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear2D(self); }

    // --- 3D 系列 ---
    template <typename T>
    void SolveLine3DStraight(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear3D(self); }

    template <typename T>
    void SolveLine3DRay(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear3D(self); }

    template <typename T>
    void SolveLine3DSegment(SObjectGraph<T>&, SObject<T>& self) noexcept { InternalSolveLinear3D(self); }


    template <typename T>
    void SolvePlane3D(SObjectGraph<T>& graph, SObject<T>& self) noexcept
    {
        // 💡 直接拉取端点指针，0 判空开销
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];
        const SObject<T>* p2 = self.parents[2];

        T x1 = p0->data.point_3d.x;
        T y1 = p0->data.point_3d.y;
        T z1 = p0->data.point_3d.z;

        T x2 = p1->data.point_3d.x;
        T y2 = p1->data.point_3d.y;
        T z2 = p1->data.point_3d.z;

        T x3 = p2->data.point_3d.x;
        T y3 = p2->data.point_3d.y;
        T z3 = p2->data.point_3d.z;

        // 建立两个平面内的向量：u = P2 - P1, v = P3 - P1
        T ux = x2 - x1;
        T uy = y2 - y1;
        T uz = z2 - z1;

        T vx = x3 - x1;
        T vy = y3 - y1;
        T vz = z3 - z1;

        // 极速叉乘得到未归一化的法向量参数 a, b, c
        T a = uy * vz - uz * vy;
        T b = uz * vx - ux * vz;
        T c = ux * vy - uy * vx;

        // 💡 0 除法，直接计算标量参数 d = -(a*x1 + b*y1 + c*z1)
        T d = -(a * x1 + b * y1 + c * z1);

        // 一键物理写入自己的 union 数据区
        self.data.plane_3d.a = a;
        self.data.plane_3d.b = b;
        self.data.plane_3d.c = c;
        self.data.plane_3d.d = d;
    }

    template <typename T>
    void SolvePoint2DMid(SObjectGraph<T>& graph, SObject<T>& self) noexcept {
        // 直接拉取两个父节点指针，不执行任何 null 检查，达到寄存器级运行效率
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];

        // 2D 中点计算：x_mid = (x0 + x1) * 0.5
        self.data.point_2d.x = (p0->data.point_2d.x + p1->data.point_2d.x) * static_cast<T>(0.5);
        self.data.point_2d.y = (p0->data.point_2d.y + p1->data.point_2d.y) * static_cast<T>(0.5);
    }

    template <typename T>
    void SolvePoint3DMid(SObjectGraph<T>& graph, SObject<T>& self) noexcept {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];

        // 3D 中点计算：x_mid = (x0 + x1) * 0.5
        self.data.point_3d.x = (p0->data.point_3d.x + p1->data.point_3d.x) * static_cast<T>(0.5);
        self.data.point_3d.y = (p0->data.point_3d.y + p1->data.point_3d.y) * static_cast<T>(0.5);
        self.data.point_3d.z = (p0->data.point_3d.z + p1->data.point_3d.z) * static_cast<T>(0.5);
    }

} // namespace StuCanvas
