#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include "../types/point.hpp"
#include "vector"
#include "utils/platonic_data.hpp"
#include "../utils/math_traits.hpp"
#include "../utils/interval.hpp"
#include "../plot/implicit_2d.hpp"
#include "../plot/implicit_3d.hpp"
#include "../plot/parametric_2d.hpp"
#include "../plot/parametric_3d.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Graph;
    template <typename T>
    struct Node;

    using std::abs;
    using std::ceil;

    template <typename T>
    bool LiangBarskyClipTest(T p, T q, T& t1, T& t2)
    {
        if (p < 0)
        {
            T r = q / p;
            if (r > t2) return false;
            if (r > t1) t1 = r;
        }
        else if (p > 0)
        {
            T r = q / p;
            if (r < t1) return false;
            if (r < t2) t2 = r;
        }
        else if (q < 0) return false;
        return true;
    }

    // --- 内部通用 2D 逻辑 ---
    template <typename T>
    void InternalPlotLine_2D(Graph<T>& graph, Node<T>& self, T t_min, T t_max)
    {
        const auto& ws = graph.world_space_2d;
        const auto& ld = self.data.line_2d;
        const T dx = ld.x1 - ld.x0;
        const T dy = ld.y1 - ld.y0;

        // 裁剪
        T t1 = t_min, t2 = t_max;
        if (!LiangBarskyClipTest(-dx, ld.x0 - ws.x_min, t1, t2)) return;
        if (!LiangBarskyClipTest(dx, ws.x_max - ld.x0, t1, t2)) return;
        if (!LiangBarskyClipTest(-dy, ld.y0 - ws.y_min, t1, t2)) return;
        if (!LiangBarskyClipTest(dy, ws.y_max - ld.y0, t1, t2)) return;

        const T x_start = ld.x0 + t1 * dx, y_start = ld.y0 + t1 * dy;
        const T x_end = ld.x0 + t2 * dx, y_end = ld.y0 + t2 * dy;

        // 采样率计算
        const T step_x = (ws.x_max - ws.x_min) / graph.resolution_2d.x;
        const T step_y = (ws.y_max - ws.y_min) / graph.resolution_2d.y;
        const auto num_points = static_cast<size_t>(ceil(std::max(abs(x_end - x_start) / step_x,
                                                                  abs(y_end - y_start) / step_y)));


        self.result_points_2d.reserve(num_points + 1);
        for (size_t i = 0; i <= num_points; ++i)
        {
            T t = static_cast<T>(i) / static_cast<T>(num_points);
            self.result_points_2d.emplace_back(Point2D<T>{
                x_start + t * (x_end - x_start),
                y_start + t * (y_end - y_start)
            });
        }

        if (num_points == 0)
        {
            // 退化保护：仅当线段确实退化（dx==0 且 dy==0）且裁剪后的起点在视口内时，才输出一个点
            const T zero = static_cast<T>(0);
            if (dx == zero && dy == zero)
            {
                if (x_start >= ws.x_min && x_start <= ws.x_max &&
                    y_start >= ws.y_min && y_start <= ws.y_max)
                {
                    self.result_points_2d.emplace_back(Point2D<T>{x_start, y_start});
                }
            }
        }
    }

    // --- 内部通用 3D 逻辑 ---
    template <typename T>
    void InternalPlotLine_3D(Graph<T>& graph, Node<T>& self, T t_min, T t_max)
    {
        const auto& ws = graph.world_space_3d;
        const auto& ld = self.data.line_3d;
        const T dx = ld.x1 - ld.x0, dy = ld.y1 - ld.y0, dz = ld.z1 - ld.z0;

        T t1 = t_min, t2 = t_max;
        if (!LiangBarskyClipTest(-dx, ld.x0 - ws.x_min, t1, t2)) return;
        if (!LiangBarskyClipTest(dx, ws.x_max - ld.x0, t1, t2)) return;
        if (!LiangBarskyClipTest(-dy, ld.y0 - ws.y_min, t1, t2)) return;
        if (!LiangBarskyClipTest(dy, ws.y_max - ld.y0, t1, t2)) return;
        if (!LiangBarskyClipTest(-dz, ld.z0 - ws.z_min, t1, t2)) return;
        if (!LiangBarskyClipTest(dz, ws.z_max - ld.z0, t1, t2)) return;

        const T x_s = ld.x0 + t1 * dx, y_s = ld.y0 + t1 * dy, z_s = ld.z0 + t1 * dz;
        const T x_e = ld.x0 + t2 * dx, y_e = ld.y0 + t2 * dy, z_e = ld.z0 + t2 * dz;

        const T sx = 0.5 * (ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T sy = 0.5 * (ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T sz = 0.5 * (ws.z_max - ws.z_min) / graph.resolution_3d.z;

        const auto num_points = static_cast<size_t>(ceil(std::max({
            abs(x_e - x_s) / sx, abs(y_e - y_s) / sy, abs(z_e - z_s) / sz
        })));



        self.result_points_3d.reserve(num_points + 1);
        for (size_t i = 0; i <= num_points; ++i)
        {
            T t = static_cast<T>(i) / static_cast<T>(num_points);
            self.result_points_3d.emplace_back(Point3D<T>{
                x_s + t * (x_e - x_s), y_s + t * (y_e - y_s), z_s + t * (z_e - z_s)
            });
        }


        if (num_points == 0)
        {
            // 仅当线段确实退化（端点重合）且该点在视口内时才输出一个点
            const T zero = static_cast<T>(0);
            if (dx == zero && dy == zero && dz == zero)
            {
                if (x_s >= ws.x_min && x_s <= ws.x_max &&
                    y_s >= ws.y_min && y_s <= ws.y_max &&
                    z_s >= ws.z_min && z_s <= ws.z_max)
                {
                    self.result_points_3d.emplace_back(Point3D<T>{x_s, y_s, z_s});
                }
            }
        }
    }

    // --- 2D Plotters ---
    template <typename T>
    void PlotStraightLine_2D(Graph<T>& graph, Node<T>& self)
    {
        constexpr long double MARKER_LD = -0x1.BAADC0DEp+300L;
        const T INVALID_VAL = static_cast<T>(MARKER_LD);
        if (self.data.line_2d.x0 == INVALID_VAL) return; // 退化直线，不绘制

        InternalPlotLine_2D(graph, self, -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::infinity());
    }

    template <typename T>
    void PlotSegment_2D(Graph<T>& graph, Node<T>& self)
    {
        InternalPlotLine_2D(graph, self, T(0.0), T(1.0));
    }

    template <typename T>
    void PlotRay_2D(Graph<T>& graph, Node<T>& self)
    {
        InternalPlotLine_2D(graph, self, T(0.0), std::numeric_limits<T>::infinity());
    }

    // --- 3D Plotters ---
    template <typename T>
    void PlotStraightLine_3D(Graph<T>& graph, Node<T>& self)
    {
        InternalPlotLine_3D(graph, self, -std::numeric_limits<T>::infinity(), std::numeric_limits<T>::infinity());
    }

    template <typename T>
    void PlotSegment_3D(Graph<T>& graph, Node<T>& self)
    {
        InternalPlotLine_3D(graph, self, T(0.0), T(1.0));
    }

    template <typename T>
    void PlotRay_3D(Graph<T>& graph, Node<T>& self)
    {
        InternalPlotLine_3D(graph, self, T(0.0), std::numeric_limits<T>::infinity());
    }


    namespace utils
    {
        template <typename T>
        void lerp_and_push(std::vector<Point2D<T>>& res, T x1, T y1, T x2, T y2, T v1, T v2)
        {
            if ((v1 > 0 && v2 <= 0) || (v1 < 0 && v2 >= 0))
            {
                T t = -v1 / (v2 - v1);
                res.emplace_back(Point2D<T>{x1 + t * (x2 - x1), y1 + t * (y2 - y1)});
            }
        }
    }

    /**
     * @brief 使用原子级特化区间函数绘制 2D 圆形
     * 核心提升：evaluate_circle_implicit 消除了 (x-a)*(x-a) 的变量相关性冗余
     */
    template <typename T>
    void PlotCircle_2D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 基础几何参数提取
        const T cx = self.data.circle_2d.cx;
        const T cy = self.data.circle_2d.cy;
        const T r_sq = self.data.circle_2d.r * self.data.circle_2d.r;
        constexpr T MARKER_LD = -0x1.BAADC0DEp+300L;
        const T INVALID_VAL = static_cast<T>(MARKER_LD);
        if (self.data.circle_2d.r == INVALID_VAL)   // cx,cy,r 均为此值
        {
            // 备份原始的 circle_2d 数据，防止 union 覆盖导致数据丢失
            auto backup_circle = self.data.circle_2d;

            // 从父节点获取原始三点
            Node<T>* p0 = graph.GetNode(self.parents[0]);
            Node<T>* p2 = graph.GetNode(self.parents[2]);

            // 取首尾点构造直线（三点共线，任意两不同点即可）
            self.data.line_2d.x0 = p0->data.point_2d.x;
            self.data.line_2d.y0 = p0->data.point_2d.y;
            self.data.line_2d.x1 = p2->data.point_2d.x;
            self.data.line_2d.y1 = p2->data.point_2d.y;

            // 使用现有的直线绘制函数（内部含 Liang-Barsky 裁剪）
            PlotStraightLine_2D<T>(graph, self);

            // 恢复原始的 circle_2d 标记，保证节点数据一致性
            self.data.circle_2d = backup_circle;

            return; // 提前返回，跳过圆绘制
        }
        // 2. 视口与物理分辨率获取
        const auto& ws = graph.world_space_2d;
        const T dx_px = 0.5 * (ws.x_max - ws.x_min) / graph.resolution_2d.x;
        const T dy_px = 0.5 * (ws.y_max - ws.y_min) / graph.resolution_2d.y;

        // 设置四叉树终止阈值：10 个物理像素宽度
        const T threshold_x = dx_px * static_cast<T>(10.0);
        const T threshold_y = dy_px * static_cast<T>(10.0);

        // 3. 采样点缓存逻辑（用于精密插值阶段）
        // 建议：由于 evaluate_circle_implicit 非常精确，四叉树深度会更合理，Map 的压力会减小
        std::map<std::pair<T, T>, T> cache;
        auto eval_f = [&](T x, T y)
        {
            auto it = cache.find({x, y});
            if (it != cache.end()) return it->second;
            // 标量隐函数：f = (x-cx)^2 + (y-cy)^2 - r^2
            T val = (x - cx) * (x - cx) + (y - cy) * (y - cy) - r_sq;
            return cache[{x, y}] = val;
        };

        // 4. 四叉树递归逻辑
        auto quadtree_prune = [&](auto& self_ref, T x0, T y0, T x1, T y1) -> void
        {
            // --- 核心改动：调用原子级区间评估函数 ---
            // 该函数在 IX 包含 cx 时能准确识别出极小值为 0，不会产生 IA 膨胀
            auto res_iv = StuCanvas::utils::evaluate_circle_implicit(
                StuCanvas::Interval<T>(x0, x1),
                StuCanvas::Interval<T>(y0, y1),
                cx, cy, r_sq
            );

            // 区间算术剪枝：如果不跨越 0，则该区域绝对不包含圆周
            if (res_iv.lower > 0 || res_iv.upper < 0) return;

            T w = x1 - x0;
            T h = y1 - y0;

            // 递归分发
            if (w > threshold_x || h > threshold_y)
            {
                T mx = x0 + w / 2;
                T my = y0 + h / 2;
                self_ref(self_ref, x0, y0, mx, my);
                self_ref(self_ref, mx, y0, x1, my);
                self_ref(self_ref, x0, my, mx, y1);
                self_ref(self_ref, mx, my, x1, y1);
                return;
            }

            // 5. 像素级采样阶段：此时当前区域约为 10x10 像素大小
            T ax0 = std::floor((x0 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay0 = std::floor((y0 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T ax1 = std::ceil((x1 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay1 = std::ceil((y1 - ws.y_min) / dy_px) * dy_px + ws.y_min;

            for (T px = ax0; px < ax1; px += dx_px)
            {
                for (T py = ay0; py < ay1; py += dy_px)
                {
                    // 采样像素四个角
                    T v_bl = eval_f(px, py);
                    T v_br = eval_f(px + dx_px, py);
                    T v_tl = eval_f(px, py + dy_px);
                    T v_tr = eval_f(px + dx_px, py + dy_px);

                    T min_v = std::min({v_bl, v_br, v_tl, v_tr});
                    T max_v = std::max({v_bl, v_br, v_tl, v_tr});

                    // 介值定理：判定像素是否跨越零点
                    if (min_v <= 0 && max_v >= 0)
                    {
                        // 像素内 2x2 子采样以获得平滑结果
                        T hx = px + dx_px * 0.5;
                        T hy = py + dy_px * 0.5;

                        T v_bm = eval_f(hx, py);
                        T v_tm = eval_f(hx, py + dy_px);
                        T v_lm = eval_f(px, hy);
                        T v_rm = eval_f(px + dx_px, hy);
                        T v_mm = eval_f(hx, hy);

                        // 12 条子格边插值点存入 result_points_2d
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px, py, hx, py, v_bl, v_bm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, hx, py, hx, hy, v_bm, v_mm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px, hy, hx, hy, v_lm, v_mm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px, py, px, hy, v_bl, v_lm);

                        StuCanvas::utils::lerp_and_push(self.result_points_2d, hx, py, px + dx_px, py, v_bm, v_br);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px + dx_px, py, px + dx_px, hy, v_br,
                                                        v_rm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, hx, hy, px + dx_px, hy, v_mm, v_rm);

                        StuCanvas::utils::lerp_and_push(self.result_points_2d, hx, hy, hx, py + dy_px, v_mm, v_tm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px, py + dy_px, hx, py + dy_px, v_tl,
                                                        v_tm);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px, hy, px, py + dy_px, v_lm, v_tl);

                        StuCanvas::utils::lerp_and_push(self.result_points_2d, px + dx_px, hy, px + dx_px, py + dy_px,
                                                        v_rm, v_tr);
                        StuCanvas::utils::lerp_and_push(self.result_points_2d, hx, py + dy_px, px + dx_px, py + dy_px,
                                                        v_tm, v_tr);
                    }
                }
            }
        };

        // 6. 执行全场四叉树剪枝
        quadtree_prune(quadtree_prune, ws.x_min, ws.y_min, ws.x_max, ws.y_max);
    }


    namespace utils
    {
        /**
         * @brief 内部通用平面/三角形打点引擎
         */
        template <typename T>
        void InternalPlotPlane_3D(Graph<T>& graph, Node<T>& self, bool is_triangle)
        {
            // 1. 初始化与数据提取
            const auto& d = self.data.plane_3d;
            const auto& ws = graph.world_space_3d;
            auto res = graph.resolution_3d;
            res.x *= 3;
            res.y *= 3;
            res.z *= 3;

            T s_start = 1.0, s_end = 0.0;
            T t_start = 1.0, t_end = 0.0;
            bool intersect = false;

            // 2. 参数范围更新逻辑
            auto update_bounds = [&](T s, T t)
            {
                if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
                {
                    s_start = std::min(s_start, s);
                    s_end = std::max(s_end, s);
                    t_start = std::min(t_start, t);
                    t_end = std::max(t_end, t);
                    intersect = true;
                }
            };

            auto is_in = [&](T x, T y, T z)
            {
                return x >= ws.x_min && x <= ws.x_max && y >= ws.y_min && y <= ws.y_max && z >= ws.z_min && z <= ws.
                    z_max;
            };

            // 3. 检查顶点 (三角形只检查3个点，平行四边形检查4个点)
            if (is_in(d.ox, d.oy, d.oz)) update_bounds(0, 0);
            if (is_in(d.ox + d.ux, d.oy + d.uy, d.oz + d.uz)) update_bounds(1, 0);
            if (is_in(d.ox + d.vx, d.oy + d.vy, d.oz + d.vz)) update_bounds(0, 1);
            if (!is_triangle)
            {
                if (is_in(d.ox + d.ux + d.vx, d.oy + d.uy + d.vy, d.oz + d.uz + d.vz)) update_bounds(1, 1);
            }

            // 4. 边界裁剪 (Liang-Barsky)
            auto clip_edge = [&](T ox, T oy, T oz, T dx, T dy, T dz, bool is_u_edge, T fixed_param)
            {
                T lt1 = 0.0, lt2 = 1.0;
                if (LiangBarskyClipTest(-dx, ox - ws.x_min, lt1, lt2) &&
                    LiangBarskyClipTest(dx, ws.x_max - ox, lt1, lt2) &&
                    LiangBarskyClipTest(-dy, oy - ws.y_min, lt1, lt2) &&
                    LiangBarskyClipTest(dy, ws.y_max - oy, lt1, lt2) &&
                    LiangBarskyClipTest(-dz, oz - ws.z_min, lt1, lt2) &&
                    LiangBarskyClipTest(dz, ws.z_max - oz, lt1, lt2))
                {
                    if (is_u_edge)
                    {
                        update_bounds(lt1, fixed_param);
                        update_bounds(lt2, fixed_param);
                    }
                    else
                    {
                        update_bounds(fixed_param, lt1);
                        update_bounds(fixed_param, lt2);
                    }
                }
            };

            // 裁剪四边（如果是三角形，第四条斜边 s+t=1 理论上也要剪，但为了性能和复用，
            // 我们可以利用外接平行四边形的 AABB 结合内层 if 判定）
            clip_edge(d.ox, d.oy, d.oz, d.ux, d.uy, d.uz, true, 0.0);
            clip_edge(d.ox + d.vx, d.oy + d.vy, d.oz + d.vz, d.ux, d.uy, d.uz, true, 1.0);
            clip_edge(d.ox, d.oy, d.oz, d.vx, d.vy, d.vz, false, 0.0);
            clip_edge(d.ox + d.ux, d.oy + d.uy, d.oz + d.uz, d.vx, d.vy, d.vz, false, 1.0);

            if (!intersect)
            {
                // 简单的 AABB 快速排除
                T x_min_p = std::min({d.ox, d.ox + d.ux, d.ox + d.vx});
                T x_max_p = std::max({d.ox, d.ox + d.ux, d.ox + d.vx});
                if (x_max_p < ws.x_min || x_min_p > ws.x_max) return;
                s_start = 0.0;
                s_end = 1.0;
                t_start = 0.0;
                t_end = 1.0;
            }

            // 5. 步长与密度计算
            const T step_x = (ws.x_max - ws.x_min) / res.x;
            const T step_y = (ws.y_max - ws.y_min) / res.y;
            const T step_z = (ws.z_max - ws.z_min) / res.z;

            auto calc_n = [&](T vx, T vy, T vz)
            {
                T n = std::max({std::abs(vx) / step_x, std::abs(vy) / step_y, std::abs(vz) / step_z});
                return (n < 1) ? (size_t)1 : (size_t)std::ceil(n);
            };

            size_t total_n_s = calc_n(d.ux, d.uy, d.uz);
            size_t total_n_t = calc_n(d.vx, d.vy, d.vz);

            // 6. 双重循环采样
            size_t i_begin = static_cast<size_t>(std::floor(s_start * total_n_s));
            size_t i_end = static_cast<size_t>(std::ceil(s_end * total_n_s));
            size_t j_begin = static_cast<size_t>(std::floor(t_start * total_n_t));
            size_t j_end = static_cast<size_t>(std::ceil(t_end * total_n_t));

            for (size_t i = i_begin; i <= i_end && i <= total_n_s; ++i)
            {
                T s = static_cast<T>(i) / static_cast<T>(total_n_s);
                T sx = d.ox + s * d.ux, sy = d.oy + s * d.uy, sz = d.oz + s * d.uz;

                for (size_t j = j_begin; j <= j_end && j <= total_n_t; ++j)
                {
                    T t = static_cast<T>(j) / static_cast<T>(total_n_t);

                    // --- 核心复用区别点 ---
                    if (is_triangle && (s + t > static_cast<T>(1.0) + 1e-9)) continue;
                    // ---------------------

                    T px = sx + t * d.vx, py = sy + t * d.vy, pz = sz + t * d.vz;

                    if (px >= ws.x_min && px <= ws.x_max &&
                        py >= ws.y_min && py <= ws.y_max &&
                        pz >= ws.z_min && pz <= ws.z_max)
                    {
                        self.result_points_3d.emplace_back(Point3D<T>{px, py, pz});
                    }
                }
            }


            // 退化保护：仅当 U 和 V 均为零向量（平面退化为点）且原点在视口内时，输出原点
            if (self.result_points_3d.empty())
            {
                const T zero = static_cast<T>(0);
                if (d.ux == zero && d.uy == zero && d.uz == zero &&
                    d.vx == zero && d.vy == zero && d.vz == zero)
                {
                    if (d.ox >= ws.x_min && d.ox <= ws.x_max &&
                        d.oy >= ws.y_min && d.oy <= ws.y_max &&
                        d.oz >= ws.z_min && d.oz <= ws.z_max)
                    {
                        self.result_points_3d.emplace_back(Point3D<T>{d.ox, d.oy, d.oz});
                    }
                }
            }
        }
    } // namespace detail

    // 2. 外部调用的封装函数

    /**
     * @brief 绘制 3D 平行四边形平面
     */
    template <typename T>
    void PlotPlane_3D(Graph<T>& graph, Node<T>& self)
    {
        utils::InternalPlotPlane_3D(graph, self, false);
    }

    /**
     * @brief 绘制 3D 三角形平面
     */
    template <typename T>
    void PlotTriangle_3D(Graph<T>& graph, Node<T>& self)
    {
        utils::InternalPlotPlane_3D(graph, self, true);
    }


    template <typename T>
    void PlotArc_2D(Graph<T>& graph, Node<T>& self)
    {

        constexpr T MARKER_LD = -0x1.BAADC0DEp+300L;
        const T INVALID_VAL = static_cast<T>(MARKER_LD);
        if (self.data.arc_2d.r == INVALID_VAL)
        {
            return;
        }
        PlotCircle_2D(graph, self);

        if (self.result_points_2d.empty()) return;




        const T cx = self.data.arc_2d.cx;
        const T cy = self.data.arc_2d.cy;
        const T s_ang = self.data.arc_2d.start_angle;
        const T sweep = self.data.arc_2d.end_angle;

        const T PI = static_cast<T>(3.14159265358979323846);
        const T TWO_PI = static_cast<T>(2.0) * PI;


        auto is_point_outside_arc = [&](const Point2D<T>& pt)
        {
            T angle = std::atan2(pt.y - cy, pt.x - cx);


            T rel_angle = angle - s_ang;
            while (rel_angle < 0) rel_angle += TWO_PI;
            while (rel_angle >= TWO_PI) rel_angle -= TWO_PI;

            if (sweep >= 0)
            {
                return rel_angle > (sweep + static_cast<T>(1e-9));
            }
            else
            {
                return rel_angle < (TWO_PI + sweep - static_cast<T>(1e-9));
            }
        };


        self.result_points_2d.erase(
            std::remove_if(self.result_points_2d.begin(),
                           self.result_points_2d.end(),
                           is_point_outside_arc),
            self.result_points_2d.end()
        );
    }


    template <typename T>
    void PlotPoint_2D(Graph<T>& graph, Node<T>& self)
    {
        const T px = self.data.point_2d.x;
        const T py = self.data.point_2d.y;


        const auto& ws = graph.world_space_2d;
        if (px >= ws.x_min && px <= ws.x_max &&
            py >= ws.y_min && py <= ws.y_max)
        {
            self.result_points_2d.emplace_back(Point2D<T>{px, py});
        }
    }


    template <typename T>
    void PlotPoint_3D(Graph<T>& graph, Node<T>& self)
    {
        const T px = self.data.point_3d.x;
        const T py = self.data.point_3d.y;
        const T pz = self.data.point_3d.z;


        const auto& ws = graph.world_space_3d;
        if (px >= ws.x_min && px <= ws.x_max &&
            py >= ws.y_min && py <= ws.y_max &&
            pz >= ws.z_min && pz <= ws.z_max)
        {
            self.result_points_3d.emplace_back(Point3D<T>{px, py, pz});
        }
    }

    template <typename T>
    void PlotPolyGen_2D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 获取必要的生成参数并拷贝到局部变量（栈上）
        // 这是防止 Union 内存自杀式覆盖的关键！
        const uint32_t n_sides = self.data.poly_gen_2d.n;
        const T side_len = self.data.poly_gen_2d.side_len;
        const T cx = self.data.poly_gen_2d.cx;
        const T cy = self.data.poly_gen_2d.cy;

        // 基础校验
        if (n_sides < 3) return;

        // 2. 顶级清空结果集 (根据 ExecuteNodeUpdate 的约定，此处通常已经清空)
        // 为了保险，如果单独调用此函数，可保留 self.result_points_2d.clear();

        // 3. 数学计算：根据边长计算外接圆半径 R
        // 公式：side_len = 2 * R * sin(PI / n)
        const T PI = static_cast<T>(3.14159265358979323846);
        T circumradius = side_len / (static_cast<T>(2.0) * std::sin(PI / static_cast<T>(n_sides)));

        // 4. 计算所有顶点的坐标并暂存
        // 必须暂存，因为后续修改 union 会导致 cx, cy 丢失
        std::vector<std::pair<T, T>> vertices;
        vertices.reserve(n_sides);
        for (uint32_t i = 0; i < n_sides; ++i)
        {
            T theta = (static_cast<T>(2.0) * PI * static_cast<T>(i)) / static_cast<T>(n_sides);
            vertices.push_back({
                cx + circumradius * std::cos(theta),
                cy + circumradius * std::sin(theta)
            });
        }

        // 5. 备份原始参数块
        // 虽然我们有了局部变量，但还原数据是为了让后续的其他计算（或UI）能读到正确的 n 和 side_len
        auto backup_poly_data = self.data.poly_gen_2d;

        // 6. 组合式打点：遍历每一条边
        for (uint32_t i = 0; i < n_sides; ++i)
        {
            const auto& p1 = vertices[i];
            const auto& p2 = vertices[(i + 1) % n_sides]; // 自动闭合

            // 临时修改 Union 内存，将其伪装成一个线段节点
            // 注意：设置 x1, y1 的过程会修改内存中同一位置的 n_sides，
            // 但因为我们循环条件使用的是栈上的 n_sides，所以这里非常安全。
            self.data.line_2d.x0 = p1.first;
            self.data.line_2d.y0 = p1.second;
            self.data.line_2d.x1 = p2.first;
            self.data.line_2d.y1 = p2.second;

            // 调用现有的线段打点器
            // PlotSegment_2D 内部会读取 data.line_2d 并执行 Liang-Barsky 裁剪
            // 它会将生成的点集直接追加（emplace_back）到 result_points_2d 中
            PlotSegment_2D<T>(graph, self);
        }

        // 7. 还原原始多边形数据，保证 Node 状态的一致性
        self.data.poly_gen_2d = backup_poly_data;
    }


    template <typename T>
    void PlotPlatonicSolid_3D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 局部备份参数，防止 Union 内存覆盖
        const T cx = self.data.platonic_solid_3d.cx;
        const T cy = self.data.platonic_solid_3d.cy;
        const T cz = self.data.platonic_solid_3d.cz;
        const T radius = self.data.platonic_solid_3d.radius;
        const int type = self.data.platonic_solid_3d.type;
        auto backup_params = self.data.platonic_solid_3d;

        // 2. 获取静态拓扑数据
        auto raw = StaticData::PlatonicLibrary<T>::Get(type);
        if (raw.vertices.empty()) return;

        // 3. 预计算所有顶点坐标 (归一化并移动到中心)
        std::vector<Point3D<T>> world_verts;
        world_verts.reserve(raw.vertices.size());
        const T eps = std::numeric_limits<T>::epsilon();

        for (const auto& v : raw.vertices)
        {
            T mag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            T scale = (mag > eps) ? (radius / mag) : radius;
            world_verts.push_back({cx + v[0] * scale, cy + v[1] * scale, cz + v[2] * scale});
        }

        // 4. 组合渲染：绘制所有棱边 (Edges)
        // 注意：此处不需要去重，因为 Plotter 内部追加速度很快，重复打点不影响离线结果
        for (const auto& face : raw.faces)
        {
            for (size_t i = 0; i < face.size(); ++i)
            {
                auto p1 = world_verts[face[i]];
                auto p2 = world_verts[face[(i + 1) % face.size()]];

                self.data.line_3d = {p1.x, p1.y, p1.z, p2.x, p2.y, p2.z};
                PlotSegment_3D(graph, self);
            }
        }

        // 5. 组合渲染：绘制所有面 (Faces)
        for (const auto& face : raw.faces)
        {
            // 多边形面三角化处理 (Triangle Fan)
            for (size_t k = 1; k < face.size() - 1; ++k)
            {
                auto p0 = world_verts[face[0]];
                auto p1 = world_verts[face[k]];
                auto p2 = world_verts[face[k + 1]];

                // 伪装成 Plane3D 结构
                self.data.plane_3d.ox = p0.x;
                self.data.plane_3d.oy = p0.y;
                self.data.plane_3d.oz = p0.z;
                self.data.plane_3d.ux = p1.x - p0.x;
                self.data.plane_3d.uy = p1.y - p0.y;
                self.data.plane_3d.uz = p1.z - p0.z;
                self.data.plane_3d.vx = p2.x - p0.x;
                self.data.plane_3d.vy = p2.y - p0.y;
                self.data.plane_3d.vz = p2.z - p0.z;

                PlotTriangle_3D(graph, self);
            }
        }

        // 6. 还原参数
        self.data.platonic_solid_3d = backup_params;
    }


    namespace utils
    {
        /**
         * @brief 3D 线性插值辅助函数
         */
        template <typename T>
        void lerp_3d_push_ref(std::vector<Point3D<T>>& res, T x1, T y1, T z1, T x2, T y2, T z2, T v1, T v2)
        {
            // 介值定理：如果符号不同，则中间必有零点
            if ((v1 > 0 && v2 <= 0) || (v1 < 0 && v2 >= 0))
            {
                T t = -v1 / (v2 - v1);
                res.emplace_back(Point3D<T>{
                    x1 + t * (x2 - x1),
                    y1 + t * (y2 - y1),
                    z1 + t * (z2 - z1)
                });
            }
        }
    }

    template <typename T>
    void PlotSphere_3D(Graph<T>& graph, Node<T>& self)
    {


        // ---------- 退化检查：球体因四点共面被标记为无效 ----------
        constexpr long double MARKER_LD = -0x1.BAADC0DEp+300L;
        const T INVALID_VAL = static_cast<T>(MARKER_LD);
        if (self.data.sphere_3d.r == INVALID_VAL)
            return; // 无效球体，跳过绘制
        // 1. 获取球体基础几何参数
        const T cx = self.data.sphere_3d.cx;
        const T cy = self.data.sphere_3d.cy;
        const T cz = self.data.sphere_3d.cz;
        const T r_sq = self.data.sphere_3d.r * self.data.sphere_3d.r;

        // 2. 获取视口与物理分辨率
        const auto& ws = graph.world_space_3d;
        const T dx_px = (ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T dy_px = (ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T dz_px = (ws.z_max - ws.z_min) / graph.resolution_3d.z;

        // 设置细分终止阈值：10个物理像素单位
        const T thres_x = dx_px * static_cast<T>(10.0);
        const T thres_y = dy_px * static_cast<T>(10.0);
        const T thres_z = dz_px * static_cast<T>(10.0);

        // 标量隐函数：用于叶子节点精密采样
        auto eval_f = [&](T x, T y, T z)
        {
            return (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz) - r_sq;
        };

        // 3. 八叉树递归逻辑
        auto octree_prune = [&](auto& self_ref, T x0, T y0, T z0, T x1, T y1, T z1) -> void
        {
            // --- 核心优化：使用特化原子函数进行区间评估 ---
            // 该函数能完美处理 (X-cx)^2，当 cx 处于 [x0, x1] 中间时，能准确识别下界为 0
            auto res_iv = StuCanvas::utils::evaluate_sphere_implicit(
                StuCanvas::Interval<T>(x0, x1),
                StuCanvas::Interval<T>(y0, y1),
                StuCanvas::Interval<T>(z0, z1),
                cx, cy, cz, r_sq
            );

            // 区间算术剪枝：如果区间不跨越 0，物理上绝对没有球面经过
            if (res_iv.lower > 0 || res_iv.upper < 0) return;

            T w = x1 - x0, h = y1 - y0, d = z1 - z0;

            // 如果当前方格大于 10 像素，继续进行八叉树细分
            if (w > thres_x || h > thres_y || d > thres_z)
            {
                T mx = x0 + w / 2;
                T my = y0 + h / 2;
                T mz = z0 + d / 2;
                self_ref(self_ref, x0, y0, z0, mx, my, mz);
                self_ref(self_ref, mx, y0, z0, x1, my, mz);
                self_ref(self_ref, x0, my, z0, mx, y1, mz);
                self_ref(self_ref, mx, my, z0, x1, y1, mz);
                self_ref(self_ref, x0, y0, mz, mx, my, z1);
                self_ref(self_ref, mx, y0, mz, x1, my, z1);
                self_ref(self_ref, x0, my, mz, mx, y1, z1);
                self_ref(self_ref, mx, my, mz, x1, y1, z1);
                return;
            }

            // 4. 像素级精密采样 (处理 10x10x10 像素范围)
            T ax0 = std::floor((x0 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay0 = std::floor((y0 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T az0 = std::floor((z0 - ws.z_min) / dz_px) * dz_px + ws.z_min;
            T ax1 = std::ceil((x1 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay1 = std::ceil((y1 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T az1 = std::ceil((z1 - ws.z_min) / dz_px) * dz_px + ws.z_min;

            for (T px = ax0; px < ax1; px += dx_px)
            {
                for (T py = ay0; py < ay1; py += dy_px)
                {
                    for (T pz = az0; pz < az1; pz += dz_px)
                    {
                        // 采样体素 8 个角
                        T v[8];
                        v[0] = eval_f(px, py, pz);
                        v[1] = eval_f(px + dx_px, py, pz);
                        v[2] = eval_f(px, py + dy_px, pz);
                        v[3] = eval_f(px + dx_px, py + dy_px, pz);
                        v[4] = eval_f(px, py, pz + dz_px);
                        v[5] = eval_f(px + dx_px, py, pz + dz_px);
                        v[6] = eval_f(px, py + dy_px, pz + dz_px);
                        v[7] = eval_f(px + dx_px, py + dy_px, pz + dz_px);

                        T v_min = std::min({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]});
                        T v_max = std::max({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]});

                        // 判定体素是否穿过球面
                        if (v_min <= 0 && v_max >= 0)
                        {
                            // 5. 子体素 2x2x2 细分与 12 棱边线性插值
                            T hx = px + dx_px * 0.5;
                            T hy = py + dy_px * 0.5;
                            T hz = pz + dz_px * 0.5;

                            auto process_box = [&](T x_s, T y_s, T z_s, T x_e, T y_e, T z_e)
                            {
                                T sv[8];
                                sv[0] = eval_f(x_s, y_s, z_s);
                                sv[1] = eval_f(x_e, y_s, z_s);
                                sv[2] = eval_f(x_s, y_e, z_s);
                                sv[3] = eval_f(x_e, y_e, z_s);
                                sv[4] = eval_f(x_s, y_s, z_e);
                                sv[5] = eval_f(x_e, y_s, z_e);
                                sv[6] = eval_f(x_s, y_e, z_e);
                                sv[7] = eval_f(x_e, y_e, z_e);

                                // 在体素棱边上进行零点插值
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_e, y_s, z_s,
                                                                   sv[0], sv[1]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_s, x_e, y_e, z_s,
                                                                   sv[2], sv[3]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_e, x_e, y_s, z_e,
                                                                   sv[4], sv[5]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_e, x_e, y_e, z_e,
                                                                   sv[6], sv[7]);

                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_s, y_e, z_s,
                                                                   sv[0], sv[2]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_s, x_e, y_e, z_s,
                                                                   sv[1], sv[3]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_e, x_s, y_e, z_e,
                                                                   sv[4], sv[6]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_e, x_e, y_e, z_e,
                                                                   sv[5], sv[7]);

                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_s, y_s, z_e,
                                                                   sv[0], sv[4]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_s, x_e, y_s, z_e,
                                                                   sv[1], sv[5]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_s, x_s, y_e, z_e,
                                                                   sv[2], sv[6]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, x_e, y_e, z_s, x_e, y_e, z_e,
                                                                   sv[3], sv[7]);
                            };

                            process_box(px, py, pz, hx, hy, hz);
                            process_box(hx, py, pz, px + dx_px, hy, hz);
                            process_box(px, hy, pz, hx, py + dy_px, hz);
                            process_box(hx, hy, pz, px + dx_px, py + dy_px, hz);
                            process_box(px, py, hz, hx, hy, pz + dz_px);
                            process_box(hx, py, hz, px + dx_px, hy, pz + dz_px);
                            process_box(px, hy, hz, hx, py + dy_px, pz + dz_px);
                            process_box(hx, hy, hz, px + dx_px, py + dy_px, pz + dz_px);
                        }
                    }
                }
            }
        };

        // 6. 执行全场八叉树剪枝采样
        octree_prune(octree_prune, ws.x_min, ws.y_min, ws.z_min, ws.x_max, ws.y_max, ws.z_max);
    }


    template <typename T>
    void PlotRectangle_2D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 局部备份参数，防止 Union 内存被 PlotSegment_2D 的 line_2d 覆盖
        const T cx = self.data.rect_2d.cx;
        const T cy = self.data.rect_2d.cy;
        const T w = self.data.rect_2d.width;
        const T h = self.data.rect_2d.height;
        auto backup_data = self.data.rect_2d;

        // 2. 计算四个顶点的坐标
        T x_min = cx - w / static_cast<T>(2.0);
        T x_max = cx + w / static_cast<T>(2.0);
        T y_min = cy - h / static_cast<T>(2.0);
        T y_max = cy + h / static_cast<T>(2.0);

        // 定义四条边（起点 x0,y0 到 终点 x1,y1）
        struct Edge
        {
            T x0, y0, x1, y1;
        };
        Edge edges[4] = {
            {x_min, y_min, x_max, y_min}, // 底边
            {x_max, y_min, x_max, y_max}, // 右边
            {x_max, y_max, x_min, y_max}, // 顶边
            {x_min, y_max, x_min, y_min} // 左边
        };

        // 3. 组合绘制：复用线段打点器
        for (int i = 0; i < 4; ++i)
        {
            // 伪装成线性节点数据
            self.data.line_2d.x0 = edges[i].x0;
            self.data.line_2d.y0 = edges[i].y0;
            self.data.line_2d.x1 = edges[i].x1;
            self.data.line_2d.y1 = edges[i].y1;

            // 调用现有的线段采样函数，它会将点追加到 self.result_points_2d
            PlotSegment_2D<T>(graph, self);
        }

        // 4. 还原原始矩形数据，确保节点状态一致
        self.data.rect_2d = backup_data;
    }


    template <typename T>
    void PlotCuboid_3D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 局部备份参数，防止 Union 内存被子调用覆盖
        const T cx = self.data.cuboid_3d.cx;
        const T cy = self.data.cuboid_3d.cy;
        const T cz = self.data.cuboid_3d.cz;
        const T dx = self.data.cuboid_3d.dx;
        const T dy = self.data.cuboid_3d.dy;
        const T dz = self.data.cuboid_3d.dz;
        auto backup_data = self.data.cuboid_3d;

        // 2. 计算 8 个顶点的坐标
        Point3D<T> v[8] = {
            {cx - dx, cy - dy, cz - dz}, {cx + dx, cy - dy, cz - dz},
            {cx + dx, cy + dy, cz - dz}, {cx - dx, cy + dy, cz - dz},
            {cx - dx, cy - dy, cz + dz}, {cx + dx, cy - dy, cz + dz},
            {cx + dx, cy + dy, cz + dz}, {cx - dx, cy + dy, cz + dz}
        };

        // 3. 组合绘制：棱边 (12 条)
        auto plot_edge = [&](int i, int j)
        {
            self.data.line_3d = {v[i].x, v[i].y, v[i].z, v[j].x, v[j].y, v[j].z};
            PlotSegment_3D(graph, self);
        };
        // 底面 4 条
        plot_edge(0, 1);
        plot_edge(1, 2);
        plot_edge(2, 3);
        plot_edge(3, 0);
        // 顶面 4 条
        plot_edge(4, 5);
        plot_edge(5, 6);
        plot_edge(6, 7);
        plot_edge(7, 4);
        // 垂直 4 条
        plot_edge(0, 4);
        plot_edge(1, 5);
        plot_edge(2, 6);
        plot_edge(3, 7);

        // 4. 组合绘制：表面 (6 个面，每面 2 个三角形)
        auto plot_face_tri = [&](int i, int j, int k)
        {
            // 伪装成 Plane3D (三角形模式)
            self.data.plane_3d.ox = v[i].x;
            self.data.plane_3d.oy = v[i].y;
            self.data.plane_3d.oz = v[i].z;
            self.data.plane_3d.ux = v[j].x - v[i].x;
            self.data.plane_3d.uy = v[j].y - v[i].y;
            self.data.plane_3d.uz = v[j].z - v[i].z;
            self.data.plane_3d.vx = v[k].x - v[i].x;
            self.data.plane_3d.vy = v[k].y - v[i].y;
            self.data.plane_3d.vz = v[k].z - v[i].z;
            PlotTriangle_3D(graph, self);
        };

        // 定义 6 个面 (每面由两个三角形组成)
        int faces[6][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, // 底、顶
            {0, 1, 5, 4}, {1, 2, 6, 5}, // 前、右
            {2, 3, 7, 6}, {3, 0, 4, 7} // 后、左
        };

        for (int i = 0; i < 6; ++i)
        {
            plot_face_tri(faces[i][0], faces[i][1], faces[i][2]);
            plot_face_tri(faces[i][0], faces[i][2], faces[i][3]);
        }

        // 5. 还原原始数据
        self.data.cuboid_3d = backup_data;
    }


    /**
  * @brief 使用原子级特化区间函数绘制 3D 圆锥侧面
  * 采用区间算术剪枝 + 八叉树细分 + 12棱边线性插值寻零
  */
    template <typename T>
    void PlotCone_3D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 基础几何参数提取
        const T ax = self.data.cone_3d.apex_x;
        const T ay = self.data.cone_3d.apex_y;
        const T az = self.data.cone_3d.apex_z;
        const T bx = self.data.cone_3d.base_x;
        const T by = self.data.cone_3d.base_y;
        const T bz = self.data.cone_3d.base_z;
        const T r = self.data.cone_3d.r;
        const T eps = std::numeric_limits<T>::epsilon();
        const T dx = ax - bx, dy = ay - by, dz = az - bz;
        if (dx * dx + dy * dy + dz * dz < eps * static_cast<T>(10))
            return;

        // 2. 预计算圆锥轴向常量
        T vx = bx - ax, vy = by - ay, vz = bz - az;
        T h2 = vx * vx + vy * vy + vz * vz;
        T h = std::sqrt(h2);
        if (h < static_cast<T>(1e-9)) return; // 忽略退化的圆锥

        const T uax = vx / h, uay = vy / h, uaz = vz / h; // 单位轴向量 d
        const T k_factor = static_cast<T>(1.0) + (r * r) / h2; // factor = 1 + (R/H)^2

        // 3. 视口与物理分辨率获取
        const auto& ws = graph.world_space_3d;
        const T dx_px = (ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T dy_px = (ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T dz_px = (ws.z_max - ws.z_min) / graph.resolution_3d.z;

        // 设置八叉树细分终止阈值：10 个物理像素单位
        const T thres_x = dx_px * static_cast<T>(10.0);
        const T thres_y = dy_px * static_cast<T>(10.0);
        const T thres_z = dz_px * static_cast<T>(10.0);

        // 4. 标量隐函数：用于叶子节点的精密采样
        auto eval_f = [&](T x, T y, T z)
        {
            T wx = x - ax, wy = y - ay, wz = z - az;
            T proj = wx * uax + wy * uay + wz * uaz; // 点到轴线的投影长度 (w · d)

            // 侧面方程：f = ||w||^2 - (1 + k^2) * (w · d)^2
            T f_side = (wx * wx + wy * wy + wz * wz) - k_factor * (proj * proj);

            // 高度约束剪切：仅保留 [0, H] 之间的部分。
            // 如果在顶点 A 后方或底面 B 之外，强制返回一个非零值，避免产生插值点。
            if (proj < 0) return std::abs(f_side) + (0 - proj);
            if (proj > h) return std::abs(f_side) + (proj - h);

            return f_side;
        };

        // 5. 八叉树递归逻辑
        auto octree_prune = [&](auto& self_ref, T x0, T y0, T z0, T x1, T y1, T z1) -> void
        {
            // --- 核心优化：调用特化区间函数进行隐函数评估 ---
            // 该函数内部包含了高度范围检测，如果投影完全越界会返回 poisoned
            auto res_iv = StuCanvas::utils::evaluate_cone_implicit(
                StuCanvas::Interval<T>(x0, x1),
                StuCanvas::Interval<T>(y0, y1),
                StuCanvas::Interval<T>(z0, z1),
                ax, ay, az, uax, uay, uaz, k_factor, h
            );

            // 区间算术剪枝：如果区间不包含 0 或已中毒，则直接跳过该区域
            if (res_iv.is_poisoned() || !StuCanvas::utils::possible_root<T>(res_iv)) return;

            T w = x1 - x0, hb = y1 - y0, db = z1 - z0;

            // 分发递归：如果当前块仍大于阈值
            if (w > thres_x || hb > thres_y || db > thres_z)
            {
                T mx = x0 + w / 2, my = y0 + hb / 2, mz = z0 + db / 2;
                self_ref(self_ref, x0, y0, z0, mx, my, mz);
                self_ref(self_ref, mx, y0, z0, x1, my, mz);
                self_ref(self_ref, x0, my, z0, mx, y1, mz);
                self_ref(self_ref, mx, my, z0, x1, y1, mz);
                self_ref(self_ref, x0, y0, mz, mx, my, z1);
                self_ref(self_ref, mx, y0, mz, x1, my, z1);
                self_ref(self_ref, x0, my, mz, mx, y1, z1);
                self_ref(self_ref, mx, my, mz, x1, y1, z1);
                return;
            }

            // 6. 像素级精密采样 (处理约 10x10x10 体素范围)
            T ax0 = std::floor((x0 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay0 = std::floor((y0 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T az0 = std::floor((z0 - ws.z_min) / dz_px) * dz_px + ws.z_min;
            T ax1 = std::ceil((x1 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay1 = std::ceil((y1 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T az1 = std::ceil((z1 - ws.z_min) / dz_px) * dz_px + ws.z_min;

            for (T px = ax0; px < ax1; px += dx_px)
            {
                for (T py = ay0; py < ay1; py += dy_px)
                {
                    for (T pz = az0; pz < az1; pz += dz_px)
                    {
                        // 采样体素 8 个角的值
                        T v[8];
                        v[0] = eval_f(px, py, pz);
                        v[1] = eval_f(px + dx_px, py, pz);
                        v[2] = eval_f(px, py + dy_px, pz);
                        v[3] = eval_f(px + dx_px, py + dy_px, pz);
                        v[4] = eval_f(px, py, pz + dz_px);
                        v[5] = eval_f(px + dx_px, py, pz + dz_px);
                        v[6] = eval_f(px, py + dy_px, pz + dz_px);
                        v[7] = eval_f(px + dx_px, py + dy_px, pz + dz_px);

                        T v_min = v[0], v_max = v[0];
                        for (int k = 1; k < 8; ++k)
                        {
                            v_min = std::min(v_min, v[k]);
                            v_max = std::max(v_max, v[k]);
                        }

                        // 如果体素穿过表面 (跨越零点)
                        if (v_min <= 0 && v_max >= 0)
                        {
                            // 执行 2x2x2 子块细分并进行 12 棱边插值寻零
                            auto process_box = [&](T xs, T ys, T zs, T xe, T ye, T ze)
                            {
                                T sv[8];
                                sv[0] = eval_f(xs, ys, zs);
                                sv[1] = eval_f(xe, ys, zs);
                                sv[2] = eval_f(xs, ye, zs);
                                sv[3] = eval_f(xe, ye, zs);
                                sv[4] = eval_f(xs, ys, ze);
                                sv[5] = eval_f(xe, ys, ze);
                                sv[6] = eval_f(xs, ye, ze);
                                sv[7] = eval_f(xe, ye, ze);

                                // 调用 utils 中的 3D 线性插值器
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ys, zs, xe, ys, zs, sv[0],
                                                                   sv[1]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ye, zs, xe, ye, zs, sv[2],
                                                                   sv[3]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ys, ze, xe, ys, ze, sv[4],
                                                                   sv[5]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ye, ze, xe, ye, ze, sv[6],
                                                                   sv[7]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ys, zs, xs, ye, zs, sv[0],
                                                                   sv[2]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xe, ys, zs, xe, ye, zs, sv[1],
                                                                   sv[3]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ys, ze, xs, ye, ze, sv[4],
                                                                   sv[6]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xe, ys, ze, xe, ye, ze, sv[5],
                                                                   sv[7]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ys, zs, xs, ys, ze, sv[0],
                                                                   sv[4]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xe, ys, zs, xe, ys, ze, sv[1],
                                                                   sv[5]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xs, ye, zs, xs, ye, ze, sv[2],
                                                                   sv[6]);
                                StuCanvas::utils::lerp_3d_push_ref(self.result_points_3d, xe, ye, zs, xe, ye, ze, sv[3],
                                                                   sv[7]);
                            };

                            T hx = px + dx_px * 0.5, hy = py + dy_px * 0.5, hz = pz + dz_px * 0.5;
                            process_box(px, py, pz, hx, hy, hz);
                            process_box(hx, py, pz, px + dx_px, hy, hz);
                            process_box(px, hy, pz, hx, py + dy_px, hz);
                            process_box(hx, hy, pz, px + dx_px, py + dy_px, hz);
                            process_box(px, py, hz, hx, hy, pz + dz_px);
                            process_box(hx, py, hz, px + dx_px, hy, pz + dz_px);
                            process_box(px, hy, hz, hx, py + dy_px, pz + dz_px);
                            process_box(hx, hy, hz, px + dx_px, py + dy_px, pz + dz_px);
                        }
                    }
                }
            }
        };

        // 7. 启动全场八叉树采样
        octree_prune(octree_prune, ws.x_min, ws.y_min, ws.z_min, ws.x_max, ws.y_max, ws.z_max);
    }


/**
 * @brief 3D 圆柱面严密打点器 (无底面封口版)
 * 逻辑：无限隐函数评估 + 12棱边插值 + 严格高度区间裁剪
 */
template <typename T>
void PlotCylinder_3D(Graph<T>& graph, Node<T>& self)
{
    // 1. 提取基础几何参数
    const T p1x = self.data.cylinder_3d.p1x, p1y = self.data.cylinder_3d.p1y, p1z = self.data.cylinder_3d.p1z;
    const T p2x = self.data.cylinder_3d.p2x, p2y = self.data.cylinder_3d.p2y, p2z = self.data.cylinder_3d.p2z;
    const T r = self.data.cylinder_3d.r;
    const T r_sq = r * r;

    const T eps = std::numeric_limits<T>::epsilon();
    if ( (p2x - p1x)*(p2x - p1x) + (p2y - p1y)*(p2y - p1y) + (p2z - p1z)*(p2z - p1z) < eps * static_cast<T>(10) )
        return;

    // 2. 预计算轴向向量与长度
    T vx = p2x - p1x, vy = p2y - p1y, vz = p2z - p1z;
    T h = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (h < static_cast<T>(1e-12)) return; // 忽略退化圆柱

    const T ux = vx / h, uy = vy / h, uz = vz / h; // 单位轴向量

    // 3. 视口与分辨率参数
    const auto& ws = graph.world_space_3d;
    const T dx_px = (ws.x_max - ws.x_min) / graph.resolution_3d.x;
    const T dy_px = (ws.y_max - ws.y_min) / graph.resolution_3d.y;
    const T dz_px = (ws.z_max - ws.z_min) / graph.resolution_3d.z;

    const T thres_x = dx_px * static_cast<T>(10.0);
    const T thres_y = dy_px * static_cast<T>(10.0);
    const T thres_z = dz_px * static_cast<T>(10.0);

    // 4. 定义无限长圆柱隐函数 (不带边界惩罚，确保连续性)
    auto eval_f_inf = [&](T x, T y, T z) {
        T wx = x - p1x, wy = y - p1y, wz = z - p1z;
        T proj = wx * ux + wy * uy + wz * uz;
        // 点到轴线距离的平方 - r^2
        return (wx * wx + wy * wy + wz * wz) - (proj * proj) - r_sq;
    };

    // 5. 严格边界插值器：只有当零点落在 [0, h] 范围内才产生点
    auto lerp_strict = [&](T x1, T y1, T z1, T x2, T y2, T z2, T v1, T v2) {
        if ((v1 > 0 && v2 <= 0) || (v1 < 0 && v2 >= 0)) {
            T t = -v1 / (v2 - v1);
            T px = x1 + t * (x2 - x1);
            T py = y1 + t * (y2 - y1);
            T pz = z1 + t * (z2 - z1);

            // 严格高度约束：通过点积计算投影高度并校验
            T dot_h = (px - p1x) * ux + (py - p1y) * uy + (pz - p1z) * uz;
            if (dot_h >= static_cast<T>(0) && dot_h <= h) {
                self.result_points_3d.emplace_back(Point3D<T>{px, py, pz});
            }
        }
    };

    // 6. 八叉树递归逻辑
    auto octree_prune = [&](auto& self_ref, T x0, T y0, T z0, T x1, T y1, T z1) -> void {
        // 利用 interval.hpp 中带高度剪枝的评估函数进行快速剔除
        auto res_iv = StuCanvas::utils::evaluate_cylinder_implicit(
            StuCanvas::Interval<T>(x0, x1), StuCanvas::Interval<T>(y0, y1), StuCanvas::Interval<T>(z0, z1),
            p1x, p1y, p1z, ux, uy, uz, r_sq, h
        );

        if (res_iv.is_poisoned() || !utils::possible_root<T>(res_iv)) return;

        T w = x1 - x0, hb = y1 - y0, db = z1 - z0;
        if (w > thres_x || hb > thres_y || db > thres_z) {
            T mx = x0 + w / 2, my = y0 + hb / 2, mz = z0 + db / 2;
            self_ref(self_ref, x0, y0, z0, mx, my, mz); self_ref(self_ref, mx, y0, z0, x1, my, mz);
            self_ref(self_ref, x0, my, z0, mx, y1, mz); self_ref(self_ref, mx, my, z0, x1, y1, mz);
            self_ref(self_ref, x0, y0, mz, mx, my, z1); self_ref(self_ref, mx, y0, mz, x1, my, z1);
            self_ref(self_ref, x0, my, mz, mx, y1, z1); self_ref(self_ref, mx, my, mz, x1, y1, z1);
            return;
        }

        // 7. 像素级采样与 12 棱边插值 (处理 10x10x10 像素块)
        T ax0 = std::floor((x0 - ws.x_min) / dx_px) * dx_px + ws.x_min;
        T ay0 = std::floor((y0 - ws.y_min) / dy_px) * dy_px + ws.y_min;
        T az0 = std::floor((z0 - ws.z_min) / dz_px) * dz_px + ws.z_min;
        T ax1 = std::ceil((x1 - ws.x_min) / dx_px) * dx_px + ws.x_min;
        T ay1 = std::ceil((y1 - ws.y_min) / dy_px) * dy_px + ws.y_min;
        T az1 = std::ceil((z1 - ws.z_min) / dz_px) * dz_px + ws.z_min;

        for (T px = ax0; px < ax1; px += dx_px) {
            for (T py = ay0; py < ay1; py += dy_px) {
                for (T pz = az0; pz < az1; pz += dz_px) {
                    T v[8];
                    v[0] = eval_f_inf(px, py, pz);
                    v[1] = eval_f_inf(px + dx_px, py, pz);
                    v[2] = eval_f_inf(px, py + dy_px, pz);
                    v[3] = eval_f_inf(px + dx_px, py + dy_px, pz);
                    v[4] = eval_f_inf(px, py, pz + dz_px);
                    v[5] = eval_f_inf(px + dx_px, py, pz + dz_px);
                    v[6] = eval_f_inf(px, py + dy_px, pz + dz_px);
                    v[7] = eval_f_inf(px + dx_px, py + dy_px, pz + dz_px);

                    T v_min = v[0], v_max = v[0];
                    for(int k=1; k<8; ++k) {
                        v_min = std::min(v_min, v[k]); v_max = std::max(v_max, v[k]);
                    }

                    if (v_min <= 0 && v_max >= 0) {
                        auto process_subbox = [&](T xs, T ys, T zs, T xe, T ye, T ze) {
                            T sv[8];
                            sv[0] = eval_f_inf(xs, ys, zs); sv[1] = eval_f_inf(xe, ys, zs);
                            sv[2] = eval_f_inf(xs, ye, zs); sv[3] = eval_f_inf(xe, ye, zs);
                            sv[4] = eval_f_inf(xs, ys, ze); sv[5] = eval_f_inf(xe, ys, ze);
                            sv[6] = eval_f_inf(xs, ye, ze); sv[7] = eval_f_inf(xe, ye, ze);

                            // 对体素的 12 条棱边执行带高度校验的插值
                            lerp_strict(xs, ys, zs, xe, ys, zs, sv[0], sv[1]);
                            lerp_strict(xs, ye, zs, xe, ye, zs, sv[2], sv[3]);
                            lerp_strict(xs, ys, ze, xe, ys, ze, sv[4], sv[5]);
                            lerp_strict(xs, ye, ze, xe, ye, ze, sv[6], sv[7]);
                            lerp_strict(xs, ys, zs, xs, ye, zs, sv[0], sv[2]);
                            lerp_strict(xe, ys, zs, xe, ye, zs, sv[1], sv[3]);
                            lerp_strict(xs, ys, ze, xs, ye, ze, sv[4], sv[6]);
                            lerp_strict(xe, ys, ze, xe, ye, ze, sv[5], sv[7]);
                            lerp_strict(xs, ys, zs, xs, ys, ze, sv[0], sv[4]);
                            lerp_strict(xe, ys, zs, xe, ys, ze, sv[1], sv[5]);
                            lerp_strict(xs, ye, zs, xs, ye, ze, sv[2], sv[6]);
                            lerp_strict(xe, ye, zs, xe, ye, ze, sv[3], sv[7]);
                        };

                        T hx = px + dx_px * 0.5, hy = py + dy_px * 0.5, hz = pz + dz_px * 0.5;
                        process_subbox(px, py, pz, hx, hy, hz);
                        process_subbox(hx, py, pz, px + dx_px, hy, hz);
                        process_subbox(px, hy, pz, hx, py + dy_px, hz);
                        process_subbox(hx, hy, pz, px + dx_px, py + dy_px, hz);
                        process_subbox(px, py, hz, hx, hy, pz + dz_px);
                        process_subbox(hx, py, hz, px + dx_px, hy, pz + dz_px);
                        process_subbox(px, hy, hz, hx, py + dy_px, pz + dz_px);
                        process_subbox(hx, hy, hz, px + dx_px, py + dy_px, pz + dz_px);
                    }
                }
            }
        }
    };

    // 8. 启动
    octree_prune(octree_prune, ws.x_min, ws.y_min, ws.z_min, ws.x_max, ws.y_max, ws.z_max);
}

    template <typename T>
    void PlotCircle_3D(Graph<T>& graph, Node<T>& self)
    {
        // 1. 基础参数准备
        const auto& d = self.data.circle_3d;
        const T r_sq = d.r * d.r;

        // 计算法向量并归一化 (向量为：法向点 N - 中心点 C)
        T vx = d.nx - d.cx;
        T vy = d.ny - d.cy;
        T vz = d.nz - d.cz;
        T mag = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (mag < static_cast<T>(1e-10)) return; // 无效的法方向

        const T nx = vx / mag;
        const T ny = vy / mag;
        const T nz = vz / mag;

        // 2. 计算 0.5 像素精度阈值
        const auto& ws = graph.world_space_3d;
        const T dx_px = (ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T dy_px = (ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T dz_px = (ws.z_max - ws.z_min) / graph.resolution_3d.z;

        // 细分停止条件：体素跨度小于 0.5 个像素大小
        const T stop_thres_x = dx_px * static_cast<T>(0.5);
        const T stop_thres_y = dy_px * static_cast<T>(0.5);
        const T stop_thres_z = dz_px * static_cast<T>(0.5);

        // 3. 八叉树递归 Lambda
        auto octree_recursive = [&](auto& self_ref, T x0, T y0, T z0, T x1, T y1, T z1) -> void
        {
            // --- 第一阶段：利用特化原子区间函数进行严格剪枝 ---
            // 调用我们之前编写的 SOS (Sum of Squares) 评估函数
            auto res_iv = StuCanvas::utils::evaluate_circle_3d_sos_implicit(
                StuCanvas::Interval<T>(x0, x1),
                StuCanvas::Interval<T>(y0, y1),
                StuCanvas::Interval<T>(z0, z1),
                d.cx, d.cy, d.cz, nx, ny, nz, r_sq
            );

            // 如果隐函数的下界 (lower) 大于 0，说明整个体素内没有任何点能满足 F(P)=0
            // 使用一个极其微小的 epsilon 处理浮点数舍入误差
            if (res_iv.lower > static_cast<T>(1e-12)) return;

            // --- 第二阶段：终止判定 (0.5 像素级别) ---
            T w = x1 - x0;
            T h = y1 - y0;
            T db = z1 - z0;

            if (w <= stop_thres_x && h <= stop_thres_y && db <= stop_thres_z)
            {
                // 命中圆周！取体素中心点存入结果集
                self.result_points_3d.emplace_back(Point3D<T>{
                    (x0 + x1) * static_cast<T>(0.5),
                    (y0 + y1) * static_cast<T>(0.5),
                    (z0 + z1) * static_cast<T>(0.5)
                });
                return;
            }

            // --- 第三阶段：八叉树分裂 ---
            T mx = x0 + w * static_cast<T>(0.5);
            T my = y0 + h * static_cast<T>(0.5);
            T mz = z0 + db * static_cast<T>(0.5);

            // 递归探测 8 个子象限
            self_ref(self_ref, x0, y0, z0, mx, my, mz);
            self_ref(self_ref, mx, y0, z0, x1, my, mz);
            self_ref(self_ref, x0, my, z0, mx, y1, mz);
            self_ref(self_ref, mx, my, z0, x1, y1, mz);
            self_ref(self_ref, x0, y0, mz, mx, my, z1);
            self_ref(self_ref, mx, y0, mz, x1, my, z1);
            self_ref(self_ref, x0, my, mz, mx, y1, z1);
            self_ref(self_ref, mx, my, mz, x1, y1, z1);
        };

        // 4. 从 3D 世界空间全域启动递归采样
        octree_recursive(octree_recursive,
                         ws.x_min, ws.y_min, ws.z_min,
                         ws.x_max, ws.y_max, ws.z_max);
    }


    template <typename T>
    void PlotImplicit_2D(Graph<T>& graph, Node<T>& self) {
        if (!self.implicit_func_2d) return;

        // 构造真正的 Descriptor 传给 StuPlot
        IntervalPlot2DDescriptor<T, std::function<IntervalSet<T>(IntervalSet<T>, IntervalSet<T>)>> desc;

        const auto& c = self.data.implicit_2d_config;

        desc.function = self.implicit_func_2d;
        desc.result = &self.result_points_2d;
        desc.cpu_threads = graph.ResolveThreadCount(0); // 使用 Graph 算出的可用线程

        // 注入由 Solver 同步好的参数
        desc.x_min = c.x_min;
        desc.x_max = c.x_max;
        desc.y_min = c.y_min;
        desc.y_max = c.y_max;
        desc.sampling_threshold = c.sampling_threshold;

        // 其他用户配置参数
        desc.max_recursion_depth = c.max_recursion_depth;
        desc.use_de_refinement = c.use_de;
        desc.verification_epsilon = c.verification_epsilon;
        desc.de_population_size = c.de_pop;
        desc.de_max_generations = c.de_gen;

        // 执行 StuPlot 模块的区间算法
        plot_interval_2D(desc);
    }


    template <typename T>
void PlotImplicit_3D(Graph<T>& graph, Node<T>& self) {
        if (!self.implicit_func_3d) return;

        // 构造 StuPlot 所需的 3D Descriptor
        using FuncType = std::function<IntervalSet<T>(IntervalSet<T>, IntervalSet<T>, IntervalSet<T>)>;
        IntervalPlot3DDescriptor<T, FuncType> desc;

        const auto& c = self.data.implicit_3d_config;

        // 注入函数与结果集
        desc.function = self.implicit_func_3d;
        desc.result = &self.result_points_3d;
        desc.cpu_threads = graph.ResolveThreadCount(0);

        // 注入同步后的空间参数
        desc.x_min = c.x_min; desc.x_max = c.x_max;
        desc.y_min = c.y_min; desc.y_max = c.y_max;
        desc.z_min = c.z_min; desc.z_max = c.z_max;
        desc.sampling_threshold = c.sampling_threshold;

        // 注入算法超参数
        desc.max_recursion_depth = c.max_recursion_depth;
        desc.use_de_refinement = c.use_de;
        desc.verification_epsilon = c.verification_epsilon;
        desc.de_population_size = c.de_pop;
        desc.de_max_generations = c.de_gen;

        // 执行 3D 区间算术采样算法
        plot_interval_3D(desc);
    }


    template <typename T>
void PlotParametric_2D(Graph<T>& graph, Node<T>& self) {
        if (!self.parametric_x_2d || !self.parametric_y_2d) return;

        // 构造 StuPlot 所需的参数方程 Descriptor
        using TFunc = std::function<IntervalSet<T>(IntervalSet<T>)>;
        ParametricPlot2DDescriptor<T, TFunc, TFunc> desc;

        const auto& c = self.data.parametric_2d_config;

        desc.x_func = self.parametric_x_2d;
        desc.y_func = self.parametric_y_2d;
        desc.result = &self.result_points_2d;
        desc.cpu_threads = graph.ResolveThreadCount(0);

        // 注入同步后的参数
        desc.x_min = c.x_min; desc.x_max = c.x_max;
        desc.y_min = c.y_min; desc.y_max = c.y_max;
        desc.t_min = c.t_min; desc.t_max = c.t_max;
        desc.point_spacing = c.point_spacing;
        desc.max_recursion_depth = c.max_recursion_depth;

        // 执行 StuPlot 模块的参数化采样算法
        plot_parametric_2D(desc);
    }


    template <typename T>
void PlotParametric_3D(Graph<T>& graph, Node<T>& self) {
        if (!self.parametric_x_3d || !self.parametric_y_3d || !self.parametric_z_3d) return;

        // 构造 3D 参数化 Descriptor
        using FuncType = std::function<IntervalSet<T>(IntervalSet<T>, IntervalSet<T>)>;
        ParametricPlot3DDescriptor<T, FuncType, FuncType, FuncType> desc;

        const auto& c = self.data.parametric_3d_config;

        desc.x_func = self.parametric_x_3d;
        desc.y_func = self.parametric_y_3d;
        desc.z_func = self.parametric_z_3d;
        desc.result = &self.result_points_3d;
        desc.cpu_threads = graph.ResolveThreadCount(0);

        // 注入由 Solver 同步和由用户初始化的参数
        desc.x_min = c.x_min; desc.x_max = c.x_max;
        desc.y_min = c.y_min; desc.y_max = c.y_max;
        desc.z_min = c.z_min; desc.z_max = c.z_max;

        desc.u_min = c.u_min; desc.u_max = c.u_max;
        desc.v_min = c.v_min; desc.v_max = c.v_max;

        desc.point_spacing = c.point_spacing;
        desc.max_recursion_depth = c.max_recursion_depth;

        // 执行 StuPlot 模块的参数化曲面采样算法
        plot_parametric_3D(desc);
    }
}
