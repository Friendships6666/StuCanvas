#pragma once

#include "cmath"
#include "../utils/flat_map.hpp"
#include "../types/point.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Graph;
    template <typename T>
    struct Node;
    using std::abs;

    template <typename T>
    void SolveStraightLine_2D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_2d;
        data.x0 = point_start->data.point_2d.x;
        data.y0 = point_start->data.point_2d.y;
        data.x1 = point_end->data.point_2d.x;
        data.y1 = point_end->data.point_2d.y;
    }

    template <typename T>
    void SolveStraightLine_3D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_3d;
        data.x0 = point_start->data.point_3d.x;
        data.y0 = point_start->data.point_3d.y;
        data.z0 = point_start->data.point_3d.z;
        data.x1 = point_end->data.point_3d.x;
        data.y1 = point_end->data.point_3d.y;
        data.z1 = point_end->data.point_3d.z;
    }

    template <typename T>
    void SolveSegment_2D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_2d;
        data.x0 = point_start->data.point_2d.x;
        data.y0 = point_start->data.point_2d.y;
        data.x1 = point_end->data.point_2d.x;
        data.y1 = point_end->data.point_2d.y;
    }

    template <typename T>
    void SolveSegment_3D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_3d;
        data.x0 = point_start->data.point_3d.x;
        data.y0 = point_start->data.point_3d.y;
        data.z0 = point_start->data.point_3d.z;
        data.x1 = point_end->data.point_3d.x;
        data.y1 = point_end->data.point_3d.y;
        data.z1 = point_end->data.point_3d.z;
    }

    template <typename T>
    void SolveRay_2D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_2d;
        data.x0 = point_start->data.point_2d.x;
        data.y0 = point_start->data.point_2d.y;
        data.x1 = point_end->data.point_2d.x;
        data.y1 = point_end->data.point_2d.y;
    }

    template <typename T>
    void SolveRay_3D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        const auto point_end = graph.GetNode(self.parents[1]);
        auto& data = self.data.line_3d;
        data.x0 = point_start->data.point_3d.x;
        data.y0 = point_start->data.point_3d.y;
        data.z0 = point_start->data.point_3d.z;
        data.x1 = point_end->data.point_3d.x;
        data.y1 = point_end->data.point_3d.y;
        data.z1 = point_end->data.point_3d.z;
    }

    template <typename T>
    void SolveCircle_2D(Graph<T>& graph, Node<T>& self)
    {
        const auto point_start = graph.GetNode(self.parents[0]);
        auto& data = self.data.circle_2d;
        data.cx = point_start->data.point_2d.x;;
        data.cy = point_start->data.point_2d.y;;
    }


    template <typename T>
    void SolvePlane_3Points_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 3) return;

        Node<T>* n1 = graph.GetNode(self.parents[0]);
        Node<T>* n2 = graph.GetNode(self.parents[1]);
        Node<T>* n3 = graph.GetNode(self.parents[2]);

        const auto& p1 = n1->data.point_3d;
        const auto& p2 = n2->data.point_3d;
        const auto& p3 = n3->data.point_3d;

        auto& plane = self.data.plane_3d;


        const T eps = std::numeric_limits<T>::epsilon();
        const T min_limit = eps * static_cast<T>(10);


        auto safe_diff = [&](T a, T b) -> T
        {
            T diff = a - b;
            if (abs(diff) < eps)
            {
                return (diff >= 0) ? min_limit : -min_limit;
            }
            return diff;
        };


        plane.ox = p1.x;
        plane.oy = p1.y;
        plane.oz = p1.z;


        plane.ux = safe_diff(p2.x, p1.x);
        plane.uy = safe_diff(p2.y, p1.y);
        plane.uz = safe_diff(p2.z, p1.z);

        plane.vx = safe_diff(p3.x, p1.x);
        plane.vy = safe_diff(p3.y, p1.y);
        plane.vz = safe_diff(p3.z, p1.z);


        T nx = plane.uy * plane.vz - plane.uz * plane.vy;
        T ny = plane.uz * plane.vx - plane.ux * plane.vz;
        T nz = plane.ux * plane.vy - plane.uy * plane.vx;

        T norm_sq = nx * nx + ny * ny + nz * nz;


        if (norm_sq < eps)
        {
            if (abs(plane.ux) < abs(plane.uy))
                plane.vx += min_limit;
            else
                plane.vy += min_limit;
        }
    }

    template <typename T>
    void SolveCircle_CP_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;
        auto* center = graph.GetNode(self.parents[0]);
        auto* p_on = graph.GetNode(self.parents[1]);

        T cx = center->data.point_2d.x;
        T cy = center->data.point_2d.y;
        T px = p_on->data.point_2d.x;
        T py = p_on->data.point_2d.y;

        T dx = px - cx;
        T dy = py - cy;

        self.data.circle_2d.cx = cx;
        self.data.circle_2d.cy = cy;
        self.data.circle_2d.r = std::sqrt(dx * dx + dy * dy);
    }

    template <typename T>
    void SolveCircle_3P_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 3) return;
        const T x1 = graph.GetNode(self.parents[0])->data.point_2d.x;
        const T y1 = graph.GetNode(self.parents[0])->data.point_2d.y;
        const T x2 = graph.GetNode(self.parents[1])->data.point_2d.x;
        const T y2 = graph.GetNode(self.parents[1])->data.point_2d.y;
        const T x3 = graph.GetNode(self.parents[2])->data.point_2d.x;
        const T y3 = graph.GetNode(self.parents[2])->data.point_2d.y;

        const T eps = std::numeric_limits<T>::epsilon();


        T D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));


        if (std::abs(D) < eps)
        {
            D = (D >= 0) ? eps * 10 : -eps * 10;
        }

        const T p1_sq = x1 * x1 + y1 * y1;
        const T p2_sq = x2 * x2 + y2 * y2;
        const T p3_sq = x3 * x3 + y3 * y3;


        T cx = (p1_sq * (y2 - y3) + p2_sq * (y3 - y1) + p3_sq * (y1 - y2)) / D;
        T cy = (p1_sq * (x3 - x2) + p2_sq * (x1 - x3) + p3_sq * (x2 - x1)) / D;

        self.data.circle_2d.cx = cx;
        self.data.circle_2d.cy = cy;


        T dx = x1 - cx;
        T dy = y1 - cy;
        self.data.circle_2d.r = std::sqrt(dx * dx + dy * dy);
    }


    template <typename T>
    void SolveArc_3P_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 3) return;


        const T x1 = graph.GetNode(self.parents[0])->data.point_2d.x;
        const T y1 = graph.GetNode(self.parents[0])->data.point_2d.y;
        const T x2 = graph.GetNode(self.parents[1])->data.point_2d.x;
        const T y2 = graph.GetNode(self.parents[1])->data.point_2d.y;
        const T x3 = graph.GetNode(self.parents[2])->data.point_2d.x;
        const T y3 = graph.GetNode(self.parents[2])->data.point_2d.y;

        const T eps = std::numeric_limits<T>::epsilon();
        const T PI = static_cast<T>(3.14159265358979323846);


        T D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));


        if (std::abs(D) < eps)
        {
            D = (D >= 0) ? eps * 10 : -eps * 10;
        }

        const T p1_sq = x1 * x1 + y1 * y1;
        const T p2_sq = x2 * x2 + y2 * y2;
        const T p3_sq = x3 * x3 + y3 * y3;

        T cx = (p1_sq * (y2 - y3) + p2_sq * (y3 - y1) + p3_sq * (y1 - y2)) / D;
        T cy = (p1_sq * (x3 - x2) + p2_sq * (x1 - x3) + p3_sq * (x2 - x1)) / D;
        T r = std::sqrt((x1 - cx) * (x1 - cx) + (y1 - cy) * (y1 - cy));


        using std::atan2;
        T theta1 = atan2(y1 - cy, x1 - cx);
        T theta2 = atan2(y2 - cy, x2 - cx);
        T theta3 = atan2(y3 - cy, x3 - cx);


        auto normalize = [&](T angle)
        {
            while (angle < 0) angle += 2 * PI;
            while (angle >= 2 * PI) angle -= 2 * PI;
            return angle;
        };

        theta1 = normalize(theta1);
        theta2 = normalize(theta2);
        theta3 = normalize(theta3);


        T dist12_ccw = (theta2 >= theta1) ? (theta2 - theta1) : (2 * PI + theta2 - theta1);

        T dist13_ccw = (theta3 >= theta1) ? (theta3 - theta1) : (2 * PI + theta3 - theta1);

        T sweep_angle = 0;


        if (dist12_ccw < dist13_ccw)
        {
            sweep_angle = dist13_ccw;
        }
        else
        {
            sweep_angle = -(2 * PI - dist13_ccw);
        }


        self.data.arc_2d.cx = cx;
        self.data.arc_2d.cy = cy;
        self.data.arc_2d.r = r;
        self.data.arc_2d.start_angle = theta1;


        self.data.arc_2d.end_angle = sweep_angle;
    }


    template <typename T>
    void SolveParallelLine_Dist_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;


        Node<T>* ref_line = graph.GetNode(self.parents[0]);
        const T rx0 = ref_line->data.line_2d.x0;
        const T ry0 = ref_line->data.line_2d.y0;
        const T rx1 = ref_line->data.line_2d.x1;
        const T ry1 = ref_line->data.line_2d.y1;
        const T dist = self.data.parallel_line_2d.distance;


        T dx = rx1 - rx0;
        T dy = ry1 - ry0;
        T len = std::sqrt(dx * dx + dy * dy);


        const T eps = std::numeric_limits<T>::epsilon();
        if (len < eps)
        {
            len = eps * 10;
        }


        T nx = -dy / len;
        T ny = dx / len;


        self.data.parallel_line_2d.x0 = rx0 + nx * dist;
        self.data.parallel_line_2d.y0 = ry0 + ny * dist;
        self.data.parallel_line_2d.x1 = rx1 + nx * dist;
        self.data.parallel_line_2d.y1 = ry1 + ny * dist;
    }

    template <typename T>
    void SolveParallelLine_Point_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;


        Node<T>* ref_line = graph.GetNode(self.parents[0]);
        Node<T>* target_pt = graph.GetNode(self.parents[1]);


        const T rx0 = ref_line->data.line_2d.x0;
        const T ry0 = ref_line->data.line_2d.y0;
        const T rx1 = ref_line->data.line_2d.x1;
        const T ry1 = ref_line->data.line_2d.y1;

        const T rdx = rx1 - rx0;
        const T rdy = ry1 - ry0;


        const T tx = target_pt->data.point_2d.x;
        const T ty = target_pt->data.point_2d.y;


        self.data.line_2d.x0 = tx;
        self.data.line_2d.y0 = ty;
        self.data.line_2d.x1 = tx + rdx;
        self.data.line_2d.y1 = ty + rdy;
    }

    template <typename T>
    void SolveVerticalLine_Point_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;


        Node<T>* ref_line = graph.GetNode(self.parents[0]);
        Node<T>* target_pt = graph.GetNode(self.parents[1]);


        const T rdx = ref_line->data.line_2d.x1 - ref_line->data.line_2d.x0;
        const T rdy = ref_line->data.line_2d.y1 - ref_line->data.line_2d.y0;


        const T vdx = -rdy;
        const T vdy = rdx;


        const T tx = target_pt->data.point_2d.x;
        const T ty = target_pt->data.point_2d.y;


        self.data.line_2d.x0 = tx;
        self.data.line_2d.y0 = ty;
        self.data.line_2d.x1 = tx + vdx;
        self.data.line_2d.y1 = ty + vdy;
    }

    template <typename T>
    void SolveMidPoint_2P_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;

        Node<T>* p1 = graph.GetNode(self.parents[0]);
        Node<T>* p2 = graph.GetNode(self.parents[1]);

        self.data.point_2d.x = (p1->data.point_2d.x + p2->data.point_2d.x) * static_cast<T>(0.5);
        self.data.point_2d.y = (p1->data.point_2d.y + p2->data.point_2d.y) * static_cast<T>(0.5);
    }

    template <typename T>
    void SolveMidPoint_2P_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;

        Node<T>* p1 = graph.GetNode(self.parents[0]);
        Node<T>* p2 = graph.GetNode(self.parents[1]);

        self.data.point_3d.x = (p1->data.point_3d.x + p2->data.point_3d.x) * static_cast<T>(0.5);
        self.data.point_3d.y = (p1->data.point_3d.y + p2->data.point_3d.y) * static_cast<T>(0.5);
        self.data.point_3d.z = (p1->data.point_3d.z + p2->data.point_3d.z) * static_cast<T>(0.5);
    }

    template <typename T>
    void SolveMidPoint_Segment_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;

        Node<T>* seg = graph.GetNode(self.parents[0]);


        self.data.point_2d.x = (seg->data.line_2d.x0 + seg->data.line_2d.x1) * static_cast<T>(0.5);
        self.data.point_2d.y = (seg->data.line_2d.y0 + seg->data.line_2d.y1) * static_cast<T>(0.5);
    }

    template <typename T>
    void SolveMidPoint_Segment_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;

        Node<T>* seg = graph.GetNode(self.parents[0]);


        self.data.point_3d.x = (seg->data.line_3d.x0 + seg->data.line_3d.x1) * static_cast<T>(0.5);
        self.data.point_3d.y = (seg->data.line_3d.y0 + seg->data.line_3d.y1) * static_cast<T>(0.5);
        self.data.point_3d.y = (seg->data.line_3d.y0 + seg->data.line_3d.y1) * static_cast<T>(0.5);
    }


    template <typename T>
    void SolveSnappedPoint_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;


        Node<T>* target = graph.GetNode(self.parents[0]);
        const auto& points = target->result_points_2d;

        if (points.empty()) return;


        T gx = self.data.snap_2d.guess_x;
        T gy = self.data.snap_2d.guess_y;

        T min_dist_sq = std::numeric_limits<T>::max();
        size_t best_idx = 0;


        for (size_t i = 0; i < points.size(); ++i)
        {
            T dx = points[i].x - gx;
            T dy = points[i].y - gy;
            T d2 = dx * dx + dy * dy;
            if (d2 < min_dist_sq)
            {
                min_dist_sq = d2;
                best_idx = i;
            }
        }


        self.data.snap_2d.x = points[best_idx].x;
        self.data.snap_2d.y = points[best_idx].y;

        self.data.snap_2d.guess_x = self.data.snap_2d.x;
        self.data.snap_2d.guess_y = self.data.snap_2d.y;
    }


    template <typename T>
    void SolveSnappedPoint_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;

        Node<T>* target = graph.GetNode(self.parents[0]);
        const auto& points = target->result_points_3d;

        if (points.empty()) return;

        T gx = self.data.snap_3d.guess_x;
        T gy = self.data.snap_3d.guess_y;
        T gz = self.data.snap_3d.guess_z;

        T min_dist_sq = std::numeric_limits<T>::max();
        size_t best_idx = 0;

        for (size_t i = 0; i < points.size(); ++i)
        {
            T dx = points[i].x - gx;
            T dy = points[i].y - gy;
            T dz = points[i].z - gz;
            T d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < min_dist_sq)
            {
                min_dist_sq = d2;
                best_idx = i;
            }
        }

        self.data.snap_3d.x = points[best_idx].x;
        self.data.snap_3d.y = points[best_idx].y;
        self.data.snap_3d.z = points[best_idx].z;

        self.data.snap_3d.guess_x = self.data.snap_3d.x;
        self.data.snap_3d.guess_y = self.data.snap_3d.y;
        self.data.snap_3d.guess_z = self.data.snap_3d.z;
    }

    template <typename T>
    void SolvePolyGen_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;


        Node<T>* center = graph.GetNode(self.parents[0]);


        self.data.poly_gen_2d.cx = center->data.point_2d.x;
        self.data.poly_gen_2d.cy = center->data.point_2d.y;
    }

    template <typename T>
    void SolveTangent_Diagram_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;

        Node<T>* curve_node = graph.GetNode(self.parents[0]);
        Node<T>* pivot_node = graph.GetNode(self.parents[1]);

        const auto& pts = curve_node->result_points_2d;
        if (pts.size() < 2) return;

        const T px = pivot_node->data.point_2d.x;
        const T py = pivot_node->data.point_2d.y;


        size_t idx1 = 0, idx2 = 0;
        T min_d1 = std::numeric_limits<T>::max();
        T min_d2 = std::numeric_limits<T>::max();

        for (size_t i = 0; i < pts.size(); ++i)
        {
            T dx = pts[i].x - px;
            T dy = pts[i].y - py;
            T dist_sq = dx * dx + dy * dy;

            if (dist_sq < min_d1)
            {
                min_d2 = min_d1;
                idx2 = idx1;
                min_d1 = dist_sq;
                idx1 = i;
            }
            else if (dist_sq < min_d2)
            {
                min_d2 = dist_sq;
                idx2 = i;
            }
        }


        T dtx = pts[idx1].x - pts[idx2].x;
        T dty = pts[idx1].y - pts[idx2].y;


        self.data.line_2d.x0 = px;
        self.data.line_2d.y0 = py;

        self.data.line_2d.x1 = px + dtx;
        self.data.line_2d.y1 = py + dty;
    }

    template <typename T>
    void SolvePlaneInfinity_3P_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 3) return;

        Node<T>* n1 = graph.GetNode(self.parents[0]);
        Node<T>* n2 = graph.GetNode(self.parents[1]);
        Node<T>* n3 = graph.GetNode(self.parents[2]);

        const auto& p1 = n1->data.point_3d;
        const auto& p2 = n2->data.point_3d;
        const auto& p3 = n3->data.point_3d;

        const T eps = std::numeric_limits<T>::epsilon();
        const T min_limit = eps * static_cast<T>(10);


        T ux = p2.x - p1.x;
        T uy = p2.y - p1.y;
        T uz = p2.z - p1.z;
        T vx = p3.x - p1.x;
        T vy = p3.y - p1.y;
        T vz = p3.z - p1.z;


        T nx = uy * vz - uz * vy;
        T ny = uz * ux - ux * vz;
        T nz = ux * vy - uy * ux;
        if ((nx * nx + ny * ny + nz * nz) < eps)
        {
            vx += min_limit;
        }


        const auto& ws = graph.world_space_3d;
        T diag_sq = std::pow(ws.x_max - ws.x_min, 2) +
            std::pow(ws.y_max - ws.y_min, 2) +
            std::pow(ws.z_max - ws.z_min, 2);
        T stretch_len = std::sqrt(diag_sq) * static_cast<T>(2.0);


        auto normalize_and_stretch = [&](T& x, T& y, T& z)
        {
            T mag = std::sqrt(x * x + y * y + z * z);
            if (mag < eps) mag = eps;
            x = (x / mag) * stretch_len;
            y = (y / mag) * stretch_len;
            z = (z / mag) * stretch_len;
        };

        normalize_and_stretch(ux, uy, uz);
        normalize_and_stretch(vx, vy, vz);


        auto& plane = self.data.plane_3d;
        plane.ox = p1.x - static_cast<T>(0.5) * ux - static_cast<T>(0.5) * vx;
        plane.oy = p1.y - static_cast<T>(0.5) * uy - static_cast<T>(0.5) * vy;
        plane.oz = p1.z - static_cast<T>(0.5) * uz - static_cast<T>(0.5) * vz;

        plane.ux = ux;
        plane.uy = uy;
        plane.uz = uz;

        plane.vx = vx;
        plane.vy = vy;
        plane.vz = vz;
    }

    /**
         * @brief 工业级鲁棒性 3D 图解切面求解器 (PCA + Jacobi 实现)
         * 严禁简化版本：处理离散点云、重复点及轴对齐格栅化带来的退化风险
         */
    template <typename T>
    void SolveTangentPlane_Diagram_3D(Graph<T>& graph, Node<T>& self)
    {
        // --- 1. 基础物理依赖校验 ---
        if (self.parents.size() < 2) return;

        Node<T>* obj_node = graph.GetNode(self.parents[0]); // 被切的目标物体
        Node<T>* pivot_node = graph.GetNode(self.parents[1]); // 切点(吸附点)

        const auto& pts = obj_node->result_points_3d;
        if (pts.size() < 10) return; // 局部点云太稀疏，无法确定平面

        // 获取当前切点坐标 (来自于 SnapPoint 的结果)
        const T px = pivot_node->data.snap_3d.x;
        const T py = pivot_node->data.snap_3d.y;
        const T pz = pivot_node->data.snap_3d.z;

        // --- 2. 邻域搜寻 (K-Nearest Neighbors) ---
        // 选取 100 个点以获得稳定的统计特征，抵消八叉树采样的格栅效应
        const size_t K = std::min(pts.size(), (size_t)100);
        struct Neighbor
        {
            size_t idx;
            T dist_sq;
        };
        std::vector<Neighbor> neighbors;
        neighbors.reserve(pts.size());

        for (size_t i = 0; i < pts.size(); ++i)
        {
            T dx = pts[i].x - px;
            T dy = pts[i].y - py;
            T dz = pts[i].z - pz;
            neighbors.push_back({i, dx * dx + dy * dy + dz * dz});
        }

        // 仅对前 K 个最近的点进行部分排序，优化离线计算性能
        std::partial_sort(neighbors.begin(), neighbors.begin() + K, neighbors.end(),
                          [](const Neighbor& a, const Neighbor& b)
                          {
                              return a.dist_sq < b.dist_sq;
                          });

        // --- 3. 计算重心与协方差矩阵 ---
        T avg_x = 0, avg_y = 0, avg_z = 0;
        for (size_t i = 0; i < K; ++i)
        {
            const auto& p = pts[neighbors[i].idx];
            avg_x += p.x;
            avg_y += p.y;
            avg_z += p.z;
        }
        avg_x /= static_cast<T>(K);
        avg_y /= static_cast<T>(K);
        avg_z /= static_cast<T>(K);

        // 构建对称协方差矩阵
        T cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        for (size_t i = 0; i < K; ++i)
        {
            const auto& p = pts[neighbors[i].idx];
            T dx = p.x - avg_x;
            T dy = p.y - avg_y;
            T dz = p.z - avg_z;
            cov[0][0] += dx * dx;
            cov[0][1] += dx * dy;
            cov[0][2] += dx * dz;
            cov[1][1] += dy * dy;
            cov[1][2] += dy * dz;
            cov[2][2] += dz * dz;
        }
        // 补全对称位
        cov[1][0] = cov[0][1];
        cov[2][0] = cov[0][2];
        cov[2][1] = cov[1][2];

        // --- 4. 雅可比旋转法 (Jacobi Eigenvalue Algorithm) ---
        // 目标：将 cov 矩阵对角化，求出 3 个相互正交的特征向量
        T V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 存储特征向量的矩阵
        const int max_iterations = 50; // 对于 3x3 矩阵，50次迭代足以达到机器精度
        const T eps = std::numeric_limits<T>::epsilon();

        for (int iter = 0; iter < max_iterations; ++iter)
        {
            // 4a. 寻找矩阵中绝对值最大的非对角元素 cov[p][q]
            int p = 0, q = 1;
            T max_val = std::abs(cov[0][1]);
            if (std::abs(cov[0][2]) > max_val)
            {
                p = 0;
                q = 2;
                max_val = std::abs(cov[0][2]);
            }
            if (std::abs(cov[1][2]) > max_val)
            {
                p = 1;
                q = 2;
                max_val = std::abs(cov[1][2]);
            }

            // 如果最大的非对角元素已经足够小，说明已经完成对角化
            if (max_val < eps * 1e-4) break;

            // 4b. 计算 Jacobi 旋转角度 theta
            T theta = static_cast<T>(0.5) * std::atan2(static_cast<T>(2.0) * cov[p][q], cov[q][q] - cov[p][p]);
            T cos_t = std::cos(theta);
            T sin_t = std::sin(theta);

            // 4c. 更新协方差矩阵 (旋转变换：D = J^T * Cov * J)
            T app = cos_t * cos_t * cov[p][p] - static_cast<T>(2.0) * sin_t * cos_t * cov[p][q] + sin_t * sin_t * cov[q]
                [q];
            T aqq = sin_t * sin_t * cov[p][p] + static_cast<T>(2.0) * sin_t * cos_t * cov[p][q] + cos_t * cos_t * cov[q]
                [q];
            T apq = (cos_t * cos_t - sin_t * sin_t) * cov[p][q] + sin_t * cos_t * (cov[p][p] - cov[q][q]);

            cov[p][p] = app;
            cov[q][q] = aqq;
            cov[p][q] = cov[q][p] = 0; // 强制置零

            // 更新其余不直接参与旋转的元素
            for (int i = 0; i < 3; ++i)
            {
                if (i != p && i != q)
                {
                    T aip = cov[i][p];
                    T aiq = cov[i][q];
                    cov[i][p] = cov[p][i] = cos_t * aip - sin_t * aiq;
                    cov[i][q] = cov[q][i] = sin_t * aip + cos_t * aiq;
                }
            }

            // 4d. 更新特征向量矩阵 V (累积旋转)
            for (int i = 0; i < 3; ++i)
            {
                T vip = V[i][p];
                T viq = V[i][q];
                V[i][p] = cos_t * vip - sin_t * viq;
                V[i][q] = sin_t * vip + cos_t * viq;
            }
        }

        // --- 5. 排序特征向量，提取切平面基底 ---
        // 对角线上的元素 cov[i][i] 即为特征值 (方差)
        T eigenvalues[3] = {cov[0][0], cov[1][1], cov[2][2]};
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int b)
        {
            return eigenvalues[a] > eigenvalues[b];
        });

        // 提取前两个主成分（方差最大的两个方向）作为切平面的 U 和 V 向量
        // 最小特征向量 order[2] 则是该处的表面法线
        T ux = V[0][order[0]], uy = V[1][order[0]], uz = V[2][order[0]];
        T vx = V[0][order[1]], vy = V[1][order[1]], vz = V[2][order[1]];

        // --- 6. 无限大拉伸逻辑 ---
        // 为了确保平面覆盖视野，计算世界空间的对角线跨度
        const auto& ws = graph.world_space_3d;
        T diag_sq = std::pow(ws.x_max - ws.x_min, 2) +
            std::pow(ws.y_max - ws.y_min, 2) +
            std::pow(ws.z_max - ws.z_min, 2);
        T stretch_factor = std::sqrt(diag_sq) * static_cast<T>(5.0); // 5倍余量保证无限感

        // 应用拉伸
        ux *= stretch_factor;
        uy *= stretch_factor;
        uz *= stretch_factor;
        vx *= stretch_factor;
        vy *= stretch_factor;
        vz *= stretch_factor;

        // --- 7. 数据落盘与对齐 ---
        auto& plane_data = self.data.plane_3d;

        // 设置平面原点 O，使平面几何中心正好落在吸附点 (px, py, pz) 上
        // P(s,t) = O + sU + tV -> 居中则 s=0.5, t=0.5 对应切点
        plane_data.ox = px - static_cast<T>(0.5) * ux - static_cast<T>(0.5) * vx;
        plane_data.oy = py - static_cast<T>(0.5) * uy - static_cast<T>(0.5) * vy;
        plane_data.oz = pz - static_cast<T>(0.5) * uz - static_cast<T>(0.5) * vz;

        plane_data.ux = ux;
        plane_data.uy = uy;
        plane_data.uz = uz;

        plane_data.vx = vx;
        plane_data.vy = vy;
        plane_data.vz = vz;
    }


    /**
     * @brief 3D 三角形求解器
     * 计算逻辑与平面一致，但通过函数指针区分语义
     */
    template <typename T>
    void SolveTriangle_3Points_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 3) return;

        Node<T>* n1 = graph.GetNode(self.parents[0]);
        Node<T>* n2 = graph.GetNode(self.parents[1]);
        Node<T>* n3 = graph.GetNode(self.parents[2]);

        const auto& p1 = n1->data.point_3d;
        const auto& p2 = n2->data.point_3d;
        const auto& p3 = n3->data.point_3d;

        auto& d = self.data.plane_3d;

        // 准备机器精度
        const T eps = std::numeric_limits<T>::epsilon();
        const T min_limit = eps * static_cast<T>(10.0);

        auto safe_diff = [&](T a, T b) -> T
        {
            T diff = a - b;
            if (std::abs(diff) < eps)
            {
                return (diff >= 0) ? min_limit : -min_limit;
            }
            return diff;
        };

        // 1. 设置三角形原点 (Vertex 0)
        d.ox = p1.x;
        d.oy = p1.y;
        d.oz = p1.z;

        // 2. 计算边向量 U (V1 - V0) 和 V (V2 - V0)
        d.ux = safe_diff(p2.x, p1.x);
        d.uy = safe_diff(p2.y, p1.y);
        d.uz = safe_diff(p2.z, p1.z);

        d.vx = safe_diff(p3.x, p1.x);
        d.vy = safe_diff(p3.y, p1.y);
        d.vz = safe_diff(p3.z, p1.z);

        // 3. 共线/退化检测与微扰动保护
        T nx = d.uy * d.vz - d.uz * d.vy;
        T ny = d.uz * d.ux - d.ux * d.vz;
        T nz = d.ux * d.vy - d.uy * d.ux;

        if ((nx * nx + ny * ny + nz * nz) < eps)
        {
            // 如果三点共线，强制微调 V 向量使其产生极小面积
            if (std::abs(d.ux) < std::abs(d.uy)) d.vx += min_limit;
            else d.vy += min_limit;
        }
    }

    template <typename T>
    void SolvePlatonicSolid_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;

        // 获取中心点
        Node<T>* center = graph.GetNode(self.parents[0]);

        // 同步中心坐标
        self.data.platonic_solid_3d.cx = center->data.point_3d.x;
        self.data.platonic_solid_3d.cy = center->data.point_3d.y;
        self.data.platonic_solid_3d.cz = center->data.point_3d.z;
    }

    template <typename T>
    void SolveSphere_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;
        Node<T>* center = graph.GetNode(self.parents[0]);

        self.data.sphere_3d.cx = center->data.point_3d.x;
        self.data.sphere_3d.cy = center->data.point_3d.y;
        self.data.sphere_3d.cz = center->data.point_3d.z;
    }

    template <typename T>
    void SolveSphere_4P_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 4) return;

        // 1. 获取四个点的坐标
        Point3D<T> p[4];
        for (int i = 0; i < 4; ++i)
        {
            auto* n = graph.GetNode(self.parents[i]);
            p[i] = {n->data.point_3d.x, n->data.point_3d.y, n->data.point_3d.z};
        }

        // 2. 建立线性方程组：2(x_i - x_1)x + 2(y_i - y_1)y + 2(z_i - z_1)z = (x_i^2+y_i^2+z_i^2) - (x_1^2+y_1^2+z_1^2)
        // 令 A * [cx, cy, cz]^T = B
        T a[3][3], b[3];
        T s1 = p[0].x * p[0].x + p[0].y * p[0].y + p[0].z * p[0].z;

        for (int i = 0; i < 3; ++i)
        {
            a[i][0] = 2 * (p[i + 1].x - p[0].x);
            a[i][1] = 2 * (p[i + 1].y - p[0].y);
            a[i][2] = 2 * (p[i + 1].z - p[0].z);
            b[i] = (p[i + 1].x * p[i + 1].x + p[i + 1].y * p[i + 1].y + p[i + 1].z * p[i + 1].z) - s1;
        }

        // 3. 计算行列式 (Cramer's rule)
        T det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
            a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
            a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);

        // 4. 机器精度钳制 (针对退化情况：如四点共面)
        const T eps = std::numeric_limits<T>::epsilon();
        if (std::abs(det) < eps)
        {
            det = (det >= 0) ? eps * 10 : -eps * 10;
        }

        // 5. 计算圆心 (利用行列式代换)
        auto get_det = [&](T c0, T c1, T c2)
        {
            return c0 * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                a[0][1] * (c1 * a[2][2] - a[1][2] * c2) +
                a[0][2] * (c1 * a[2][1] - a[1][1] * c2);
        };

        // cx = det(B, col2, col3) / det ... 依此类推
        T cx = (b[0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
            a[0][1] * (b[1] * a[2][2] - a[1][2] * b[2]) +
            a[0][2] * (b[1] * a[2][1] - a[1][1] * b[2])) / det;

        T cy = (a[0][0] * (b[1] * a[2][2] - a[1][2] * b[2]) -
            b[0] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
            a[0][2] * (a[1][0] * b[2] - b[1] * a[2][0])) / det;

        T cz = (a[0][0] * (a[1][1] * b[2] - b[1] * a[2][1]) -
            a[0][1] * (a[1][0] * b[2] - b[1] * a[2][0]) +
            b[0] * (a[1][0] * a[2][1] - a[1][1] * a[2][0])) / det;

        // 6. 写入结果
        self.data.sphere_3d.cx = cx;
        self.data.sphere_3d.cy = cy;
        self.data.sphere_3d.cz = cz;

        // 半径 = 距离圆心到任意一点的距离
        T dx = cx - p[0].x;
        T dy = cy - p[0].y;
        T dz = cz - p[0].z;
        self.data.sphere_3d.r = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    namespace utils
    {
        template <typename T>
        struct IntersectionCell
        {
            uint32_t unique_parents = 0; // 有多少个不同的父对象经过了这里
            uint32_t last_p_idx = 0xFFFFFFFF; // 上一个贡献计数的父对象索引
            uint32_t total_points = 0; // 总采样点数（用于算重心）
            T sum_x = 0;
            T sum_y = 0;
        };
    }

    template <typename T>
    void SolveIntersectionPoint_2D(Graph<T>& graph, Node<T>& self)
    {
        const uint32_t num_parents = static_cast<uint32_t>(self.parents.size());
        if (num_parents < 2) return;

        // 1. 获取物理环境参数
        const auto& ws = graph.world_space_2d;
        const auto& res = graph.resolution_2d;
        const T dx = (ws.x_max - ws.x_min) / res.x;
        const T dy = (ws.y_max - ws.y_min) / res.y;

        if (dx <= 0 || dy <= 0) return;

        // 2. 定义格栅单元结构
        struct IntersectionCell2D
        {
            uint32_t unique_votes = 0; // 有多少个不同的父对象占领了此格
            uint32_t last_voter_idx = 0xFFFFFFFF; // 记录上一个投票者，防止单个对象刷票
            uint32_t point_count = 0; // 落入此格的总采样点数
            T sum_x = 0; // 坐标累加，用于计算重心
            T sum_y = 0;
        };

        // 3. 初始化空间哈希 (使用 FlatMap)
        // Key: ix << 32 | iy (2D 空间索引压缩)
        utils::FlatMap<uint64_t, IntersectionCell2D> grid;

        // --- 第一阶段：黑盒投影与唯一性投票 ---
        for (uint32_t p_idx = 0; p_idx < num_parents; ++p_idx)
        {
            Node<T>* parent = graph.GetNode(self.parents[p_idx]);
            const auto& pts = parent->result_points_2d;

            for (const auto& pt : pts)
            {
                // 浮点映射到整数格栅
                int64_t ix = static_cast<int64_t>(std::floor((pt.x - ws.x_min) / dx));
                int64_t iy = static_cast<int64_t>(std::floor((pt.y - ws.y_min) / dy));

                uint64_t key = (static_cast<uint64_t>(ix) << 32) | (static_cast<uint32_t>(iy));
                auto& cell = grid[key];

                // 核心逻辑：如果此物体在这个格子里还没投过票
                if (cell.last_voter_idx != p_idx)
                {
                    cell.unique_votes++;
                    cell.last_voter_idx = p_idx;
                }

                // 累加点数据用于计算平滑重心
                cell.sum_x += pt.x;
                cell.sum_y += pt.y;
                cell.point_count++;
            }
        }

        // --- 第二阶段：全局候选集择优 ---
        const T gx = self.data.snap_2d.guess_x;
        const T gy = self.data.snap_2d.guess_y;
        T min_dist_sq = std::numeric_limits<T>::max();
        Point2D<T> best_pt = {gx, gy};
        bool found = false;

        for (auto& entry : grid)
        {
            auto& cell = entry.second;

            // 只有被【所有】参与对象共同占据的格子，才被视为有效的交点区域
            if (cell.unique_votes == num_parents)
            {
                // 计算该格内所有点的平均位置（实现亚像素平滑）
                T cand_x = cell.sum_x / cell.point_count;
                T cand_y = cell.sum_y / cell.point_count;

                // 在所有可能的交点中，寻找离当前猜测点（历史位置）最近的那一个
                T d2 = (cand_x - gx) * (cand_x - gx) + (cand_y - gy) * (cand_y - gy);
                if (d2 < min_dist_sq)
                {
                    min_dist_sq = d2;
                    best_pt = {cand_x, cand_y};
                    found = true;
                }
            }
        }

        // --- 第三阶段：状态更新与轮换 ---
        if (found)
        {
            // 找到交点：执行“传送”
            self.data.snap_2d.x = best_pt.x;
            self.data.snap_2d.y = best_pt.y;
            // 轮换逻辑：当前位置作为下一次 Compute 的猜测点，实现持续追踪
            self.data.snap_2d.guess_x = best_pt.x;
            self.data.snap_2d.guess_y = best_pt.y;
        }
        else
        {
            // 未找到交点：保持在原猜测位置（可能几何体移开了）
            self.data.snap_2d.x = gx;
            self.data.snap_2d.y = gy;
        }
    }

    namespace utils
    {
        /**
         * @brief 3D 交点桶结构
         */
        template <typename T>
        struct IntersectionCell3D
        {
            uint32_t unique_parents_count = 0; // 占据该体素的独立父对象数量
            uint32_t last_parent_index = 0xFFFFFFFF; // 上一个在该体素打点的父对象索引（去重用）
            uint32_t total_points_in_cell = 0; // 该体素内所有采样点的总数
            T sum_x = 0; // 用于计算重心
            T sum_y = 0;
            T sum_z = 0;
        };
    }

    template <typename T>
    void SolveIntersectionCurve_3D(Graph<T>& graph, Node<T>& self)
    {
        const uint32_t num_parents = static_cast<uint32_t>(self.parents.size());
        if (num_parents < 2) return;

        const auto& ws = graph.world_space_3d;
        const auto& res = graph.resolution_3d;
        const T dx = (ws.x_max - ws.x_min) / res.x;
        const T dy = (ws.y_max - ws.y_min) / res.y;
        const T dz = (ws.z_max - ws.z_min) / res.z;
        if (dx <= 0 || dy <= 0 || dz <= 0) return;

        struct IntersectionCell
        {
            uint32_t unique_votes = 0;
            uint32_t last_voter_idx = 0xFFFFFFFF;
            uint32_t total_points = 0;
            T sx = 0, sy = 0, sz = 0;
        };

        auto get_key = [](int64_t ix, int64_t iy, int64_t iz) -> uint64_t
        {
            uint64_t ux = static_cast<uint64_t>(ix + 1000000) & 0x1FFFFF;
            uint64_t uy = static_cast<uint64_t>(iy + 1000000) & 0x1FFFFF;
            uint64_t uz = static_cast<uint64_t>(iz + 1000000) & 0x1FFFFF;
            return (ux << 42) | (uy << 21) | uz;
        };

        // 初始容量设大一点，减少 rehash
        utils::FlatMap<uint64_t, IntersectionCell> grid(200000);

        for (uint32_t p_idx = 0; p_idx < num_parents; ++p_idx)
        {
            Node<T>* parent = graph.GetNode(self.parents[p_idx]);
            const auto& pts = parent->result_points_3d;

            for (const auto& pt : pts)
            {
                int64_t ix = static_cast<int64_t>(std::floor((pt.x - ws.x_min) / dx));
                int64_t iy = static_cast<int64_t>(std::floor((pt.y - ws.y_min) / dy));
                int64_t iz = static_cast<int64_t>(std::floor((pt.z - ws.z_min) / dz));

                uint64_t key = get_key(ix, iy, iz);
                auto& cell = grid[key];

                if (cell.last_voter_idx != p_idx)
                {
                    cell.unique_votes++;
                    cell.last_voter_idx = p_idx;
                }

                cell.sx += pt.x;
                cell.sy += pt.y;
                cell.sz += pt.z;
                cell.total_points++;
            }
        }

        // 重要：显式清空并填充
        self.result_points_3d.clear();
        self.result_points_3d.reserve(2000); // 预估交线点数

        for (auto it = grid.begin(); it != grid.end(); ++it)
        {
            const auto& cell = it->second;
            if (cell.unique_votes == num_parents)
            {
                T inv_count = static_cast<T>(1.0) / static_cast<T>(cell.total_points);
                self.result_points_3d.emplace_back(Point3D<T>{
                    cell.sx * inv_count,
                    cell.sy * inv_count,
                    cell.sz * inv_count
                });
            }
        }
    }


    template <typename T>
    void SolveRectangle_2D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;

        // 获取父节点（中心点）
        Node<T>* center = graph.GetNode(self.parents[0]);

        // 同步中心点坐标到矩形数据块中
        self.data.rect_2d.cx = center->data.point_2d.x;
        self.data.rect_2d.cy = center->data.point_2d.y;
    }

    template <typename T>
    void SolveCuboid_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.empty()) return;
        Node<T>* center = graph.GetNode(self.parents[0]);

        // 同步中心位置
        self.data.cuboid_3d.cx = center->data.point_3d.x;
        self.data.cuboid_3d.cy = center->data.point_3d.y;
        self.data.cuboid_3d.cz = center->data.point_3d.z;
    }


    template <typename T>
    void SolveCone_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;
        auto* apex = graph.GetNode(self.parents[0]);
        auto* base = graph.GetNode(self.parents[1]);

        self.data.cone_3d.apex_x = apex->data.point_3d.x;
        self.data.cone_3d.apex_y = apex->data.point_3d.y;
        self.data.cone_3d.apex_z = apex->data.point_3d.z;

        self.data.cone_3d.base_x = base->data.point_3d.x;
        self.data.cone_3d.base_y = base->data.point_3d.y;
        self.data.cone_3d.base_z = base->data.point_3d.z;
    }


    /**
 * @brief 3D 圆柱面求解器
 * 从两个父节点(Point3D)提取坐标并存入 cylinder_3d 数据结构
 */
    template <typename T>
    void SolveCylinder_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;

        // 获取两个端点节点
        Node<T>* n1 = graph.GetNode(self.parents[0]);
        Node<T>* n2 = graph.GetNode(self.parents[1]);

        // 同步物理坐标
        auto& d = self.data.cylinder_3d;
        d.p1x = n1->data.point_3d.x;
        d.p1y = n1->data.point_3d.y;
        d.p1z = n1->data.point_3d.z;

        d.p2x = n2->data.point_3d.x;
        d.p2y = n2->data.point_3d.y;
        d.p2z = n2->data.point_3d.z;
    }


    template <typename T>
    void SolveCircle_3D(Graph<T>& graph, Node<T>& self)
    {
        if (self.parents.size() < 2) return;
        auto* center = graph.GetNode(self.parents[0]);
        auto* normal = graph.GetNode(self.parents[1]);

        auto& d = self.data.circle_3d;
        d.cx = center->data.point_3d.x;
        d.cy = center->data.point_3d.y;
        d.cz = center->data.point_3d.z;

        d.nx = normal->data.point_3d.x;
        d.ny = normal->data.point_3d.y;
        d.nz = normal->data.point_3d.z;
    }


    template <typename T>
