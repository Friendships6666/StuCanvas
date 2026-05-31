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
    /**
     * @brief 2D 自由直线的代数解算器
     * @note 拓扑排序算法已经完全保证了父节点（端点 P0, P1）在此函数运行前，
     *       其代数状态（point_2d）已经更新为最新值。因此直接读取极为安全、高效！
     */
    template <typename T>
    void SolveLine2DStraight(SObjectGraph<T>& graph, SObject<T>& self) noexcept
    {
        const SObject<T>* p0 = self.parents[0];
        const SObject<T>* p1 = self.parents[1];

        self.data.line_2d.x0 = p0->data.point_2d.x;
        self.data.line_2d.y0 = p0->data.point_2d.y;
        self.data.line_2d.x1 = p1->data.point_2d.x;
        self.data.line_2d.y1 = p1->data.point_2d.y;
    }


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
} // namespace StuCanvas
