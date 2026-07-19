/***************************************************************************
 * Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
 *                                                                          *
 * Distributed under the terms of the MIT License.                          *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <vector>

#include "flex_vector.hpp"
#include "instance.hpp"
#include "object.hpp"
#include "pinned_vector.hpp"
#include "tiny_vector.hpp"

namespace StuCanvas
{


    // =========================================================================
    // 0. 内部辅助数据提取函数 (零开销内联，提取父对象的数据放入自己位置)
    // =========================================================================

    inline void extractLine2D ( DAGObject& node )
    {
        DAGObject* p0 = node.parents[ 0 ];
        DAGObject* p1 = node.parents[ 1 ];

        node.data.line_2d.x0 = p0->data.point_2d.x;
        node.data.line_2d.y0 = p0->data.point_2d.y;
        node.data.line_2d.x1 = p1->data.point_2d.x;
        node.data.line_2d.y1 = p1->data.point_2d.y;
    }

    inline void extractLine3D ( DAGObject& node )
    {
        DAGObject* p0 = node.parents[ 0 ];
        DAGObject* p1 = node.parents[ 1 ];

        node.data.line_3d.x0 = p0->data.point_3d.x;
        node.data.line_3d.y0 = p0->data.point_3d.y;
        node.data.line_3d.z0 = p0->data.point_3d.z;
        node.data.line_3d.x1 = p1->data.point_3d.x;
        node.data.line_3d.y1 = p1->data.point_3d.y;
        node.data.line_3d.z1 = p1->data.point_3d.z;
    }

    // =========================================================================
    // 1. 各类型几何/物理解算函数 (VTable-Free Inline Placeholders)
    // =========================================================================

    // ─────────────────────────────────────────────────────────────────────────
    // 1.1 2D 节点求解器
    // ─────────────────────────────────────────────────────────────────────────

    inline void solvePoint2DMid ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 中点解算逻辑
    }
    // 🚀 零开销内联：二维线系列万能吸附投影计算器（通过常数参数进行编译期分支消除）
    inline void solveLine2DSnapHelper ( DAGObject& node, DAGObject* parent, bool clamp_min, double min_t,
                                        bool clamp_max, double max_t )
    {
        double gx = node.data.snap_2d.x;
        double gy = node.data.snap_2d.y;

        double x0 = parent->data.line_2d.x0;
        double y0 = parent->data.line_2d.y0;
        double x1 = parent->data.line_2d.x1;
        double y1 = parent->data.line_2d.y1;

        double dx = x1 - x0;
        double dy = y1 - y0;
        double denom = ( dx * dx ) + ( dy * dy );

        double t = 0.0;

        t = ( ( gx - x0 ) * dx + ( gy - y0 ) * dy ) / denom;

        // 编译器会根据实参的 true/false 自动在内联时抹除无用分支
        if ( clamp_min && t < min_t )
        {
            t = min_t;
        }
        if ( clamp_max && t > max_t )
        {
            t = max_t;
        }


        node.data.snap_2d.lock = t;
        node.data.snap_2d.x = x0 + t * dx;
        node.data.snap_2d.y = y0 + t * dy;
    }

    // 🚀 零开销内联：二维圆/弧系列万能吸附投影计算器
    inline void solveCircle2DSnapHelper ( DAGObject& node, DAGObject* parent )
    {
        double gx = node.data.snap_2d.x;
        double gy = node.data.snap_2d.y;

        double cx = parent->data.circle_2d.cx;
        double cy = parent->data.circle_2d.cy;
        double r = parent->data.circle_2d.r;

        double dx = gx - cx;
        double dy = gy - cy;

        // 计算当前猜测点相对于圆心的极角 theta（范围在 [-pi, pi] 之间）
        // 若猜测点恰好在圆心上，std::atan2 也会安全返回 0.0，不触发除零异常
        double theta = std::atan2 ( dy, dx );

        node.data.snap_2d.lock = theta;
        node.data.snap_2d.x = cx + r * std::cos ( theta );
        node.data.snap_2d.y = cy + r * std::sin ( theta );
    }

    // =========================================================================
    // 🚀 万能吸附求解器 (POINT_2D_SNAP - 2D 智能对齐约束)
    // =========================================================================
    inline void solvePoint2DSnap ( DAGraph& graph, DAGObject& node )
    {
        // 1. 获取被吸附的父目标节点
        DAGObject* parent = node.parents[ 0 ];

        // 2. 根据父节点的几何/物理类型，进行万能吸附分发
        switch ( parent->type )
        {
            // ─────────────────────────────────────────────────────────────────
            // A. 吸附 2D 线系列 (根据初始猜测点计算 t 值，并对线段/射线执行范围钳制)
            // ─────────────────────────────────────────────────────────────────
            case NodeType::LINE_2D_STRAIGHT:
            case NodeType::LINE_2D_PARALLEL:
            case NodeType::LINE_2D_PERPENDICULAR:
            {
                // 直线系列：t 无须钳制 (-inf, +inf)
                solveLine2DSnapHelper ( node, parent, false, 0.0, false, 0.0 );
                break;
            }

            case NodeType::LINE_2D_SEGMENT:
            {
                // 线段：t 被强行钳制在 [0.0, 1.0]
                solveLine2DSnapHelper ( node, parent, true, 0.0, true, 1.0 );
                break;
            }

            case NodeType::LINE_2D_RAY:
            {
                // 射线：t 被强行钳制在 [0.0, +inf)
                solveLine2DSnapHelper ( node, parent, true, 0.0, false, 0.0 );
                break;
            }

            // ─────────────────────────────────────────────────────────────────
            // B. 吸附 2D 圆与圆弧系列 (计算极角 theta 并执行极坐标锁定与写回)
            // ─────────────────────────────────────────────────────────────────
            case NodeType::CIRCLE_2D:
            case NodeType::CIRCLE_2D_THREE_POINTS:
            case NodeType::ARC_2D:
            {
                solveCircle2DSnapHelper ( node, parent );
                break;
            }

            case NodeType::ELLIPSE_2D:
            case NodeType::HYPERBOLA_2D:
            case NodeType::PARABOLA_2D:
            {
                break;
            }


                // ─────────────────────────────────────────────────────────────────
                // C. 吸附 2D 显式代数函数（动态检测 1 阶导数，自适应启用 Gauss-Newton 正交投影）
                // ─────────────────────────────────────────────────────────────────
            case NodeType::FUNC_EXPLICIT_2D:
            {
                // 1. 尝试检索 Y = F(X) 资产
                if ( auto* asset_y_from_x = parent->assets.get< DAGAssets::ExplicitScalarFnYFromX > () )
                {
                    double x_g = node.data.snap_2d.x;
                    double y_g = node.data.snap_2d.y;

                    double x = x_g;

                    // 1.1 初始范围钳制
                    if ( auto* dom = parent->assets.get< DAGAssets::xDomain > () )
                    {
                        x = std::clamp ( x, dom->min_val, dom->max_val );
                    }

                    // 🚀 核心优化：若挂载了 1 阶导数资产，启用 2 次 Gauss-Newton 迭代，无缝收敛到正交最近点！
                    if ( auto* deriv = parent->assets.get< DAGAssets::ExplicitDerivativeFnYWrtX > () )
                    {
                        if ( deriv->order == 1 )
                        {
                            for ( int iter = 0; iter < 2; ++iter )
                            {
                                double y = asset_y_from_x->fn ( x );
                                double dy = deriv->fn ( x );
                                double denom = 1.0 + ( dy * dy );
                                if ( denom > 1e-12 )
                                {
                                    x = x + ( ( x_g - x ) + ( y_g - y ) * dy ) / denom;
                                }
                            }
                        }
                    }

                    // 1.2 再次截断并写回
                    if ( auto* dom = parent->assets.get< DAGAssets::xDomain > () )
                    {
                        x = std::clamp ( x, dom->min_val, dom->max_val );
                    }

                    node.data.snap_2d.x = x;
                    node.data.snap_2d.y = asset_y_from_x->fn ( x );
                }
                // 2. 尝试检索 X = F(Y) 资产
                else if ( auto* asset_x_from_y = parent->assets.get< DAGAssets::ExplicitScalarFnXFromY > () )
                {
                    double x_g = node.data.snap_2d.x;
                    double y_g = node.data.snap_2d.y;

                    double y = y_g;

                    if ( auto* dom = parent->assets.get< DAGAssets::yDomain > () )
                    {
                        y = std::clamp ( y, dom->min_val, dom->max_val );
                    }

                    // 🚀 针对 X = F(Y) 的 1 阶导数正交投影迭代
                    if ( auto* deriv = parent->assets.get< DAGAssets::ExplicitDerivativeFnXWrtY > () )
                    {
                        if ( deriv->order == 1 )
                        {
                            for ( int iter = 0; iter < 2; ++iter )
                            {
                                double x = asset_x_from_y->fn ( y );
                                double dx = deriv->fn ( y );
                                double denom = 1.0 + ( dx * dx );
                                if ( denom > 1e-12 )
                                {
                                    y = y + ( ( y_g - y ) + ( x_g - x ) * dx ) / denom;
                                }
                            }
                        }
                    }

                    if ( auto* dom = parent->assets.get< DAGAssets::yDomain > () )
                    {
                        y = std::clamp ( y, dom->min_val, dom->max_val );
                    }

                    node.data.snap_2d.x = asset_x_from_y->fn ( y );
                    node.data.snap_2d.y = y;
                }
                break;
            }
            // ─────────────────────────────────────────────────────────────────
            // D. 吸附 2D 隐式代数函数（f(x, y) = 0）
            // ─────────────────────────────────────────────────────────────────
            case NodeType::FUNC_IMPLICIT_2D:
            {
                // TODO: 后面在此实现 2D 隐式函数吸附求解器
                break;
            }

            // ─────────────────────────────────────────────────────────────────
            // E. 吸附 2D 参数化曲线（x = f(t), y = g(t)）
            // ─────────────────────────────────────────────────────────────────
            case NodeType::FUNC_PARAMETRIC_2D:
            {
                // TODO: 后面在此实现 2D 参数曲线吸附求解器
                break;
            }

            default:
            {
                // 默认不进行任何吸附，保持自由参数状态
                break;
            }
        }
    }
    inline void solvePoint2DSection ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 剖面点解算逻辑
    }

    inline void solvePoint2DIntersect ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 交点解算逻辑
    }

    inline void solveLine2DSegment ( DAGObject& node )
    {
        extractLine2D ( node );
    }

    inline void solveLine2DStraight ( DAGObject& node )
    {
        extractLine2D ( node );
    }

    inline void solveLine2DRay ( DAGObject& node )
    {
        extractLine2D ( node );
    }

    inline void solveLine2DPerpendicular ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 垂线解算逻辑
    }

    inline void solveLine2DParallel ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 平行线解算逻辑
    }

    inline void solveTangent2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 切线解算逻辑
    }

    inline void solveTangent3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 切线解算逻辑
    }

    inline void solveCircle2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 圆解算逻辑
    }

    inline void solveCircle2DThreePoints ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 三点圆解算逻辑
    }

    inline void solveArc2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 圆弧解算逻辑
    }

    inline void solveEllipse2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 椭圆解算逻辑
    }

    inline void solveHyperbola2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 双曲线解算逻辑
    }

    inline void solveParabola2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 抛物线解算逻辑
    }

    inline void solvePoly2DGenerative ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 创成多边形解算逻辑
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1.2 3D 节点求解器
    // ─────────────────────────────────────────────────────────────────────────

    inline void solvePoint3DMid ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 中点解算逻辑
    }

    inline void solvePoint3DSnap ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 捕捉点解算逻辑
    }

    inline void solvePoint3DSection ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 剖面点解算逻辑
    }

    inline void solvePoint3DIntersect ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 交点解算逻辑
    }

    inline void solveLine3DSegment ( DAGObject& node )
    {
        extractLine3D ( node );
    }

    inline void solveLine3DStraight ( DAGObject& node )
    {
        extractLine3D ( node );
    }

    inline void solveLine3DRay ( DAGObject& node )
    {
        extractLine3D ( node );
    }

    inline void solveLine3DPerpendicular ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 垂线解算逻辑
    }

    inline void solveLine3DParallel ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 平行线解算逻辑
    }

    inline void solvePlane3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 平面解算逻辑
    }

    inline void solvePlane3DParallel ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 平行平面解算逻辑
    }

    inline void solvePlane3DPerpendicular ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 垂直平面解算逻辑
    }

    inline void solveSphere3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 球解算逻辑
    }

    inline void solveSphere3DFourPoints ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 四点球解算逻辑
    }

    inline void solveCylinder3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 圆柱体解算逻辑
    }

    inline void solveCone3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 圆锥体解算逻辑
    }

    inline void solveCuboid3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 长方体解算逻辑
    }

    inline void solvePlatonicSolid3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 柏拉图多面体解算逻辑
    }

    inline void solveCircle3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 圆解算逻辑
    }

    inline void solveArc3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 弧线解算逻辑
    }

    inline void solveEllipse3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 椭圆解算逻辑
    }

    inline void solveHyperbola3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 双曲线解算逻辑
    }

    inline void solveParabola3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 抛物线解算逻辑
    }

    inline void solvePoly3DGenerative ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 创成曲线/曲面解算逻辑
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 1.3 标量与函数式求解器
    // ─────────────────────────────────────────────────────────────────────────

    inline void solveFuncExplicit2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 显式函数解算逻辑
    }

    inline void solveFuncExplicit3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 显式函数解算逻辑
    }

    inline void solveFuncImplicit2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 隐式函数解算逻辑
    }

    inline void solveFuncImplicit3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 隐式函数解算逻辑
    }

    inline void solveFuncParametric2D ( DAGObject& node )
    {
        // 占位符：后续在此实现 2D 参数化函数解算逻辑
    }

    inline void solveFuncParametric3D ( DAGObject& node )
    {
        // 占位符：后续在此实现 3D 参数化函数解算逻辑
    }

    // =========================================================================
    // 2. 核心分发函数 (Huge Switch-case Evaluator - 汇编级等差 O(1) 性能)
    // =========================================================================

    inline void DAGObjectSolver ( DAGraph& graph, DAGObject& node )
    {
        switch ( node.type )
        {
            // 🚀 核心改动：自由点与标量作为非计算节点，case 块直接合并留空，无任何小 solver 依赖
            case NodeType::POINT_2D_FREE:
            case NodeType::POINT_3D_FREE:
            case NodeType::SCALAR:
            {
                break;
            }

            case NodeType::POINT_2D_MID:
            {
                solvePoint2DMid ( node );
                break;
            }

            case NodeType::POINT_2D_SNAP:
            {
                solvePoint2DSnap ( graph, node );
                break;
            }

            case NodeType::POINT_2D_SECTION:
            {
                solvePoint2DSection ( node );
                break;
            }

            case NodeType::POINT_2D_INTERSECT:
            {
                solvePoint2DIntersect ( node );
                break;
            }

            case NodeType::LINE_2D_SEGMENT:
            {
                solveLine2DSegment ( node );
                break;
            }

            case NodeType::LINE_2D_STRAIGHT:
            {
                solveLine2DStraight ( node );
                break;
            }

            case NodeType::LINE_2D_RAY:
            {
                solveLine2DRay ( node );
                break;
            }

            case NodeType::LINE_2D_PERPENDICULAR:
            {
                solveLine2DPerpendicular ( node );
                break;
            }

            case NodeType::LINE_2D_PARALLEL:
            {
                solveLine2DParallel ( node );
                break;
            }

            case NodeType::TANGENT_2D:
            {
                solveTangent2D ( node );
                break;
            }

            case NodeType::TANGENT_3D:
            {
                solveTangent3D ( node );
                break;
            }

            case NodeType::CIRCLE_2D:
            {
                solveCircle2D ( node );
                break;
            }

            case NodeType::CIRCLE_2D_THREE_POINTS:
            {
                solveCircle2DThreePoints ( node );
                break;
            }

            case NodeType::ARC_2D:
            {
                solveArc2D ( node );
                break;
            }

            case NodeType::ELLIPSE_2D:
            {
                solveEllipse2D ( node );
                break;
            }

            case NodeType::HYPERBOLA_2D:
            {
                solveHyperbola2D ( node );
                break;
            }

            case NodeType::PARABOLA_2D:
            {
                solveParabola2D ( node );
                break;
            }

            case NodeType::POLY_2D_GENERATIVE:
            {
                solvePoly2DGenerative ( node );
                break;
            }

            case NodeType::POINT_3D_MID:
            {
                solvePoint3DMid ( node );
                break;
            }

            case NodeType::POINT_3D_SNAP:
            {
                solvePoint3DSnap ( node );
                break;
            }

            case NodeType::POINT_3D_SECTION:
            {
                solvePoint3DSection ( node );
                break;
            }

            case NodeType::POINT_3D_INTERSECT:
            {
                solvePoint3DIntersect ( node );
                break;
            }

            case NodeType::LINE_3D_SEGMENT:
            {
                solveLine3DSegment ( node );
                break;
            }

            case NodeType::LINE_3D_STRAIGHT:
            {
                solveLine3DStraight ( node );
                break;
            }

            case NodeType::LINE_3D_RAY:
            {
                solveLine3DRay ( node );
                break;
            }

            case NodeType::LINE_3D_PERPENDICULAR:
            {
                solveLine3DPerpendicular ( node );
                break;
            }

            case NodeType::LINE_3D_PARALLEL:
            {
                solveLine3DParallel ( node );
                break;
            }

            case NodeType::PLANE_3D:
            {
                solvePlane3D ( node );
                break;
            }

            case NodeType::PLANE_3D_PARALLEL:
            {
                solvePlane3DParallel ( node );
                break;
            }

            case NodeType::PLANE_3D_PERPENDICULAR:
            {
                solvePlane3DPerpendicular ( node );
                break;
            }

            case NodeType::SPHERE_3D:
            {
                solveSphere3D ( node );
                break;
            }

            case NodeType::SPHERE_3D_FOUR_POINTS:
            {
                solveSphere3DFourPoints ( node );
                break;
            }

            case NodeType::CYLINDER_3D:
            {
                solveCylinder3D ( node );
                break;
            }

            case NodeType::CONE_3D:
            {
                solveCone3D ( node );
                break;
            }

            case NodeType::CUBOID_3D:
            {
                solveCuboid3D ( node );
                break;
            }

            case NodeType::PLATONIC_SOLID_3D:
            {
                solvePlatonicSolid3D ( node );
                break;
            }

            case NodeType::CIRCLE_3D:
            {
                solveCircle3D ( node );
                break;
            }

            case NodeType::ARC_3D:
            {
                solveArc3D ( node );
                break;
            }

            case NodeType::ELLIPSE_3D:
            {
                solveEllipse3D ( node );
                break;
            }

            case NodeType::HYPERBOLA_3D:
            {
                solveHyperbola3D ( node );
                break;
            }

            case NodeType::PARABOLA_3D:
            {
                solveParabola3D ( node );
                break;
            }

            case NodeType::POLY_3D_GENERATIVE:
            {
                solvePoly3DGenerative ( node );
                break;
            }

            case NodeType::FUNC_EXPLICIT_2D:
            {
                solveFuncExplicit2D ( node );
                break;
            }

            case NodeType::FUNC_EXPLICIT_3D:
            {
                solveFuncExplicit3D ( node );
                break;
            }

            case NodeType::FUNC_IMPLICIT_2D:
            {
                solveFuncImplicit2D ( node );
                break;
            }

            case NodeType::FUNC_IMPLICIT_3D:
            {
                solveFuncImplicit3D ( node );
                break;
            }

            case NodeType::FUNC_PARAMETRIC_2D:
            {
                solveFuncParametric2D ( node );
                break;
            }

            case NodeType::FUNC_PARAMETRIC_3D:
            {
                solveFuncParametric3D ( node );
                break;
            }

            default:
            {
                // 默认不做处理
                break;
            }
        }
    }
}   // namespace StuCanvas