void SolveImplicit_2D(Graph<T>& graph, Node<T>& self) {
        auto& c = self.data.implicit_2d_config;

        // 1. 同步范围：使隐函数绘制范围与 Graph 世界空间完全一致
        c.x_min = graph.world_space_2d.x_min;
        c.x_max = graph.world_space_2d.x_max;
        c.y_min = graph.world_space_2d.y_min;
        c.y_max = graph.world_space_2d.y_max;

        // 2. 同步精度：将四叉树采样阈值设置为 Graph 的“物理像素”大小
        // 采样阈值 = 跨度 / 分辨率
        T span_x = c.x_max - c.x_min;
        T span_y = c.y_max - c.y_min;

        T px_x = (graph.resolution_2d.x > 0) ? (span_x / graph.resolution_2d.x) : static_cast<T>(0.01);
        T px_y = (graph.resolution_2d.y > 0) ? (span_y / graph.resolution_2d.y) : static_cast<T>(0.01);

        // 取较细的一个作为采样阈值
        c.sampling_threshold = std::min(px_x, px_y);
    }


    template <typename T>
void SolveImplicit_3D(Graph<T>& graph, Node<T>& self) {
        auto& c = self.data.implicit_3d_config;

        // 1. 同步 3D 空间范围
        c.x_min = graph.world_space_3d.x_min;
        c.x_max = graph.world_space_3d.x_max;
        c.y_min = graph.world_space_3d.y_min;
        c.y_max = graph.world_space_3d.y_max;
        c.z_min = graph.world_space_3d.z_min;
        c.z_max = graph.world_space_3d.z_max;

        // 2. 同步精度：计算最小体素边长作为采样阈值
        T sx = (graph.resolution_3d.x > 0) ? (c.x_max - c.x_min) / graph.resolution_3d.x : 0.1;
        T sy = (graph.resolution_3d.y > 0) ? (c.y_max - c.y_min) / graph.resolution_3d.y : 0.1;
        T sz = (graph.resolution_3d.z > 0) ? (c.z_max - c.z_min) / graph.resolution_3d.z : 0.1;

        // 取三轴中最精细的作为采样阈值
        c.sampling_threshold = std::min({sx, sy, sz});
    }


    template <typename T>
