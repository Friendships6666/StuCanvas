#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include "../types/point.hpp"
#include "vector"
#include "utils/platonic_data.hpp"

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
        if (num_points == 0) return;

        self.result_points_2d.reserve(num_points + 1);
        for (size_t i = 0; i <= num_points; ++i)
        {
            T t = static_cast<T>(i) / static_cast<T>(num_points);
            self.result_points_2d.emplace_back(Point2D<T>{
                x_start + t * (x_end - x_start),
                y_start + t * (y_end - y_start)
            });
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

        const T sx = 0.5*(ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T sy = 0.5*(ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T sz = 0.5*(ws.z_max - ws.z_min) / graph.resolution_3d.z;

        const auto num_points = static_cast<size_t>(ceil(std::max({
            abs(x_e - x_s) / sx, abs(y_e - y_s) / sy, abs(z_e - z_s) / sz
        })));

        if (num_points == 0)
        {
            self.result_points_3d.emplace_back(Point3D<T>{x_s, y_s, z_s});
            return;
        }

        self.result_points_3d.reserve(num_points + 1);
        for (size_t i = 0; i <= num_points; ++i)
        {
            T t = static_cast<T>(i) / static_cast<T>(num_points);
            self.result_points_3d.emplace_back(Point3D<T>{
                x_s + t * (x_e - x_s), y_s + t * (y_e - y_s), z_s + t * (z_e - z_s)
            });
        }
    }

    // --- 2D Plotters ---
    template <typename T>
    void PlotStraightLine_2D(Graph<T>& graph, Node<T>& self)
    {
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
        struct Interval
        {
            T low, high;
            Interval operator+(const Interval& o) const { return {low + o.low, high + o.high}; }
            Interval operator-(T s) const { return {low - s, high - s}; }

            Interval sqr() const
            {
                using std::max;
                using std::min;
                T a = low * low;
                T b = high * high;
                if (low <= 0 && high >= 0) return {static_cast<T>(0), max(a, b)};
                return {min(a, b), max(a, b)};
            }

            [[nodiscard]] bool contains_zero() const { return low <= 0 && high >= 0; }
        };


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

    template <typename T>
    void PlotCircle_2D(Graph<T>& graph, Node<T>& self)
    {
        using namespace utils;
        // 注意：ExecuteNodeUpdate 已经清空了 result_points_2d

        const T cx = self.data.circle_2d.cx;
        const T cy = self.data.circle_2d.cy;
        const T r_sq = self.data.circle_2d.r * self.data.circle_2d.r;

        const auto& ws = graph.world_space_2d;
        const T dx_px = 0.5*(ws.x_max - ws.x_min) / graph.resolution_2d.x;
        const T dy_px = 0.5*(ws.y_max - ws.y_min) / graph.resolution_2d.y;

        // 性能关键：四叉树细分的终止阈值设置为 10 个像素的宽度
        const T threshold_x = dx_px * static_cast<T>(10.0);
        const T threshold_y = dy_px * static_cast<T>(10.0);

        // 缓存采样点数值（建议后期将 map 换成 FlatMap 以进一步提升速度）
        std::map<std::pair<T, T>, T> cache;
        auto eval_f = [&](T x, T y)
        {
            auto it = cache.find({x, y});
            if (it != cache.end()) return it->second;
            T val = (x - cx) * (x - cx) + (y - cy) * (y - cy) - r_sq;
            return cache[{x, y}] = val;
        };

        auto quadtree_prune = [&](auto& self_ref, T x0, T y0, T x1, T y1) -> void
        {
            // 1. 区间算术剪枝 (Pruning)
            Interval<T> IX = {x0, x1};
            Interval<T> IY = {y0, y1};
            if (!((IX - cx).sqr() + (IY - cy).sqr() - r_sq).contains_zero()) return;

            T w = x1 - x0;
            T h = y1 - y0;

            // 2. 终止判定：如果当前方格大于 10 像素，则继续细分
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

            // 3. 像素级采样阶段：此时 w, h 约为 10 像素大小
            // 对齐到物理像素边界
            T ax0 = std::floor((x0 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay0 = std::floor((y0 - ws.y_min) / dy_px) * dy_px + ws.y_min;
            T ax1 = std::ceil((x1 - ws.x_min) / dx_px) * dx_px + ws.x_min;
            T ay1 = std::ceil((y1 - ws.y_min) / dy_px) * dy_px + ws.y_min;

            for (T px = ax0; px < ax1; px += dx_px)
            {
                for (T py = ay0; py < ay1; py += dy_px)
                {
                    // 采样像素四角
                    T v_bl = eval_f(px, py);
                    T v_br = eval_f(px + dx_px, py);
                    T v_tl = eval_f(px, py + dy_px);
                    T v_tr = eval_f(px + dx_px, py + dy_px);

                    using std::min;
                    using std::max;
                    T min_v = min({v_bl, v_br, v_tl, v_tr});
                    T max_v = max({v_bl, v_br, v_tl, v_tr});

                    // 介值定理判定像素是否穿过曲线
                    if (min_v <= 0 && max_v >= 0)
                    {
                        // 二次细分：半像素步长 (1 个像素拆为 4 个子格)
                        T hx = px + dx_px * 0.5;
                        T hy = py + dy_px * 0.5;

                        T v_bm = eval_f(hx, py);
                        T v_tm = eval_f(hx, py + dy_px);
                        T v_lm = eval_f(px, hy);
                        T v_rm = eval_f(px + dx_px, hy);
                        T v_mm = eval_f(hx, hy);

                        // 在 4 个子格的 12 条边上插值 (内部复用逻辑)
                        // 子格 1 (左下)
                        lerp_and_push(self.result_points_2d, px, py, hx, py, v_bl, v_bm);
                        lerp_and_push(self.result_points_2d, hx, py, hx, hy, v_bm, v_mm);
                        lerp_and_push(self.result_points_2d, px, hy, hx, hy, v_lm, v_mm);
                        lerp_and_push(self.result_points_2d, px, py, px, hy, v_bl, v_lm);
                        // 子格 2 (右下)
                        lerp_and_push(self.result_points_2d, hx, py, px + dx_px, py, v_bm, v_br);
                        lerp_and_push(self.result_points_2d, px + dx_px, py, px + dx_px, hy, v_br, v_rm);
                        lerp_and_push(self.result_points_2d, hx, hy, px + dx_px, hy, v_mm, v_rm);
                        // 子格 3 (左上)
                        lerp_and_push(self.result_points_2d, hx, hy, hx, py + dy_px, v_mm, v_tm);
                        lerp_and_push(self.result_points_2d, px, py + dy_px, hx, py + dy_px, v_tl, v_tm);
                        lerp_and_push(self.result_points_2d, px, hy, px, py + dy_px, v_lm, v_tl);
                        // 子格 4 (右上)
                        lerp_and_push(self.result_points_2d, px + dx_px, hy, px + dx_px, py + dy_px, v_rm, v_tr);
                        lerp_and_push(self.result_points_2d, hx, py + dy_px, px + dx_px, py + dy_px, v_tm, v_tr);
                    }
                }
            }
        };

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
        using namespace utils;

        // 1. 获取球体参数
        const T cx = self.data.sphere_3d.cx;
        const T cy = self.data.sphere_3d.cy;
        const T cz = self.data.sphere_3d.cz;
        const T r_sq = self.data.sphere_3d.r * self.data.sphere_3d.r;

        // 2. 获取视口与分辨率
        const auto& ws = graph.world_space_3d;
        const T dx_px = (ws.x_max - ws.x_min) / graph.resolution_3d.x;
        const T dy_px = (ws.y_max - ws.y_min) / graph.resolution_3d.y;
        const T dz_px = (ws.z_max - ws.z_min) / graph.resolution_3d.z;

        // 设置细分终止阈值：10个像素单位
        const T thres_x = dx_px * static_cast<T>(10.0);
        const T thres_y = dy_px * static_cast<T>(10.0);
        const T thres_z = dz_px * static_cast<T>(10.0);

        // 隐式函数：f(P) = dist(P, C)^2 - r^2
        auto eval_f = [&](T x, T y, T z)
        {
            return (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz) - r_sq;
        };

        // 3. 八叉树递归逻辑
        auto octree_prune = [&](auto& self_ref, T x0, T y0, T z0, T x1, T y1, T z1) -> void
        {
            // 区间算术判定：该区域是否可能包含球面
            Interval<T> IX = {x0, x1}, IY = {y0, y1}, IZ = {z0, z1};
            if (!((IX - cx).sqr() + (IY - cy).sqr() + (IZ - cz).sqr() - r_sq).contains_zero()) return;

            T w = x1 - x0, h = y1 - y0, d = z1 - z0;

            // 如果方格大于 10 像素，继续细分
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

            // 4. 像素级精密采样 (10x10x10 范围)
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

                        using std::min;
                        using std::max;
                        T v_min = min({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]});
                        T v_max = max({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]});

                        // 判定体素是否穿过球面
                        if (v_min <= 0 && v_max >= 0)
                        {
                            // 5. 子体素细分 (2x2x2)
                            T hx = px + dx_px * 0.5;
                            T hy = py + dy_px * 0.5;
                            T hz = pz + dz_px * 0.5;

                            // 定义子块处理逻辑 (针对 12 条棱边进行插值)
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

                                // 12 条棱边插值
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_e, y_s, z_s, sv[0], sv[1]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_s, x_e, y_e, z_s, sv[2], sv[3]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_e, x_e, y_s, z_e, sv[4], sv[5]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_e, x_e, y_e, z_e, sv[6], sv[7]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_s, y_e, z_s, sv[0], sv[2]);
                                lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_s, x_e, y_e, z_s, sv[1], sv[3]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_e, x_s, y_e, z_e, sv[4], sv[6]);
                                lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_e, x_e, y_e, z_e, sv[5], sv[7]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_s, z_s, x_s, y_s, z_e, sv[0], sv[4]);
                                lerp_3d_push_ref(self.result_points_3d, x_e, y_s, z_s, x_e, y_s, z_e, sv[1], sv[5]);
                                lerp_3d_push_ref(self.result_points_3d, x_s, y_e, z_s, x_s, y_e, z_e, sv[2], sv[6]);
                                lerp_3d_push_ref(self.result_points_3d, x_e, y_e, z_s, x_e, y_e, z_e, sv[3], sv[7]);
                            };

                            // 遍历 8 个子块
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

        // 启动八叉树
        octree_prune(octree_prune, ws.x_min, ws.y_min, ws.z_min, ws.x_max, ws.y_max, ws.z_max);
    }
}
