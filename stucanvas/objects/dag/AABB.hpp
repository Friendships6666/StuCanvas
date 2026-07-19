/****************************************************************************
 * Copyright (c) 2025-2026 Tian Yuxuan (Friendships666)                     *
 *                                                                          *
 * StuCanvas is licensed under Mulan PSL v2.                                *
 * You can use this software according to the terms and conditions of the   *
 * Mulan PSL v2.                                                            *
 * You may obtain a copy of Mulan PSL v2 at:                                *
 *          http://license.coscl.org.cn/MulanPSL2                           *
 *                                                                          *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF     *
 * ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO        *
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.       *
 * See the Mulan PSL v2 for more details.                                   *
 ***************************************************************************/


#pragma once

#include <algorithm>
#include <cmath>
#include <eigen3/Eigen/Dense>
#include <limits>

#include "instance.hpp"
#include "node_types.hpp"
#include "object.hpp"

namespace StuCanvas
{
    // 用户侧无 Eigen 暴露，保持纯原生 double 承载
    struct AABB3D
    {
        double min_x;
        double min_y;
        double min_z;
        double max_x;
        double max_y;
        double max_z;
    };

    /**
     * @brief 计算 DAGObjectInstance 变换后的 Axis-Aligned Bounding Box (AABB 3D)
     *
     * 算法原理：
     * 1. 0 资产依赖：直接通过 switch-case 提取 NodeData union 原始拓扑。对于 2D 对象，其本地坐标 z 轴天然初始化为 0.0。
     * 2. 构造仿射变换矩阵 A = Rotation * Scale，并使用 Arvo 快速轴向区间投影算法
     *    在 O(1) 复杂度内将 local AABB 映射到 world AABB。
     * 3. 2D/3D 统一判定：不进行任何手动的条件判定，通过矩阵变换自然产生正确的 3D 空间包围盒。
     */
    [[nodiscard]] inline AABB3D computeInstanceAABB ( const DAGObjectInstance& instance )
    {
        const DAGObject* node = instance.source;
        if ( !node ) [[unlikely]]
        {
            return { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        }

        constexpr double inf = std::numeric_limits< double >::infinity ();
        Eigen::Vector3d L_min ( inf, inf, inf );
        Eigen::Vector3d L_max ( -inf, -inf, -inf );

        // =====================================================================
        // 1. 纯 NodeData 拓扑 case 解析 (2D 对象的 z 轴天然设为 0.0)
        // =====================================================================
        switch ( node->type )
        {
            case NodeType::POINT_2D_FREE:
            case NodeType::POINT_2D_MID:
            case NodeType::POINT_2D_SECTION:
            case NodeType::POINT_2D_INTERSECT:
            {
                const double x = node->data.point_2d.x;
                const double y = node->data.point_2d.y;
                L_min = Eigen::Vector3d ( x, y, 0.0 );
                L_max = Eigen::Vector3d ( x, y, 0.0 );
                break;
            }
            case NodeType::POINT_2D_SNAP:
            {
                const double x = node->data.snap_2d.x;
                const double y = node->data.snap_2d.y;
                L_min = Eigen::Vector3d ( x, y, 0.0 );
                L_max = Eigen::Vector3d ( x, y, 0.0 );
                break;
            }
            case NodeType::POINT_3D_FREE:
            case NodeType::POINT_3D_MID:
            case NodeType::POINT_3D_SECTION:
            case NodeType::POINT_3D_INTERSECT:
            {
                const double x = node->data.point_3d.x;
                const double y = node->data.point_3d.y;
                const double z = node->data.point_3d.z;
                L_min = Eigen::Vector3d ( x, y, z );
                L_max = Eigen::Vector3d ( x, y, z );
                break;
            }
            case NodeType::POINT_3D_SNAP:
            {
                const double x = node->data.snap_3d.x;
                const double y = node->data.snap_3d.y;
                const double z = node->data.snap_3d.z;
                L_min = Eigen::Vector3d ( x, y, z );
                L_max = Eigen::Vector3d ( x, y, z );
                break;
            }
            case NodeType::LINE_2D_SEGMENT:
            {
                const double x0 = node->data.line_2d.x0;
                const double y0 = node->data.line_2d.y0;
                const double x1 = node->data.line_2d.x1;
                const double y1 = node->data.line_2d.y1;
                L_min = Eigen::Vector3d ( std::min ( x0, x1 ), std::min ( y0, y1 ), 0.0 );
                L_max = Eigen::Vector3d ( std::max ( x0, x1 ), std::max ( y0, y1 ), 0.0 );
                break;
            }
            case NodeType::LINE_3D_SEGMENT:
            {
                const double x0 = node->data.line_3d.x0;
                const double y0 = node->data.line_3d.y0;
                const double z0 = node->data.line_3d.z0;
                const double x1 = node->data.line_3d.x1;
                const double y1 = node->data.line_3d.y1;
                const double z1 = node->data.line_3d.z1;
                L_min = Eigen::Vector3d ( std::min ( x0, x1 ), std::min ( y0, y1 ), std::min ( z0, z1 ) );
                L_max = Eigen::Vector3d ( std::max ( x0, x1 ), std::max ( y0, y1 ), std::max ( z0, z1 ) );
                break;
            }
            case NodeType::CIRCLE_2D:
            case NodeType::CIRCLE_2D_THREE_POINTS:
            case NodeType::ARC_2D:
            {
                const double cx = node->data.circle_2d.cx;
                const double cy = node->data.circle_2d.cy;
                const double r = node->data.circle_2d.r;
                L_min = Eigen::Vector3d ( cx - r, cy - r, 0.0 );
                L_max = Eigen::Vector3d ( cx + r, cy + r, 0.0 );
                break;
            }
            case NodeType::SPHERE_3D:
            case NodeType::SPHERE_3D_FOUR_POINTS:
            {
                const double cx = node->data.sphere_3d.cx;
                const double cy = node->data.sphere_3d.cy;
                const double cz = node->data.sphere_3d.cz;
                const double r = node->data.sphere_3d.r;
                L_min = Eigen::Vector3d ( cx - r, cy - r, cz - r );
                L_max = Eigen::Vector3d ( cx + r, cy + r, cz + r );
                break;
            }
            case NodeType::CYLINDER_3D:
            {
                const double x0 = node->data.cylinder_3d.x0;
                const double y0 = node->data.cylinder_3d.y0;
                const double z0 = node->data.cylinder_3d.z0;
                const double x1 = node->data.cylinder_3d.x1;
                const double y1 = node->data.cylinder_3d.y1;
                const double z1 = node->data.cylinder_3d.z1;
                const double r = node->data.cylinder_3d.r;

                const Eigen::Vector3d p0 ( x0, y0, z0 );
                const Eigen::Vector3d p1 ( x1, y1, z1 );
                const Eigen::Vector3d d = p1 - p0;
                const double len = d.norm ();

                if ( len < 1e-12 )
                {
                    L_min = p0 - Eigen::Vector3d ( r, r, r );
                    L_max = p0 + Eigen::Vector3d ( r, r, r );
                }
                else
                {
                    const Eigen::Vector3d u = d / len;
                    const double ex = r * std::sqrt ( std::max ( 0.0, 1.0 - u.x () * u.x () ) );
                    const double ey = r * std::sqrt ( std::max ( 0.0, 1.0 - u.y () * u.y () ) );
                    const double ez = r * std::sqrt ( std::max ( 0.0, 1.0 - u.z () * u.z () ) );

                    L_min = Eigen::Vector3d ( std::min ( x0, x1 ) - ex, std::min ( y0, y1 ) - ey,
                                              std::min ( z0, z1 ) - ez );
                    L_max = Eigen::Vector3d ( std::max ( x0, x1 ) + ex, std::max ( y0, y1 ) + ey,
                                              std::max ( z0, z1 ) + ez );
                }
                break;
            }
            case NodeType::PLANE_3D:
            case NodeType::PLANE_3D_PARALLEL:
            case NodeType::PLANE_3D_PERPENDICULAR:
            case NodeType::LINE_2D_STRAIGHT:
            case NodeType::LINE_2D_RAY:
            case NodeType::LINE_3D_STRAIGHT:
            case NodeType::LINE_3D_RAY:
            {
                L_min = Eigen::Vector3d ( -inf, -inf, -inf );
                L_max = Eigen::Vector3d ( inf, inf, inf );
                break;
            }
            default:
            {
                // 默认降级为物理零点盒（函数、标量等非几何属性节点）
                L_min = Eigen::Vector3d::Zero ();
                L_max = Eigen::Vector3d::Zero ();
                break;
            }
        }

        // =====================================================================
        // 2. 应用仿射变换投影（Arvo's Bounding Box Projection Algorithm）
        // =====================================================================
        const Eigen::Quaterniond q ( instance.world_rotation[ 3 ],   // w
                                     instance.world_rotation[ 0 ],   // x
                                     instance.world_rotation[ 1 ],   // y
                                     instance.world_rotation[ 2 ]    // z
        );
        const Eigen::Matrix3d Rot = q.normalized ().toRotationMatrix ();
        const Eigen::Matrix3d Scale =
            Eigen::Vector3d ( instance.world_scales[ 0 ], instance.world_scales[ 1 ], instance.world_scales[ 2 ] )
                .asDiagonal ();

        const Eigen::Matrix3d A = Rot * Scale;
        const Eigen::Vector3d t ( instance.world_position[ 0 ], instance.world_position[ 1 ],
                                  instance.world_position[ 2 ] );

        // Arvo Bounding Box 区间仿射投影
        Eigen::Vector3d W_min = t;
        Eigen::Vector3d W_max = t;
        for ( int i = 0; i < 3; ++i )
        {
            for ( int j = 0; j < 3; ++j )
            {
                const double a = A ( i, j ) * L_min ( j );
                const double b = A ( i, j ) * L_max ( j );
                W_min ( i ) += std::min ( a, b );
                W_max ( i ) += std::max ( a, b );
            }
        }

        return { W_min.x (), W_min.y (), W_min.z (), W_max.x (), W_max.y (), W_max.z () };
    }
}   // namespace StuCanvas