void SolveParametric_2D(Graph<T>& graph, Node<T>& self) {
        auto& c = self.data.parametric_2d_config;

        // 1. 同步世界空间范围（用于 StuPlot 内部的视口剔除）
        c.x_min = graph.world_space_2d.x_min;
        c.x_max = graph.world_space_2d.x_max;
        c.y_min = graph.world_space_2d.y_min;
        c.y_max = graph.world_space_2d.y_max;

        // 2. 同步精度：点间距
        // 步长建议设置为 1 个物理像素的大小，以保证线条连续
        T px_x = (graph.resolution_2d.x > 0) ? (c.x_max - c.x_min) / graph.resolution_2d.x : 0.05;
        T px_y = (graph.resolution_2d.y > 0) ? (c.y_max - c.y_min) / graph.resolution_2d.y : 0.05;

        c.point_spacing = std::min(px_x, px_y);
    }


    template <typename T>
void SolveParametric_3D(Graph<T>& graph, Node<T>& self) {
        auto& c = self.data.parametric_3d_config;

        // 1. 同步世界空间范围（用于参数曲面的 AABB 裁剪）
        c.x_min = graph.world_space_3d.x_min;
        c.x_max = graph.world_space_3d.x_max;
        c.y_min = graph.world_space_3d.y_min;
        c.y_max = graph.world_space_3d.y_max;
        c.z_min = graph.world_space_3d.z_min;
        c.z_max = graph.world_space_3d.z_max;

        // 2. 同步精度：根据分辨率计算点间距
        T sx = (graph.resolution_3d.x > 0) ? (c.x_max - c.x_min) / graph.resolution_3d.x : 0.1;
        T sy = (graph.resolution_3d.y > 0) ? (c.y_max - c.y_min) / graph.resolution_3d.y : 0.1;
        T sz = (graph.resolution_3d.z > 0) ? (c.z_max - c.z_min) / graph.resolution_3d.z : 0.1;

        // 采样点间距取三轴中最精细的一个
        c.point_spacing = std::min({sx, sy, sz});
    }
}
