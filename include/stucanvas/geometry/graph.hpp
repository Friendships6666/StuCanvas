#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>
#include "../types/mesh.hpp"
#include "../types/tri_mesh_halfedge_3d.hpp"
#include "../utils/block_deque.hpp"
#include "../utils/flat_map.hpp"
#include <set>
#include <unordered_set>
#include <utility>
#include "plotter.hpp"
#include "solver.hpp"
#include "../utils/parallel_for.hpp"
#include "utils/platonic_data.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Graph;
    template <typename T>
    struct Node;
    template <typename T>
    using SolverFuncPtr = void (*)(Graph<T>&, Node<T>&);
    template <typename T>
    using PlotterFuncPtr = void (*)(Graph<T>&, Node<T>&);


    enum class NodeMask : uint64_t
    {
        NONE = 0,
        DIRTY = 1ULL << 0,
        VISITED = 1ULL << 1,
        HIDDEN = 1ULL << 2,
        FROZEN = 1ULL << 3,
        SELECTED = 1ULL << 4
    };

    enum class NodeType : uint32_t
    {
        MASK_WORLD = 0xFF000000,
        MASK_CATEGORY = 0x00FF0000,
        MASK_SPECIFIC = 0x0000FFFF,


        WORLD_2D = 0x01000000,
        WORLD_3D = 0x02000000,
        WORLD_META = 0x04000000,


        CAT_POINT = 0x00010000,
        CAT_LINEAR = 0x00020000,
        CAT_CURVE = 0x00030000,
        CAT_SURFACE = 0x00040000,
        CAT_FUNCTION = 0x00050000,
        CAT_SCALAR = 0x00060000,

        POINT_2D_FREE = WORLD_2D | CAT_POINT | 0x0001,
        POINT_2D_INTERSECT = WORLD_2D | CAT_POINT | 0x0002,
        POINT_2D_MID = WORLD_2D | CAT_POINT | 0x0003,
        POINT_2D_SNAP = WORLD_2D | CAT_POINT | 0x0004,

        LINE_2D_SEGMENT = WORLD_2D | CAT_LINEAR | 0x0001,
        LINE_2D_STRAIGHT = WORLD_2D | CAT_LINEAR | 0x0002,
        LINE_2D_TANGENT = WORLD_2D | CAT_LINEAR | 0x0003,
        LINE_2D_RAY = WORLD_2D | CAT_LINEAR | 0x0004,
        POLY_2D_GENERATIVE = WORLD_2D | CAT_CURVE | 0x0005,

        CIRCLE_2D = WORLD_2D | CAT_CURVE | 0x0001,
        ARC_2D = WORLD_2D | CAT_CURVE | 0x0002,


        POINT_3D_FREE = WORLD_3D | CAT_POINT | 0x0001,
        POINT_3D_INTERSECT = WORLD_3D | CAT_POINT | 0x0002,
        POINT_3D_MID = WORLD_3D | CAT_POINT | 0x0003,
        POINT_3D_SNAP = WORLD_3D | CAT_POINT | 0x0004,

        LINE_3D_STRAIGHT = WORLD_3D | CAT_LINEAR | 0x0001,
        LINE_3D_SEGMENT = WORLD_3D | CAT_LINEAR | 0x0002,
        LINE_3D_TANGENT = WORLD_2D | CAT_LINEAR | 0x0003,
        LINE_3D_RAY = WORLD_2D | CAT_LINEAR | 0x0004,


        PLANE_3D = WORLD_3D | CAT_SURFACE | 0x0001,
        SPHERE_3D = WORLD_3D | CAT_SURFACE | 0x0002,
        CYLINDER_3D = WORLD_3D | CAT_SURFACE | 0x0003,
        PLANE_TANGENT_3D = WORLD_3D | CAT_SURFACE | 0x0004,
        PLANE_3D_TRAINGLE = WORLD_3D | CAT_SURFACE | 0x0005,
        CURVE_3D_INTERSECTION = WORLD_3D | CAT_CURVE | 0x0001,


        SCALAR = WORLD_META | CAT_SCALAR | 0x0001,
        FUNC_EXPLICIT = WORLD_META | CAT_FUNCTION | 0x0001,
        FUNC_PARAMETRIC = WORLD_META | CAT_FUNCTION | 0x0002,

        UNKNOWN = 0x00000000
    };


    [[nodiscard]] constexpr bool is_2d(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0xFF000000) == 0x01000000;
    }


    [[nodiscard]] constexpr bool is_3d(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0xFF000000) == 0x02000000;
    }


    [[nodiscard]] constexpr bool is_point(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0x00FF0000) == 0x00010000;
    }


    [[nodiscard]] constexpr bool is_linear(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0x00FF0000) == 0x00020000;
    }

    template <typename T>
    struct Selection
    {
        Graph<T>& graph;
        std::vector<uint64_t> ids;

        // --- 链式筛选方法 ---
        Selection& ByName(const std::string& name)
        {
            // 使用 std::erase_if (C++20) 或 传统的 remove_if 逻辑进行原地过滤，效率更高
            std::erase_if(ids, [&](uint64_t id)
            {
                return graph.GetNode(id)->name != name;
            });
            return *this;
        }

        Selection& ByType(NodeType type)
        {
            std::erase_if(ids, [&](uint64_t id)
            {
                return graph.GetNode(id)->type != type;
            });
            return *this;
        }

        using NodePredicate = bool (*)(NodeType);

        Selection& ByPredicate(NodePredicate pred)
        {
            std::erase_if(ids, [&](uint64_t id)
            {
                return !pred(graph.GetNode(id)->type);
            });
            return *this;
        }

        // --- 核心魔法：隐式转换 ---
        // 当这个对象被赋值给 std::vector<uint64_t> 时自动调用
        operator std::vector<uint64_t>() &&
        {
            return std::move(ids);
        }

        // 允许在没有隐式转换的情况下也能直接拷贝
        operator std::vector<uint64_t>() const &
        {
            return ids;
        }

        // --- 核心魔法：支持 range-based for 循环 ---
        auto begin() { return ids.begin(); }
        auto end() { return ids.end(); }
        auto begin() const { return ids.begin(); }
        auto end() const { return ids.end(); }
    };



    template <typename T>
    struct Node
    {
        using value_type = T;
        uint64_t id{};
        std::string name = "Unnamed";
        static inline std::atomic<uint64_t> global_node_id_counter{0};
        NodeType type = NodeType::UNKNOWN;
        uint64_t mask = 0;
        void set_mask(NodeMask m) noexcept { mask |= static_cast<uint64_t>(m); }

        void clear_mask(NodeMask m) noexcept { mask &= ~static_cast<uint64_t>(m); }

        [[nodiscard]] bool has_mask(NodeMask m) const noexcept { return (mask & static_cast<uint64_t>(m)) != 0; }

        union
        {
            struct
            {
                T x;
                T y;
            } point_2d;

            struct
            {
                T x;
                T y;
                T z;
            } point_3d;

            struct
            {
                T x0;
                T y0;
                T x1;
                T y1;
            } line_2d;

            struct
            {
                T x0;
                T y0;
                T z0;
                T x1;
                T y1;
                T z1;
            } line_3d;

            struct
            {
                T cx;
                T cy;
                T r;
            } circle_2d;

            struct {
                T cx, cy, cz; // 中心点 (由 Solver 更新)
                T r;          // 半径
            } sphere_3d;

            struct
            {
                T ox;
                T oy;
                T oz;
                T ux;
                T uy;
                T uz;
                T vx;
                T vy;
                T vz;
            } plane_3d;

            struct
            {
                T cx, cy, r;
                T start_angle;
                T end_angle;
            } arc_2d;

            struct
            {
                T x0, y0, x1, y1;
                T distance;
            } parallel_line_2d;

            struct
            {
                T x, y;
                T guess_x, guess_y;
            } snap_2d;

            struct
            {
                T x, y, z;
                T guess_x, guess_y, guess_z;
            } snap_3d;

            struct
            {
                T cx, cy;
                T side_len;
                uint32_t n;
            } poly_gen_2d;

            struct {
                T cx, cy, cz; // 中心点 (由 Solver 更新)
                T radius;     // 半径
                int type;     // 4, 6, 8, 12, 20
            } platonic_solid_3d;
        } data;

        utils::BlockDeque<uint64_t, 4> parents;
        utils::BlockDeque<uint64_t, 16> children;
        std::vector<Point2D<T>> result_points_2d;
        std::vector<Point3D<T>> result_points_3d;
        std::vector<Mesh2D<T>> result_meshes_2d;
        std::vector<Mesh3D<T>> result_meshes_3d;
        SolverFuncPtr<T> solver = nullptr;
        PlotterFuncPtr<T> plotter = nullptr;

        Node() noexcept
        {
            id = global_node_id_counter.fetch_add(1, std::memory_order_relaxed);
        }


        // --- 辅助结构体 ---
        struct SpacingStats { T min_dist, max_dist, avg_dist, variance; };
        struct AABB2D { T x_min, y_min, x_max, y_max; };
        struct AABB3D { T x_min, y_min, z_min, x_max, y_max, z_max; };

        // ==========================================
        // 1 & 2. 包围盒 (AABB)
        // ==========================================
        AABB2D GetAABB2D() const {
            if (result_points_2d.empty()) return {0, 0, 0, 0};
            T xmin = result_points_2d[0].x, xmax = xmin, ymin = result_points_2d[0].y, ymax = ymin;
            for (const auto& p : result_points_2d) {
                xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
                ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
            }
            return {xmin, ymin, xmax, ymax};
        }

        AABB3D GetAABB3D() const {
            if (result_points_3d.empty()) return {0,0,0,0,0,0};
            T xmin = result_points_3d[0].x, xmax = xmin, ymin = result_points_3d[0].y, ymax = ymin, zmin = result_points_3d[0].z, zmax = zmin;
            for (const auto& p : result_points_3d) {
                xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
                ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
                zmin = std::min(zmin, p.z); zmax = std::max(zmax, p.z);
            }
            return {xmin, ymin, zmin, xmax, ymax, zmax};
        }

        // ==========================================
        // 3 & 4. 重心 (Centroid)
        // ==========================================
        Point2D<T> GetCentroid2D() const {
            if (result_points_2d.empty()) return {0, 0};
            T sx = 0, sy = 0;
            for (const auto& p : result_points_2d) { sx += p.x; sy += p.y; }
            T inv_n = static_cast<T>(1.0) / result_points_2d.size();
            return {sx * inv_n, sy * inv_n};
        }

        Point3D<T> GetCentroid3D() const {
            if (result_points_3d.empty()) return {0, 0, 0};
            T sx = 0, sy = 0, sz = 0;
            for (const auto& p : result_points_3d) { sx += p.x; sy += p.y; sz += p.z; }
            T inv_n = static_cast<T>(1.0) / result_points_3d.size();
            return {sx * inv_n, sy * inv_n, sz * inv_n};
        }

        // ==========================================
        // 5 & 6. 间距综合统计 (单次遍历完成 Min/Max/Avg/Var)
        // ==========================================
        SpacingStats GetSpacingStats2D() const {
            if (result_points_2d.size() < 2) return {0,0,0,0};
            T min_d = std::numeric_limits<T>::max(), max_d = 0, sum_d = 0, sum_sq_d = 0;
            size_t n = result_points_2d.size() - 1;
            for (size_t i = 0; i < n; ++i) {
                T dx = result_points_2d[i+1].x - result_points_2d[i].x;
                T dy = result_points_2d[i+1].y - result_points_2d[i].y;
                T d = std::sqrt(dx*dx + dy*dy);
                min_d = std::min(min_d, d); max_d = std::max(max_d, d);
                sum_d += d; sum_sq_d += d*d;
            }
            T avg = sum_d / n;
            return {min_d, max_d, avg, (sum_sq_d / n) - (avg * avg)};
        }

        SpacingStats GetSpacingStats3D() const {
            if (result_points_3d.size() < 2) return {0,0,0,0};
            T min_d = std::numeric_limits<T>::max(), max_d = 0, sum_d = 0, sum_sq_d = 0;
            size_t n = result_points_3d.size() - 1;
            for (size_t i = 0; i < n; ++i) {
                T dx = result_points_3d[i+1].x - result_points_3d[i].x, dy = result_points_3d[i+1].y - result_points_3d[i].y, dz = result_points_3d[i+1].z - result_points_3d[i].z;
                T d = std::sqrt(dx*dx + dy*dy + dz*dz);
                min_d = std::min(min_d, d); max_d = std::max(max_d, d);
                sum_d += d; sum_sq_d += d*d;
            }
            T avg = sum_d / n;
            return {min_d, max_d, avg, (sum_sq_d / n) - (avg * avg)};
        }

        // ==========================================
        // 7. 协方差 (Covariance XY)
        // ==========================================
        T GetCovariance2D() const {
            if (result_points_2d.empty()) return 0;
            auto c = GetCentroid2D();
            T cov = 0;
            for (const auto& p : result_points_2d) cov += (p.x - c.x) * (p.y - c.y);
            return cov / result_points_2d.size();
        }

        // ==========================================
        // 8 & 9. 密度 (Density) 与 机器精度保护
        // ==========================================
        T GetDensity2D() const {
            if (result_points_2d.empty()) return 0;
            auto b = GetAABB2D();
            T area = (b.x_max - b.x_min) * (b.y_max - b.y_min);
            // 使用机器精度替代 1e-9
            if (area <= std::numeric_limits<T>::epsilon()) return static_cast<T>(result_points_2d.size());
            return static_cast<T>(result_points_2d.size()) / area;
        }

        T GetDensity3D() const {
            if (result_points_3d.empty()) return 0;
            auto b = GetAABB3D();
            T vol = (b.x_max - b.x_min) * (b.y_max - b.y_min) * (b.z_max - b.z_min);
            if (vol <= std::numeric_limits<T>::epsilon()) return static_cast<T>(result_points_3d.size());
            return static_cast<T>(result_points_3d.size()) / vol;
        }

        // ==========================================
        // 10. 最大外接半径 (Bounding Radius)
        // ==========================================
        T GetBoundingRadius3D() const {
            if (result_points_3d.empty()) return 0;
            auto c = GetCentroid3D();
            T max_r2 = 0;
            for (const auto& p : result_points_3d) {
                T dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
                max_r2 = std::max(max_r2, dx*dx + dy*dy + dz*dz);
            }
            return std::sqrt(max_r2);
        }
        // --- 协方差矩阵结构 ---
        struct CovarianceMatrix3D {
            T cxx, cyy, czz; // 对角线分量 (方差)
            T cxy, cyz, czx; // 非对角线分量 (协方差)
        };

        // ==========================================
        // 3D 协方差矩阵计算
        // ==========================================
        CovarianceMatrix3D GetCovariance3D() const {
            if (result_points_3d.empty()) return {0,0,0,0,0,0};

            // 1. 获取重心
            auto c = GetCentroid3D();

            // 2. 单次遍历计算所有阶矩
            T cxx=0, cyy=0, czz=0, cxy=0, cyz=0, czx=0;
            for (const auto& p : result_points_3d) {
                T dx = p.x - c.x;
                T dy = p.y - c.y;
                T dz = p.z - c.z;

                cxx += dx * dx;
                cyy += dy * dy;
                czz += dz * dz;
                cxy += dx * dy;
                cyz += dy * dz;
                czx += dz * dx;
            }

            T inv_n = static_cast<T>(1.0) / static_cast<T>(result_points_3d.size());
            return {
                cxx * inv_n, cyy * inv_n, czz * inv_n,
                cxy * inv_n, cyz * inv_n, czx * inv_n
            };
        }

        /**
         * @brief 2D 像素密度 (不使用长度)
         * 计算公式：点数 / AABB像素最大边长
         * 理想值：2.0 ~ 3.0 (代表在主轴方向每像素有 2-3 个点)
         */
        T GetPixelDensity2D(const Graph<T>& graph) const {
            if (result_points_2d.empty()) return 0;

            // 1. 计算世界坐标下的 AABB
            auto aabb = GetAABB2D();

            // 2. 获取当前的像素步长
            const auto& ws = graph.world_space_2d;
            const auto& res = graph.resolution_2d;
            T dx_unit = (ws.x_max - ws.x_min) / res.x;
            T dy_unit = (ws.y_max - ws.y_min) / res.y;

            // 3. 转化为像素跨度 (Pixel counts)
            T w_px = (aabb.x_max - aabb.x_min) / (dx_unit + std::numeric_limits<T>::epsilon());
            T h_px = (aabb.y_max - aabb.y_min) / (dy_unit + std::numeric_limits<T>::epsilon());

            // 4. 取主轴跨度作为参考基准
            T max_dim_px = std::max(w_px, h_px);

            if (max_dim_px <= std::numeric_limits<T>::epsilon()) return static_cast<T>(result_points_2d.size());

            return static_cast<T>(result_points_2d.size()) / max_dim_px;
        }

        /**
         * @brief 3D 像素密度 (不使用长度)
         */
        T GetPixelDensity3D(const Graph<T>& graph) const {
            if (result_points_3d.empty()) return 0;

            auto aabb = GetAABB3D();
            const auto& ws = graph.world_space_3d;
            const auto& res = graph.resolution_3d;

            T dx_u = (ws.x_max - ws.x_min) / res.x;
            T dy_u = (ws.y_max - ws.y_min) / res.y;
            T dz_u = (ws.z_max - ws.z_min) / res.z;

            T w_px = (aabb.x_max - aabb.x_min) / (dx_u + std::numeric_limits<T>::epsilon());
            T h_px = (aabb.y_max - aabb.y_min) / (dy_u + std::numeric_limits<T>::epsilon());
            T d_px = (aabb.z_max - aabb.z_min) / (dz_u + std::numeric_limits<T>::epsilon());

            T max_dim_px = std::max({w_px, h_px, d_px});

            if (max_dim_px <= std::numeric_limits<T>::epsilon()) return static_cast<T>(result_points_3d.size());

            return static_cast<T>(result_points_3d.size()) / max_dim_px;
        }

        /**
         * @brief 计算 2D 点云相邻点之间的平均像素距离
         * @return 平均每个步长跨越的像素个数 (Euclidean distance in pixels)
         */
        T GetAveragePixelSpacing2D(const Graph<T>& graph) const {
            if (result_points_2d.size() < 2) return 0;

            // 1. 计算世界单位到像素单位的缩放因子
            const auto& ws = graph.world_space_2d;
            const auto& res = graph.resolution_2d;
            T sx = res.x / (ws.x_max - ws.x_min + std::numeric_limits<T>::epsilon());
            T sy = res.y / (ws.y_max - ws.y_min + std::numeric_limits<T>::epsilon());

            T total_pixel_dist = 0;
            for (size_t i = 0; i < result_points_2d.size() - 1; ++i) {
                // 将世界坐标差值转换为像素差值
                T dx_px = (result_points_2d[i+1].x - result_points_2d[i].x) * sx;
                T dy_px = (result_points_2d[i+1].y - result_points_2d[i].y) * sy;

                // 计算像素空间下的欧几里得距离
                total_pixel_dist += std::sqrt(dx_px * dx_px + dy_px * dy_px);
            }

            return total_pixel_dist / static_cast<T>(result_points_2d.size() - 1);
        }

        /**
         * @brief 计算 3D 点云相邻点之间的平均像素距离
         */
        T GetAveragePixelSpacing3D(const Graph<T>& graph) const {
            if (result_points_3d.size() < 2) return 0;

            const auto& ws = graph.world_space_3d;
            const auto& res = graph.resolution_3d;
            T sx = res.x / (ws.x_max - ws.x_min + std::numeric_limits<T>::epsilon());
            T sy = res.y / (ws.y_max - ws.y_min + std::numeric_limits<T>::epsilon());
            T sz = res.z / (ws.z_max - ws.z_min + std::numeric_limits<T>::epsilon());

            T total_pixel_dist = 0;
            for (size_t i = 0; i < result_points_3d.size() - 1; ++i) {
                T dx_px = (result_points_3d[i+1].x - result_points_3d[i].x) * sx;
                T dy_px = (result_points_3d[i+1].y - result_points_3d[i].y) * sy;
                T dz_px = (result_points_3d[i+1].z - result_points_3d[i].z) * sz;

                total_pixel_dist += std::sqrt(dx_px * dx_px + dy_px * dy_px + dz_px * dz_px);
            }

            return total_pixel_dist / static_cast<T>(result_points_3d.size() - 1);
        }


        private:
        // ==========================================================
        // 步骤 1: 3D 线性化空间哈希 (Linear Spatial Grid) 基础组件
        // ==========================================================


        // 获取点云包围盒
        AABB3D Internal_GetPointsAABB(const std::vector<Point3D<T>>& pts) const {
            if (pts.empty()) return {0,0,0, 0,0,0};
            AABB3D b = {pts[0].x, pts[0].y, pts[0].z, pts[0].x, pts[0].y, pts[0].z};
            for (const auto& p : pts) {
                if (p.x < b.x_min) b.x_min = p.x; if (p.x > b.x_max) b.x_max = p.x;
                if (p.y < b.y_min) b.y_min = p.y; if (p.y > b.y_max) b.y_max = p.y;
                if (p.z < b.z_min) b.z_min = p.z; if (p.z > b.z_max) b.z_max = p.z;
            }
            return b;
        }

        // 线性哈希核心结构
        struct LinearGrid {
            std::vector<uint32_t> cell_starts;   // 长度为 Nx*Ny*Nz + 1，记录每个格子的起始索引
            std::vector<uint32_t> point_indices; // 长度为 N，重新排列后的点索引
            size_t nx = 1, ny = 1, nz = 1;       // 空间在三轴上被划分的格子数
            T dx = 1, dy = 1, dz = 1;            // 单个格子的物理尺寸
            AABB3D bounds = {0,0,0,0,0,0};
        };

        // 将 3D 物理坐标转换为 1D 扁平数组索引
        size_t GetVoxelFlatIndex(const Point3D<T>& p, const LinearGrid& lg) const {
            int64_t ix = static_cast<int64_t>((p.x - lg.bounds.x_min) / lg.dx);
            int64_t iy = static_cast<int64_t>((p.y - lg.bounds.y_min) / lg.dy);
            int64_t iz = static_cast<int64_t>((p.z - lg.bounds.z_min) / lg.dz);

            // 严格钳制防止浮点精度导致的越界
            ix = std::clamp<int64_t>(ix, 0, static_cast<int64_t>(lg.nx - 1));
            iy = std::clamp<int64_t>(iy, 0, static_cast<int64_t>(lg.ny - 1));
            iz = std::clamp<int64_t>(iz, 0, static_cast<int64_t>(lg.nz - 1));

            // Z -> Y -> X 的顺序扁平化
            return static_cast<size_t>(iz * (lg.nx * lg.ny) + iy * lg.nx + ix);
        }

        /**
         * @brief 构建线性空间网格
         * 算法原理: 扫描两遍点云。第一遍统计每个格子里的点数；第二遍利用前缀和将点索引按格子分类存入连续数组。
         */
        LinearGrid Internal_BuildLinearGrid(const std::vector<Point3D<T>>& pts) const {
            LinearGrid lg;
            if (pts.empty()) return lg;

            lg.bounds = Internal_GetPointsAABB(pts);
            const T eps = std::numeric_limits<T>::epsilon();

            // 计算世界空间最大跨度
            T span_x = lg.bounds.x_max - lg.bounds.x_min;
            T span_y = lg.bounds.y_max - lg.bounds.y_min;
            T span_z = lg.bounds.z_max - lg.bounds.z_min;
            T max_span = std::max({span_x, span_y, span_z});

            if (max_span <= eps) {
                // 退化情况：所有点在同一位置
                lg.nx = lg.ny = lg.nz = 1;
                lg.dx = lg.dy = lg.dz = 1.0;
            } else {
                // 动态决定的格子密度。对于凸包算法，单轴 32 份切分能平衡内存和查找效率
                const size_t target_bins = 32;
                T cell_size = max_span / static_cast<T>(target_bins);
                if (cell_size <= eps) cell_size = eps * 10;

                // 统一使用正方体格子，方便后续进行球形邻域搜索
                lg.dx = lg.dy = lg.dz = cell_size;
                lg.nx = static_cast<size_t>(span_x / cell_size) + 1;
                lg.ny = static_cast<size_t>(span_y / cell_size) + 1;
                lg.nz = static_cast<size_t>(span_z / cell_size) + 1;
            }

            size_t num_cells = lg.nx * lg.ny * lg.nz;

            // Pass 1: 统计每个格子有多少个点
            std::vector<uint32_t> counts(num_cells, 0);
            for (const auto& p : pts) {
                counts[GetVoxelFlatIndex(p, lg)]++;
            }

            // Pass 2: 计算前缀和 (Prefix Sum)，确定每个格子在连续数组里的起始段
            lg.cell_starts.assign(num_cells + 1, 0);
            for (size_t i = 0; i < num_cells; ++i) {
                lg.cell_starts[i + 1] = lg.cell_starts[i] + counts[i];
            }

            // Pass 3: 填充数据。将所有点的原始索引重排，属于同一个格子的点挨在一起
            lg.point_indices.resize(pts.size());
            std::vector<uint32_t> current_offsets = lg.cell_starts;
            for (size_t i = 0; i < pts.size(); ++i) {
                size_t cell_idx = GetVoxelFlatIndex(pts[i], lg);
                lg.point_indices[current_offsets[cell_idx]++] = static_cast<uint32_t>(i);
            }

            return lg;
        }


        private:
        // ==========================================================
        // 步骤 2: 随机化 PCA 与 栅格加速分割 (无损)
        // ==========================================================

        /**
         * @brief 雅可比旋转法提取对称矩阵的最大特征向量 (3x3)
         * 这是 PCA 的核心数学实现，极其稳健
         */
        Point3D<T> Internal_ExtractJacobiPrincipal(T cov[3][3]) const {
            T V[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
            T eps = std::numeric_limits<T>::epsilon();

            for (int iter = 0; iter < 15; ++iter) { // 3x3矩阵15次迭代足以达到机器精度
                int p = 0, q = 1;
                T max_val = std::abs(cov[0][1]);
                if (std::abs(cov[0][2]) > max_val) { p = 0; q = 2; max_val = std::abs(cov[0][2]); }
                if (std::abs(cov[1][2]) > max_val) { p = 1; q = 2; max_val = std::abs(cov[1][2]); }

                if (max_val < eps) break;

                T theta = static_cast<T>(0.5) * std::atan2(static_cast<T>(2.0) * cov[p][q], cov[q][q] - cov[p][p]);
                T cos_t = std::cos(theta);
                T sin_t = std::sin(theta);

                // 更新特征向量矩阵 V
                for (int i = 0; i < 3; ++i) {
                    T vip = V[i][p], viq = V[i][q];
                    V[i][p] = cos_t * vip - sin_t * viq;
                    V[i][q] = sin_t * vip + cos_t * viq;
                }

                // 更新协方差矩阵 (相似变换)
                T app = cos_t * cos_t * cov[p][p] - static_cast<T>(2.0) * sin_t * cos_t * cov[p][q] + sin_t * sin_t * cov[q][q];
                T aqq = sin_t * sin_t * cov[p][p] + static_cast<T>(2.0) * sin_t * cos_t * cov[p][q] + cos_t * cos_t * cov[q][q];
                T apq = (cos_t * cos_t - sin_t * sin_t) * cov[p][q] + sin_t * cos_t * (cov[p][p] - cov[q][q]);
                cov[p][p] = app; cov[q][q] = aqq; cov[p][q] = cov[q][p] = 0;

                for (int i = 0; i < 3; ++i) {
                    if (i != p && i != q) {
                        T aip = cov[i][p], aiq = cov[i][q];
                        cov[i][p] = cov[p][i] = cos_t * aip - sin_t * aiq;
                        cov[i][q] = cov[q][i] = sin_t * aip + cos_t * aiq;
                    }
                }
            }

            // 寻找最大特征值对应的列
            int best_idx = 0;
            T max_ev = cov[0][0];
            if (cov[1][1] > max_ev) { max_ev = cov[1][1]; best_idx = 1; }
            if (cov[2][2] > max_ev) { max_ev = cov[2][2]; best_idx = 2; }

            return { V[0][best_idx], V[1][best_idx], V[2][best_idx] };
        }

        /**
         * @brief 随机化 PCA 计算主轴
         * 通过对点云进行固定步长的下采样（不丢失点云信息，仅用于计算方向），极速获得分割面
         */
        Point3D<T> Internal_ComputeRandomizedPCA(const std::vector<Point3D<T>>& pts, Point3D<T>& out_center) const {
            const size_t n = pts.size();
            const size_t K = std::min(n, static_cast<size_t>(2048)); // 固定采样 2048 点进行主轴估计
            const size_t step = n / K;

            T sx = 0, sy = 0, sz = 0;
            for (size_t i = 0; i < n; i += step) {
                sx += pts[i].x; sy += pts[i].y; sz += pts[i].z;
            }
            out_center = { sx / K, sy / K, sz / K };

            T cov[3][3] = {0};
            for (size_t i = 0; i < n; i += step) {
                T dx = pts[i].x - out_center.x;
                T dy = pts[i].y - out_center.y;
                T dz = pts[i].z - out_center.z;
                cov[0][0] += dx * dx; cov[0][1] += dx * dy; cov[0][2] += dx * dz;
                cov[1][1] += dy * dy; cov[1][2] += dy * dz;
                cov[2][2] += dz * dz;
            }
            cov[1][0] = cov[0][1]; cov[2][0] = cov[0][2]; cov[2][1] = cov[1][2];

            return Internal_ExtractJacobiPrincipal(cov);
        }

        /**
         * @brief 栅格加速的无损点云分割
         * 算法原理：对于每一个体素格，先判断它的 AABB 是否完全在分割平面的一侧。
         * 如果是，则直接将格内所有点的索引整体搬运，跳过逐点的点积计算。
         */
        void Internal_SplitCluster(const std::vector<Point3D<T>>& pts,
                                   const LinearGrid& lg,
                                   const Point3D<T>& center,
                                   const Point3D<T>& normal,
                                   std::vector<Point3D<T>>& left,
                                   std::vector<Point3D<T>>& right) const
        {
            size_t num_cells = lg.nx * lg.ny * lg.nz;
            T eps = std::numeric_limits<T>::epsilon();

            for (size_t i = 0; i < num_cells; ++i) {
                size_t p_start = lg.cell_starts[i];
                size_t p_end = lg.cell_starts[i + 1];
                if (p_start == p_end) continue; // 空格子

                // 计算该体素的 8 个顶点中，离平面最近和最远的距离
                // 这里使用了一个高效的 AABB 判别逻辑
                size_t ix = i % lg.nx;
                size_t iy = (i / lg.nx) % lg.ny;
                size_t iz = i / (lg.nx * lg.ny);

                AABB3D cell_box = {
                    lg.bounds.x_min + ix * lg.dx, lg.bounds.y_min + iy * lg.dy, lg.bounds.z_min + iz * lg.dz,
                    lg.bounds.x_min + (ix+1) * lg.dx, lg.bounds.y_min + (iy+1) * lg.dy, lg.bounds.z_min + (iz+1) * lg.dz
                };

                // 计算 AABB 在法线方向上的最小/最大投影距离
                T min_d = 0, max_d = 0;
                auto compute_extent = [&](T n_comp, T b_min, T b_max, T& d_min, T& d_max) {
                    T p1 = n_comp * (b_min - 0); // 这里的 0 是局部化的中心，后续统一减
                    T p2 = n_comp * (b_max - 0);
                    d_min += std::min(p1, p2);
                    d_max += std::max(p1, p2);
                };

                // 将平面方程移动到 AABB 局部坐标系
                T offset = normal.x * center.x + normal.y * center.y + normal.z * center.z;
                T d_min = -offset, d_max = -offset;
                compute_extent(normal.x, cell_box.x_min, cell_box.x_max, d_min, d_max);
                compute_extent(normal.y, cell_box.y_min, cell_box.y_max, d_min, d_max);
                compute_extent(normal.z, cell_box.z_min, cell_box.z_max, d_min, d_max);

                // --- 决策分支 ---
                if (d_min > -eps) {
                    // 整个格子都在平面正向 (Right)
                    for (size_t j = p_start; j < p_end; ++j) right.push_back(pts[lg.point_indices[j]]);
                } else if (d_max < eps) {
                    // 整个格子都在平面负向 (Left)
                    for (size_t j = p_start; j < p_end; ++j) left.push_back(pts[lg.point_indices[j]]);
                } else {
                    // 格子跨越平面，必须逐点判断
                    for (size_t j = p_start; j < p_end; ++j) {
                        const auto& p = pts[lg.point_indices[j]];
                        T dist = (p.x - center.x) * normal.x + (p.y - center.y) * normal.y + (p.z - center.z) * normal.z;
                        if (dist > 0) right.push_back(p);
                        else left.push_back(p);
                    }
                }
            }
        }


        private:
        // 计算 AABB 盒到平面的理论最大正向距离 (无信息损失的剪枝核心)
        T Internal_MaxDistAABBToPlane(const AABB3D& box, const Point3D<T>& p_on_plane, const Point3D<T>& normal) const {
            // 根据法线方向选择 AABB 中最远的一个顶点进行点积计算
            Point3D<T> extreme_pt = {
                (normal.x > 0) ? box.x_max : box.x_min,
                (normal.y > 0) ? box.y_max : box.y_min,
                (normal.z > 0) ? box.z_max : box.z_min
            };
            return (extreme_pt.x - p_on_plane.x) * normal.x +
                   (extreme_pt.y - p_on_plane.y) * normal.y +
                   (extreme_pt.z - p_on_plane.z) * normal.z;
        }

        // 在给定点簇和格栅中，寻找离指定面最远的点
        size_t Internal_FindFurthestPoint(const std::vector<Point3D<T>>& pts,
                                          const LinearGrid& lg,
                                          const Point3D<T>& face_p,
                                          const Point3D<T>& face_n,
                                          T& out_max_dist) const
        {
            size_t best_idx = static_cast<size_t>(-1);
            out_max_dist = std::numeric_limits<T>::epsilon(); // 初始阈值为机器精度

            size_t num_cells = lg.nx * lg.ny * lg.nz;
            for (size_t i = 0; i < num_cells; ++i) {
                size_t p_start = lg.cell_starts[i];
                size_t p_end = lg.cell_starts[i + 1];
                if (p_start == p_end) continue;

                // 1. 空间剪枝：计算当前格子理论上的最大贡献
                size_t ix = i % lg.nx;
                size_t iy = (i / lg.nx) % lg.ny;
                size_t iz = i / (lg.nx * lg.ny);

                AABB3D cell_box = {
                    lg.bounds.x_min + ix * lg.dx, lg.bounds.y_min + iy * lg.dy, lg.bounds.z_min + iz * lg.dz,
                    lg.bounds.x_min + (ix+1) * lg.dx, lg.bounds.y_min + (iy+1) * lg.dy, lg.bounds.z_min + (iz+1) * lg.dz
                };

                // 如果格子的最远点都比当前全局最大值近，直接跳过整个格子的点
                if (Internal_MaxDistAABBToPlane(cell_box, face_p, face_n) <= out_max_dist) {
                    continue;
                }

                // 2. 只有可能产生新极值的格子，才进入点级计算
                for (size_t j = p_start; j < p_end; ++j) {
                    size_t pt_idx = lg.point_indices[j];
                    const auto& P = pts[pt_idx];
                    T d = (P.x - face_p.x) * face_n.x + (P.y - face_p.y) * face_n.y + (P.z - face_p.z) * face_n.z;
                    if (d > out_max_dist) {
                        out_max_dist = d;
                        best_idx = pt_idx;
                    }
                }
            }
            return best_idx;
        }


private:
        /**
         * @brief 顶级鲁棒版 QuickHull 实现
         * 采用 Edge-Map 拓扑重建技术，100% 解决撕裂、碎片化及法线反转问题。
         */
        Mesh3D<T> Internal_BuildQuickHullLossless(const std::vector<Point3D<T>>& pts) const
        {
            Mesh3D<T> mesh;
            const size_t n_pts = pts.size();
            if (n_pts < 4) return mesh;

            // 1. 动态计算误差容差 (防止共面碎面)
            T max_c = 0;
            for (const auto& p : pts) max_c = std::max({max_c, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
            const T tolerance = std::numeric_limits<T>::epsilon() * std::max(static_cast<T>(1.0), max_c) * static_cast<T>(128.0);

            auto sub =[](const Point3D<T>& a, const Point3D<T>& b) { return Point3D<T>{a.x-b.x, a.y-b.y, a.z-b.z}; };
            auto dot =[](const Point3D<T>& a, const Point3D<T>& b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
            auto cross =[](const Point3D<T>& a, const Point3D<T>& b) {
                return Point3D<T>{a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
            };

            // 2. 初始四面体生成
            size_t p[4] = {0, 0, 0, 0};
            for (size_t i = 1; i < n_pts; ++i) if (pts[i].x < pts[p[0]].x) p[0] = i;
            T max_d2 = -1;
            for (size_t i = 0; i < n_pts; ++i) {
                T d2 = dot(sub(pts[i], pts[p[0]]), sub(pts[i], pts[p[0]]));
                if (d2 > max_d2) { max_d2 = d2; p[1] = i; }
            }
            T max_l2 = -1; Point3D<T> v01 = sub(pts[p[1]], pts[p[0]]);
            for (size_t i = 0; i < n_pts; ++i) {
                Point3D<T> cr = cross(v01, sub(pts[i], pts[p[0]]));
                T d2 = dot(cr, cr); if (d2 > max_l2) { max_l2 = d2; p[2] = i; }
            }
            Point3D<T> n_base = cross(v01, sub(pts[p[2]], pts[p[0]]));
            T max_pd = -1;
            for (size_t i = 0; i < n_pts; ++i) {
                T d = std::abs(dot(sub(pts[i], pts[p[0]]), n_base));
                if (d > max_pd) { max_pd = d; p[3] = i; }
            }
            if (max_pd <= tolerance) return mesh;

            Point3D<T> centroid = {0,0,0};
            for(int i=0; i<4; ++i) { centroid.x += pts[p[i]].x; centroid.y += pts[p[i]].y; centroid.z += pts[p[i]].z; }
            centroid.x *= 0.25; centroid.y *= 0.25; centroid.z *= 0.25;

            struct Face {
                uint32_t v[3];
                Point3D<T> normal;
                std::vector<uint32_t> outside_set;
                bool dead = false;
            };
            std::vector<Face> faces;

            auto add_face = [&](uint32_t v1, uint32_t v2, uint32_t v3) {
                Point3D<T> n = cross(sub(pts[v2], pts[v1]), sub(pts[v3], pts[v1]));
                if (dot(n, sub(pts[v1], centroid)) < 0) std::swap(v2, v3);
                n = cross(sub(pts[v2], pts[v1]), sub(pts[v3], pts[v1]));
                T len = std::sqrt(dot(n, n));
                n.x /= len; n.y /= len; n.z /= len;
                faces.push_back({{v1, v2, v3}, n, {}, false});
            };

            add_face(p[0], p[1], p[2]); add_face(p[0], p[2], p[3]);
            add_face(p[0], p[3], p[1]); add_face(p[1], p[3], p[2]);

            // 初始点分配
            for (uint32_t i = 0; i < n_pts; ++i) {
                if (i==p[0] || i==p[1] || i==p[2] || i==p[3]) continue;
                for (auto& f : faces) {
                    if (dot(sub(pts[i], pts[f.v[0]]), f.normal) > tolerance) {
                        f.outside_set.push_back(i); break;
                    }
                }
            }

            // 3. 增量生长
            std::vector<size_t> face_stack;
            for(size_t i=0; i<faces.size(); ++i) face_stack.push_back(i);

            while (!face_stack.empty()) {
                size_t f_idx = face_stack.back();
                face_stack.pop_back();

                if (faces[f_idx].dead || faces[f_idx].outside_set.empty()) continue;

                // 找最远点
                uint32_t best_p = faces[f_idx].outside_set[0];
                T max_d = -1;
                for (uint32_t pid : faces[f_idx].outside_set) {
                    T d = dot(sub(pts[pid], pts[faces[f_idx].v[0]]), faces[f_idx].normal);
                    if (d > max_d) { max_d = d; best_p = pid; }
                }

                // 收集可见面并汇总外部集
                std::vector<uint32_t> pool;
                std::vector<std::pair<uint32_t, uint32_t>> edges;
                for (auto& f : faces) {
                    if (f.dead) continue;
                    if (dot(sub(pts[best_p], pts[f.v[0]]), f.normal) > tolerance) {
                        f.dead = true;
                        pool.insert(pool.end(), f.outside_set.begin(), f.outside_set.end());
                        for(int k=0; k<3; ++k) edges.push_back({f.v[k], f.v[(k+1)%3]});
                    }
                }

                // 提取地平线边缘 (关键：去重边即为边界)
                std::vector<std::pair<uint32_t, uint32_t>> horizon;
                for (const auto& e : edges) {
                    bool shared = false;
                    for (size_t h=0; h<horizon.size(); ++h) {
                        if (horizon[h].first == e.second && horizon[h].second == e.first) {
                            horizon[h] = horizon.back(); horizon.pop_back();
                            shared = true; break;
                        }
                    }
                    if (!shared) horizon.push_back(e);
                }

                // 创建新面
                size_t start_f = faces.size();
                for (const auto& e : horizon) {
                    add_face(e.first, e.second, best_p);
                    face_stack.push_back(faces.size() - 1);
                }

                // 重新分配点
                for (uint32_t pid : pool) {
                    if (pid == best_p) continue;
                    for (size_t i = start_f; i < faces.size(); ++i) {
                        if (dot(sub(pts[pid], pts[faces[i].v[0]]), faces[i].normal) > tolerance) {
                            faces[i].outside_set.push_back(pid); break;
                        }
                    }
                }
            }

            // 4. 组装输出
            utils::FlatMap<uint32_t, uint32_t> v_map;
            for (const auto& f : faces) {
                if (f.dead) continue;
                for (int k = 0; k < 3; ++k) {
                    if (v_map.find(f.v[k]) == v_map.end()) {
                        v_map.insert(f.v[k], static_cast<uint32_t>(mesh.vertices.size()));
                        mesh.vertices.push_back({pts[f.v[k]], RGBA::White()});
                    }
                }
                mesh.AddTriangle(v_map.find(f.v[0])->second, v_map.find(f.v[1])->second, v_map.find(f.v[2])->second);
            }
            return mesh;
        }

    private:
        /**
         * @brief 无损计算点云簇相对于其凸包的最大凹度
         * 算法原理：对于凸包的每一个面片，利用 LinearGrid 寻找其正向半空间中距离最远的点。
         */
        T Internal_CalculateMaxConcavity(const std::vector<Point3D<T>>& pts,
                                         const Mesh3D<T>& hull,
                                         const LinearGrid& lg) const
        {
            T global_max_dist = 0;
            const T eps = std::numeric_limits<T>::epsilon();

            // 1. 遍历凸包的所有三角形面片
            for (size_t i = 0; i < hull.indices.size(); i += 3) {
                const auto& v0 = hull.vertices[hull.indices[i]].position;
                const auto& v1 = hull.vertices[hull.indices[i+1]].position;
                const auto& v2 = hull.vertices[hull.indices[i+2]].position;

                // 2. 计算该面片的精确单位法线
                Point3D<T> edge1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                Point3D<T> edge2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                Point3D<T> normal = {
                    edge1.y * edge2.z - edge1.z * edge2.y,
                    edge1.z * edge2.x - edge1.x * edge2.z,
                    edge1.x * edge2.y - edge1.y * edge2.x
                };
                T n_len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (n_len < eps) continue; // 跳过退化面
                normal.x /= n_len; normal.y /= n_len; normal.z /= n_len;

                // 3. 使用格栅剪枝寻找该面片外侧最远的点
                T local_max_dist = 0;
                Internal_FindFurthestPoint(pts, lg, v0, normal, local_max_dist);

                if (local_max_dist > global_max_dist) {
                    global_max_dist = local_max_dist;
                }
            }

            return global_max_dist;
        }
	public:
	        /**
	         * @brief 顶级近似凸分解接口
	         * @param max_concavity_ratio 凹度比例阈值 (0.01~0.1)，基于物体跨度归一化
	         * @param max_parts 最大允许分割的凸块数量
	         */
	        void ComputeACDMesh3D(T max_concavity_ratio = 0.05, size_t max_parts = 16)
	        {
	            // 清空旧数据
	            result_meshes_3d.clear();
	            if (result_points_3d.size() < 4) return;
	            // 获取整体包围盒跨度，用于归一化阈值
	            auto global_aabb = Internal_GetPointsAABB(result_points_3d);
	            T span = std::max({global_aabb.x_max - global_aabb.x_min,
	                               global_aabb.y_max - global_aabb.y_min,
	                               global_aabb.z_max - global_aabb.z_min});
	            T abs_threshold = max_concavity_ratio * span;
	            // 待处理点云簇栈 (深度优先搜索)
	            std::vector<std::vector<Point3D<T>>> cluster_stack;
	            cluster_stack.push_back(result_points_3d);
	            while (!cluster_stack.empty()) {
	                std::vector<Point3D<T>> current_pts = std::move(cluster_stack.back());
	                cluster_stack.pop_back();
	                // 1. 为当前点集构建随机增量凸包 (替换原 QuickHull)
	                Mesh3D<T> hull = Internal_BuildIncrementalHull3D(current_pts);
	                if (hull.indices.empty()) continue;
	                // 2. 检查停止条件
	                if (result_meshes_3d.size() + cluster_stack.size() >= max_parts) {
	                    result_meshes_3d.push_back(std::move(hull));
	                    continue;
	                }
	                // 3. 构建加速格栅计算真实凹度
	                LinearGrid lg = Internal_BuildLinearGrid(current_pts);
	                T current_concavity = Internal_CalculateMaxConcavity(current_pts, hull, lg);
	                // 4. 决策：如果足够凸则保存，否则继续切割
	                if (current_concavity <= abs_threshold) {
	                    result_meshes_3d.push_back(std::move(hull));
	                }
	                else {
	                    // 5. 随机化 PCA 寻找最佳切割方向
	                    Point3D<T> center, normal;
	                    normal = Internal_ComputeRandomizedPCA(current_pts, center);
	                    // 6. 格栅加速分割 (无信息损失)
	                    std::vector<Point3D<T>> left, right;
	                    Internal_SplitCluster(current_pts, lg, center, normal, left, right);
	                    // 7. 将子簇压入栈，继续递归
	                    if (right.size() >= 4) cluster_stack.push_back(std::move(right));
	                    if (left.size() >= 4) cluster_stack.push_back(std::move(left));
	                    // 如果分不开了（例如所有点共面），强制终止防止死循环
	                    if (left.empty() || right.empty()) {
	                        result_meshes_3d.push_back(std::move(hull));
	                        if (!left.empty()) cluster_stack.pop_back();
	                    }
	                }
	            }
	        }

	    private:
	        // ==========================================================
	        // 随机增量凸包算法 (基于半边结构 + 面级 BVH 加速)
	        // ==========================================================
	        /**
	         * @brief 计算点面距离与可见性
	         * @return 距离 (正值为可见，负值为不可见)
	         */
        T Internal_PointFaceDistance(const Point3D<T>& p, const TriMeshHalfEdge3D<T>& mesh, uint32_t face_idx) const {
            auto verts = mesh.GetFaceVertices(face_idx);
            const auto& v0 = mesh.vertices[verts[0]].position;
            const auto& v1 = mesh.vertices[verts[1]].position;
            const auto& v2 = mesh.vertices[verts[2]].position;

            T nx = (v1.y - v0.y) * (v2.z - v0.z) - (v1.z - v0.z) * (v2.y - v0.y);
            T ny = (v1.z - v0.z) * (v2.x - v0.x) - (v1.x - v0.x) * (v2.z - v0.z);
            T nz = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);

            T len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > std::numeric_limits<T>::epsilon()) {
                nx /= len; ny /= len; nz /= len;
            }

            return (p.x - v0.x) * nx + (p.y - v0.y) * ny + (p.z - v0.z) * nz;
        }

	        // --- BVH 节点结构 ---
	        struct BVHNode {
	            AABB3D box;
	            uint32_t left = -1;   // 左子节点索引
	            uint32_t right = -1;  // 右子节点索引
	            std::vector<uint32_t> face_indices; // 仅叶子节点存储面索引
	        };
	        /**
	         * @brief 为当前网格中的活跃面构建 BVH
	         */
	        std::vector<BVHNode> Internal_BuildFacesBVH(const TriMeshHalfEdge3D<T>& mesh) const {
	            std::vector<BVHNode> nodes;
	            std::vector<uint32_t> active_faces;
	            for (uint32_t i = 0; i < mesh.faces.size(); ++i) {
	                if (!mesh.faces[i].is_deleted) {
	                    active_faces.push_back(i);
	                }
	            }
	            if (active_faces.empty()) return nodes;
	            // 递归构建 Lambda
	            std::function<uint32_t(std::vector<uint32_t>::iterator, std::vector<uint32_t>::iterator, int)> build =
	                [&](std::vector<uint32_t>::iterator begin, std::vector<uint32_t>::iterator end, int depth) -> uint32_t {
	                nodes.push_back(BVHNode());
	                uint32_t current_idx = nodes.size() - 1;
	                auto& node = nodes[current_idx];
	                // 计算 AABB
	                node.box = {std::numeric_limits<T>::max(), std::numeric_limits<T>::max(), std::numeric_limits<T>::max(),
	                            std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest()};
	                for (auto it = begin; it != end; ++it) {
	                    auto verts = mesh.GetFaceVertices(*it);
	                    for (auto vi : verts) {
	                        const auto& p = mesh.vertices[vi].position;
	                        node.box.x_min = std::min(node.box.x_min, p.x); node.box.x_max = std::max(node.box.x_max, p.x);
	                        node.box.y_min = std::min(node.box.y_min, p.y); node.box.y_max = std::max(node.box.y_max, p.y);
	                        node.box.z_min = std::min(node.box.z_min, p.z); node.box.z_max = std::max(node.box.z_max, p.z);
	                    }
	                }
	                size_t count = std::distance(begin, end);
	                // 叶子节点条件
	                if (count <= 4 || depth > 20) {
	                    node.face_indices.assign(begin, end);
	                    return current_idx;
	                }
	                // 沿最长轴排序
	                T span_x = node.box.x_max - node.box.x_min;
	                T span_y = node.box.y_max - node.box.y_min;
	                T span_z = node.box.z_max - node.box.z_min;
	                int axis = (span_x > span_y) ? ((span_x > span_z) ? 0 : 2) : ((span_y > span_z) ? 1 : 2);
	                std::sort(begin, end, [&](uint32_t a, uint32_t b) {
	                    auto va = mesh.GetFaceVertices(a);
	                    auto vb = mesh.GetFaceVertices(b);
	                    T ca = (mesh.vertices[va[0]].position.x + mesh.vertices[va[1]].position.x + mesh.vertices[va[2]].position.x) / 3.0;
	                    T cb = (mesh.vertices[vb[0]].position.x + mesh.vertices[vb[1]].position.x + mesh.vertices[vb[2]].position.x) / 3.0;
	                    if (axis == 1) { ca = (mesh.vertices[va[0]].position.y + mesh.vertices[va[1]].position.y + mesh.vertices[va[2]].position.y) / 3.0; cb = (mesh.vertices[vb[0]].position.y + mesh.vertices[vb[1]].position.y + mesh.vertices[vb[2]].position.y) / 3.0; }
	                    if (axis == 2) { ca = (mesh.vertices[va[0]].position.z + mesh.vertices[va[1]].position.z + mesh.vertices[va[2]].position.z) / 3.0; cb = (mesh.vertices[vb[0]].position.z + mesh.vertices[vb[1]].position.z + mesh.vertices[vb[2]].position.z) / 3.0; }
	                    return ca < cb;
	                });
	                auto mid = begin + count / 2;
	                node.left = build(begin, mid, depth + 1);
	                node.right = build(mid, end, depth + 1);
	                return current_idx;
	            };
	            build(active_faces.begin(), active_faces.end(), 0);
	            return nodes;
	        }
	        /**
	         * @brief 通过 BVH 查找所有对于点 p 可见的面
	         */
	        void Internal_FindVisibleFaces(const Point3D<T>& p, const TriMeshHalfEdge3D<T>& mesh,
	                                       const std::vector<BVHNode>& bvh, uint32_t node_idx,
	                                       std::vector<uint32_t>& visible_faces) const {
	            const auto& node = bvh[node_idx];
	            // 快速排除：如果点在 AABB 内部，则一定可见或相交；如果在严格外部，需判断
	            // 凸包性质：如果点在面法线正向，则可见。如果点在 AABB 外部，但法线朝内，可能不可见。
	            // 这里简化：AABB 测试仅作粗筛。如果点不在扩展 AABB 内部，肯定不可见。
	            T eps = std::numeric_limits<T>::epsilon() * 10;
	            if (p.x < node.box.x_min - eps || p.x > node.box.x_max + eps ||
	                p.y < node.box.y_min - eps || p.y > node.box.y_max + eps ||
	                p.z < node.box.z_min - eps || p.z > node.box.z_max + eps) {
	                return;
	            }
	            if (!node.face_indices.empty()) {
	                for (uint32_t fi : node.face_indices) {
	                    if (!mesh.faces[fi].is_deleted && Internal_PointFaceDistance(p, mesh, fi) > 0) {
	                        visible_faces.push_back(fi);
	                    }
	                }
	            } else {
	                if (node.left != -1) Internal_FindVisibleFaces(p, mesh, bvh, node.left, visible_faces);
	                if (node.right != -1) Internal_FindVisibleFaces(p, mesh, bvh, node.right, visible_faces);
	            }
	        }
	        /**
	         * @brief 从可见面集合中提取地平线边界
	         */
	        std::vector<uint32_t> Internal_FindHorizon(const TriMeshHalfEdge3D<T>& mesh, const std::vector<uint32_t>& visible_faces) const {
	            std::vector<uint32_t> horizon_edges;
	            for (uint32_t fi : visible_faces) {
	                uint32_t he = mesh.faces[fi].edge;
	                for (int i = 0; i < 3; ++i) {
	                    uint32_t twin_he = mesh.edges[he].twin;
	                    if (twin_he != TriMeshHalfEdge3D<T>::INVALID_IDX) {
	                        uint32_t neighbor_face = mesh.edges[twin_he].face;
	                        // 如果邻接面不可见(或被删除)，则当前半边是地平线边
	                        bool neighbor_visible = false;
	                        for (uint32_t vf : visible_faces) {
	                            if (vf == neighbor_face) { neighbor_visible = true; break; }
	                        }
	                        if (!neighbor_visible) {
	                            horizon_edges.push_back(he);
	                        }
	                    } else {
	                        // 边界边
	                        horizon_edges.push_back(he);
	                    }
	                    he = mesh.edges[he].next;
	                }
	            }
	            return horizon_edges;
	        }
		    private:
	        // ==========================================================
	        // 随机增量凸包算法 (重写版：基于面邻接数组，无 BVH，绝对稳健)
	        // ==========================================================
	        struct IncFace {
	            std::array<uint32_t, 3> v;       // 顶点索引 (指向原始点集 pts)
	        	std::array<uint32_t, 3> adj;
	            Point3D<T> normal;
	            T dist;              // 平面距离原点的距离 (normal . v0)
	            bool is_deleted = false;
	        };
	    Mesh3D<T> Internal_BuildIncrementalHull3D(const std::vector<Point3D<T>>& pts) const
        {
            Mesh3D<T> result_mesh;
            if (pts.size() < 4) return result_mesh;

            // --- 步骤 0: 动态计算相对容差 (非常重要) ---
            T min_x = pts[0].x, max_x = pts[0].x, min_y = pts[0].y, max_y = pts[0].y, min_z = pts[0].z, max_z = pts[0].z;
            for (const auto& p : pts) {
                if (p.x < min_x) min_x = p.x; else if (p.x > max_x) max_x = p.x;
                if (p.y < min_y) min_y = p.y; else if (p.y > max_y) max_y = p.y;
                if (p.z < min_z) min_z = p.z; else if (p.z > max_z) max_z = p.z;
            }
            T span = std::max({max_x - min_x, max_y - min_y, max_z - min_z});

            // 设定物理容差：跨度的十万分之一。只要点到平面的距离小于这个值，
            // 算法就会认为该点 "共面" 或 "在内部"，避免在平面上长出微型四面体。
            const T tolerance = std::max(span * static_cast<T>(1e-5), std::numeric_limits<T>::epsilon() * 100);

            // --- 步骤 1: 寻找最极端的初始四面体 (使用精确距离) ---
            auto sub =[](const Point3D<T>& a, const Point3D<T>& b) { return Point3D<T>{a.x-b.x, a.y-b.y, a.z-b.z}; };
            auto dot =[](const Point3D<T>& a, const Point3D<T>& b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
            auto cross =[](const Point3D<T>& a, const Point3D<T>& b) {
                return Point3D<T>{a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
            };

            size_t p[4] = {0, 0, 0, 0};
            for (size_t i = 1; i < pts.size(); ++i) if (pts[i].x < pts[p[0]].x) p[0] = i;

            T max_d2 = -1;
            for (size_t i = 0; i < pts.size(); ++i) {
                T d2 = dot(sub(pts[i], pts[p[0]]), sub(pts[i], pts[p[0]]));
                if (d2 > max_d2) { max_d2 = d2; p[1] = i; }
            }

            T max_l2 = -1; Point3D<T> v01 = sub(pts[p[1]], pts[p[0]]);
            for (size_t i = 0; i < pts.size(); ++i) {
                Point3D<T> cr = cross(v01, sub(pts[i], pts[p[0]]));
                T d2 = dot(cr, cr); if (d2 > max_l2) { max_l2 = d2; p[2] = i; }
            }

            Point3D<T> n_base = cross(v01, sub(pts[p[2]], pts[p[0]]));
            T len_n = std::sqrt(dot(n_base, n_base));
            if (len_n > std::numeric_limits<T>::epsilon()) { n_base.x /= len_n; n_base.y /= len_n; n_base.z /= len_n; }

            T max_pd = -1;
            for (size_t i = 0; i < pts.size(); ++i) {
                T d = std::abs(dot(sub(pts[i], pts[p[0]]), n_base));
                if (d > max_pd) { max_pd = d; p[3] = i; }
            }

            // 如果整个点集扁平化程度低于容差，返回空
            if (max_pd <= tolerance) return result_mesh;

            // --- 步骤 2: 构建初始半边网格 ---
            TriMeshHalfEdge3D<T> he_mesh;
            Point3D<T> centroid = {
                (pts[p[0]].x + pts[p[1]].x + pts[p[2]].x + pts[p[3]].x) * 0.25,
                (pts[p[0]].y + pts[p[1]].y + pts[p[2]].y + pts[p[3]].y) * 0.25,
                (pts[p[0]].z + pts[p[1]].z + pts[p[2]].z + pts[p[3]].z) * 0.25
            };
            auto v0 = he_mesh.AddVertex(pts[p[0]]);
            auto v1 = he_mesh.AddVertex(pts[p[1]]);
            auto v2 = he_mesh.AddVertex(pts[p[2]]);
            auto v3 = he_mesh.AddVertex(pts[p[3]]);

            std::vector<uint32_t> initial_faces;
            auto check_and_add_face = [&](uint32_t a, uint32_t b, uint32_t c) {
                Point3D<T> e1 = sub(he_mesh.vertices[b].position, he_mesh.vertices[a].position);
                Point3D<T> e2 = sub(he_mesh.vertices[c].position, he_mesh.vertices[a].position);
                Point3D<T> n = cross(e1, e2);
                Point3D<T> dir = sub(centroid, he_mesh.vertices[a].position);
                if (dot(n, dir) > 0) std::swap(b, c);
                initial_faces.push_back(he_mesh.AddFace(a, b, c));
            };

            check_and_add_face(v0, v1, v2);
            check_and_add_face(v0, v1, v3);
            check_and_add_face(v0, v2, v3);
            check_and_add_face(v1, v2, v3);

            // --- 步骤 3: 建立冲突映射图 (仅记录真实在容差外的点) ---
            std::unordered_map<uint32_t, std::vector<size_t>> face_to_points;
            for (size_t i = 0; i < pts.size(); ++i) {
                if (i == p[0] || i == p[1] || i == p[2] || i == p[3]) continue;
                for (uint32_t fi : initial_faces) {
                    // 只关心明确位于外侧容差以上的点
                    if (Internal_PointFaceDistance(pts[i], he_mesh, fi) > tolerance) {
                        face_to_points[fi].push_back(i);
                        break;
                    }
                }
            }

            // --- 步骤 4: QuickHull 主循环 ---
            while (!face_to_points.empty()) {
                auto it = face_to_points.begin();
                uint32_t start_face = it->first;

                if (it->second.empty() || he_mesh.faces[start_face].is_deleted) {
                    face_to_points.erase(it);
                    continue;
                }

                // **核心修改：找出距离当前平面最远的点 (QuickHull精髓)**
                T max_dist = -1;
                size_t best_idx_in_vector = 0;
                for (size_t i = 0; i < it->second.size(); ++i) {
                    T dist = Internal_PointFaceDistance(pts[it->second[i]], he_mesh, start_face);
                    if (dist > max_dist) {
                        max_dist = dist;
                        best_idx_in_vector = i;
                    }
                }

                // 如果最远的点依然没有超出容差，则该面处理完毕
                if (max_dist <= tolerance) {
                    face_to_points.erase(it);
                    continue;
                }

                // 取出该点，并通过交换队尾高效删除
                size_t pt_idx = it->second[best_idx_in_vector];
                std::swap(it->second[best_idx_in_vector], it->second.back());
                it->second.pop_back();

                const auto& p_new = pts[pt_idx];

                // 4.1 拓扑 BFS：通过半边网络极速寻找连片的可见面
                std::vector<uint32_t> visible_faces;
                std::unordered_set<uint32_t> visible_set;
                std::vector<uint32_t> queue = {start_face};
                visible_set.insert(start_face);

                while (!queue.empty()) {
                    uint32_t f = queue.back();
                    queue.pop_back();
                    visible_faces.push_back(f);

                    uint32_t he = he_mesh.faces[f].edge;
                    for (int i = 0; i < 3; ++i) {
                        uint32_t twin = he_mesh.edges[he].twin;
                        if (twin != TriMeshHalfEdge3D<T>::INVALID_IDX) {
                            uint32_t nf = he_mesh.edges[twin].face;
                            if (visible_set.find(nf) == visible_set.end() && !he_mesh.faces[nf].is_deleted) {
                                // 容差设为 0 以保证视野覆盖的连贯性，防止边界锯齿
                                if (Internal_PointFaceDistance(p_new, he_mesh, nf) > 0) {
                                    visible_set.insert(nf);
                                    queue.push_back(nf);
                                }
                            }
                        }
                        he = he_mesh.edges[he].next;
                    }
                }

                // 4.2 提取地平线边框
                std::vector<std::pair<uint32_t, uint32_t>> horizon_verts;
                for (uint32_t f : visible_faces) {
                    uint32_t he = he_mesh.faces[f].edge;
                    for (int i = 0; i < 3; ++i) {
                        uint32_t twin = he_mesh.edges[he].twin;
                        bool neighbor_visible = false;
                        if (twin != TriMeshHalfEdge3D<T>::INVALID_IDX) {
                            uint32_t nf = he_mesh.edges[twin].face;
                            if (visible_set.find(nf) != visible_set.end()) neighbor_visible = true;
                        }

                        if (!neighbor_visible) {
                            uint32_t origin_v = he_mesh.edges[he].origin_vertex;
                            uint32_t end_v = he_mesh.edges[he_mesh.edges[he].next].origin_vertex;
                            horizon_verts.push_back({origin_v, end_v});
                        }
                        he = he_mesh.edges[he].next;
                    }
                }

                // 4.3 收集所有将被删除面的关联点
                std::vector<size_t> orphan_points;
                for (uint32_t f : visible_faces) {
                    auto face_it = face_to_points.find(f);
                    if (face_it != face_to_points.end()) {
                        orphan_points.insert(orphan_points.end(), face_it->second.begin(), face_it->second.end());
                        face_to_points.erase(face_it);
                    }
                    he_mesh.DeleteFace(f);
                }

                // 4.4 插入新顶点，与地平线缝合
                uint32_t new_v_idx = he_mesh.AddVertex(p_new);
                std::vector<uint32_t> new_faces;

                for (const auto& edge : horizon_verts) {
                    uint32_t new_f = he_mesh.AddFace(new_v_idx, edge.first, edge.second);
                    if(new_f != TriMeshHalfEdge3D<T>::INVALID_IDX) {
                        new_faces.push_back(new_f);
                    }
                }

                // 4.5 孤儿点重组
                for (size_t orphan : orphan_points) {
                    for (uint32_t nf : new_faces) {
                        // 依然使用精确容差决定是否接纳该点
                        if (Internal_PointFaceDistance(pts[orphan], he_mesh, nf) > tolerance) {
                            face_to_points[nf].push_back(orphan);
                            break;
                        }
                    }
                }
            }

            // --- 步骤 5: 转换为 Mesh3D 输出 ---
            utils::FlatMap<uint32_t, uint32_t> v_map;
            for (uint32_t fi = 0; fi < he_mesh.faces.size(); ++fi) {
                if (he_mesh.faces[fi].is_deleted) continue;
                auto verts = he_mesh.GetFaceVertices(fi);
                uint32_t mesh_vi[3];
                for (int k = 0; k < 3; ++k) {
                    uint32_t vi = verts[k];
                    if (v_map.find(vi) == v_map.end()) {
                        v_map.insert(vi, static_cast<uint32_t>(result_mesh.vertices.size()));
                        result_mesh.vertices.push_back({he_mesh.vertices[vi].position, RGBA::White()});
                    }
                    mesh_vi[k] = v_map.find(vi)->second;
                }
                result_mesh.AddTriangle(mesh_vi[0], mesh_vi[1], mesh_vi[2]);
            }

            return result_mesh;
        }





    };

    template <typename T>
    struct Graph
    {
        using value_type = T;
        utils::BlockDeque<Node<T>, 256> node_pool;
        std::string name;
        utils::FlatMap<uint64_t, size_t> id_map;

        Node<T>* GetNode(uint64_t id)
        {
            auto it = id_map.find(id);
            if (it == id_map.end()) throw std::out_of_range("Invalid node id");
            return &node_pool[it->second];
        }

        // 在 Graph 结构体内
        Selection<T> Select()
        {
            std::vector<uint64_t> all_ids;
            all_ids.reserve(node_pool.size());
            for (size_t i = 0; i < node_pool.size(); ++i) all_ids.push_back(node_pool[i].id);
            return {*this, std::move(all_ids)};
        }

        Selection<T> Select(const std::vector<uint64_t>& scope)
        {
            return {*this, scope};
        }

        void ClearMaskPool(NodeMask m)
        {
            // 线性遍历整个 BlockDeque 内存池
            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                node_pool[i].clear_mask(m);
            }
        }

        std::vector<std::vector<uint64_t>> CompileLeveledOrder()
        {
            std::vector<std::vector<uint64_t>> levels;
            if (node_pool.empty()) return levels;


            utils::FlatMap<uint64_t, size_t> in_degrees;
            std::vector<uint64_t> current_level_nodes;

            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                auto& node = node_pool[i];
                size_t d = node.parents.size();
                in_degrees[node.id] = d;
                if (d == 0)
                {
                    current_level_nodes.emplace_back(node.id);
                }
            }


            size_t processed_count = 0;
            while (!current_level_nodes.empty())
            {
                levels.emplace_back(current_level_nodes);
                processed_count += current_level_nodes.size();

                std::vector<uint64_t> next_level_nodes;
                for (uint64_t curr_id : current_level_nodes)
                {
                    Node<T>* curr_node = GetNode(curr_id);
                    for (uint64_t child_id : curr_node->children)
                    {
                        if (--in_degrees[child_id] == 0)
                        {
                            next_level_nodes.emplace_back(child_id);
                        }
                    }
                }
                current_level_nodes = std::move(next_level_nodes);
            }


            if (processed_count != node_pool.size())
            {
                throw std::runtime_error("Dependency cycle detected in Graph!");
            }

            return levels;
        }


        void PropagateMaskDownstream(uint64_t start_node_id, NodeMask m)
        {
            // 使用 vector 模拟栈 (Stack) 进行深度优先遍历
            std::vector<uint64_t> stack;
            stack.push_back(start_node_id);

            while (!stack.empty())
            {
                uint64_t current_id = stack.back();
                stack.pop_back();

                Node<T>* node = GetNode(current_id);

                // 统一判定逻辑：
                // 如果当前节点已经设置了该掩码，说明该分支已经传播过，直接跳过。
                // 这既保证了效率（O(V+E)），也防止了在复杂 DAG 中的重复计算。
                if (node->has_mask(m)) continue;

                // 设置掩码
                node->set_mask(m);

                // 将所有子节点压入栈中以待后续处理
                // 注意：由于是栈，子节点处理顺序与 children 列表相反，但对结果无影响
                for (uint64_t child_id : node->children)
                {
                    stack.push_back(child_id);
                }
            }
        }

        bool DeleteNode(uint64_t id)
        {
            auto it = id_map.find(id);
            if (it == id_map.end()) return false;

            size_t index_to_delete = it->second;
            uint64_t last_node_id = node_pool.back().id;


            if (index_to_delete != node_pool.size() - 1)
            {
                std::swap(node_pool[index_to_delete], node_pool.back());

                id_map[last_node_id] = index_to_delete;
            }


            node_pool.pop_back();
            id_map.erase(id);
            return true;
        }


        void Compute(uint64_t num_threads = 1)
        {
            // 1. 脏标记向下传播 (必须先于拓扑执行)
            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                if (node_pool[i].has_mask(NodeMask::DIRTY))
                {
                    PropagateMaskDownstream(node_pool[i].id, NodeMask::DIRTY);
                }
            }

            // 2. 计算线程数逻辑
            uint32_t target_threads = ResolveThreadCount(num_threads);

            // 3. 获取分层拓扑序列
            auto levels = CompileLeveledOrder();

            // 4. 按层级执行
            for (auto& level_nodes : levels)
            {
                // 只有当目标线程数 > 1 且 当前层级节点数 > 1 时才启动并行
                if (target_threads > 1 && level_nodes.size() > 1)
                {
                    utils::parallel_for(size_t(0), level_nodes.size(), [&](size_t start, size_t end)
                    {
                        for (size_t i = start; i < end; ++i)
                        {
                            ExecuteNodeUpdate(level_nodes[i], false);
                        }
                    }, target_threads);
                }
                else
                {
                    for (uint64_t id : level_nodes)
                    {
                        ExecuteNodeUpdate(id, false);
                    }
                }
            }
        }

        /**
         * @brief 强制全量计算 (忽略 DIRTY 标记)
         * @param num_threads 线程控制逻辑同 Compute
         */
        void ComputeForce(uint64_t num_threads = 1)
        {
            uint32_t target_threads = ResolveThreadCount(num_threads);
            auto levels = CompileLeveledOrder();

            for (auto& level_nodes : levels)
            {
                if (target_threads > 1 && level_nodes.size() > 1)
                {
                    utils::parallel_for(size_t(0), level_nodes.size(), [&](size_t start, size_t end)
                    {
                        for (size_t i = start; i < end; ++i)
                        {
                            ExecuteNodeUpdate(level_nodes[i], true);
                        }
                    }, target_threads);
                }
                else
                {
                    for (uint64_t id : level_nodes)
                    {
                        ExecuteNodeUpdate(id, true);
                    }
                }
            }
        }

        [[nodiscard]] uint32_t ResolveThreadCount(uint64_t req) const
        {
            uint32_t hardware = std::thread::hardware_concurrency();
            if (hardware == 0) hardware = 1; // 容错处理

            if (req == 0 || req > static_cast<uint64_t>(hardware))
            {
                return hardware;
            }
            return static_cast<uint32_t>(req);
        }

        /**
         * @brief 内部执行单个节点的求解和打点
         * @param id 节点ID
         * @param force 是否强制执行
         */
        void ExecuteNodeUpdate(uint64_t id, bool force)
        {
            Node<T>* node = GetNode(id);

            // 如果不是强制模式且节点不是脏的，则跳过
            if (!force && !node->has_mask(NodeMask::DIRTY)) return;
            node->result_points_2d.clear();
            node->result_points_3d.clear();

            // 1. 执行数学求解 (更新 data)
            if (node->solver)
            {
                node->solver(*this, *node);
            }


            // 2. 执行打点渲染 (更新 result_points)
            if (node->plotter)
            {
                node->plotter(*this, *node);
            }


            // 3. 计算完成，清除脏标记
            node->clear_mask(NodeMask::DIRTY);
        }

        struct
        {
            T x_min;
            T y_min;
            T x_max;
            T y_max;
        } world_space_2d;

        struct
        {
            T x_min;
            T y_min;
            T z_min;
            T x_max;
            T y_max;
            T z_max;
        } world_space_3d;

        struct
        {
            T x, y;
        } resolution_2d;

        struct
        {
            T x, y, z;
        } resolution_3d;


        uint64_t CreateFreePoint_2D(T x, T y, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_FREE;
            node.data.point_2d.x = x;
            node.data.point_2d.y = y;
            id_map[node.id] = node_pool.size() - 1;
            node.solver = nullptr;
            node.plotter = PlotPoint_2D;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return node.id;
        }

        uint64_t CreateFreePoint_3D(T x, T y, T z, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_FREE;
            node.data.point_3d.x = x;
            node.data.point_3d.y = y;
            node.data.point_3d.z = z;
            id_map[node.id] = node_pool.size() - 1;
            node.solver = nullptr;
            node.plotter =  PlotPoint_3D;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return node.id;
        }

        uint64_t CreateStraightLine_2D(uint64_t start_point_id, uint64_t end_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_line_id = node.id;
            id_map[new_line_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_line_id);
            end_node->children.emplace_back(new_line_id);


            node.solver = SolveStraightLine_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;


            return new_line_id;
        }

        uint64_t CreateStraightLine_3D(uint64_t start_point_id, uint64_t end_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_STRAIGHT;


            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_line_id = node.id;
            id_map[new_line_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_line_id);
            end_node->children.emplace_back(new_line_id);


            node.solver = SolveStraightLine_3D<T>;
            node.plotter = PlotStraightLine_3D<T>;


            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_line_id;
        }

        uint64_t CreateSegment_2D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_SEGMENT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveSegment_2D<T>;
            node.plotter = PlotSegment_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }

        uint64_t CreateSegment_3D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_SEGMENT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveSegment_3D<T>;
            node.plotter = PlotSegment_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_seg_id;
        }


        uint64_t CreateRay_2D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_RAY;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveRay_2D<T>;
            node.plotter = PlotRay_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }

        uint64_t CreateRay_3D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_RAY;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveRay_3D<T>;
            node.plotter = PlotRay_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }


        uint64_t CreateCircle_2D(uint64_t center_point_id, T radius, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_point_id);


            if (!(is_2d(center_node->type) && is_point(center_node->type)))
            {
                throw std::invalid_argument("Center argument must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(center_point_id);


            node.data.circle_2d.r = radius;

            uint64_t new_circle_id = node.id;
            id_map[new_circle_id] = node_pool.size() - 1;


            center_node->children.emplace_back(new_circle_id);


            node.solver = SolveCircle_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_circle_id;
        }


        uint64_t CreateCircle_2D(uint64_t center_id, uint64_t point_on_circle_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(center_id);
            Node<T>* n2 = GetNode(point_on_circle_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) || !(is_2d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(center_id);
            node.parents.emplace_back(point_on_circle_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveCircle_CP_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateCircle_2D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) ||
                !(is_2d(n2->type) && is_point(n2->type)) ||
                !(is_2d(n3->type) && is_point(n3->type)))
            {
                throw std::invalid_argument("All three arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            node.solver = SolveCircle_3P_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreatePlane_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            auto is_valid_point = [](Node<T>* n)
            {
                return is_3d(n->type) && is_point(n->type);
            };

            if (!is_valid_point(n1) || !is_valid_point(n2) || !is_valid_point(n3))
            {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_3D;


            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_plane_id = node.id;
            id_map[new_plane_id] = node_pool.size() - 1;


            n1->children.emplace_back(new_plane_id);
            n2->children.emplace_back(new_plane_id);
            n3->children.emplace_back(new_plane_id);


            node.solver = SolvePlane_3Points_3D<T>;
            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_plane_id;
        }

        /**
         * @brief 创建 3D 三角形面
         * @param p1_id 顶点1 (原点)
         * @param p2_id 顶点2 (决定 U 向量)
         * @param p3_id 顶点3 (决定 V 向量)
         */
        uint64_t CreateTriangle_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            auto is_valid_point = [](Node<T>* n) {
                return is_3d(n->type) && is_point(n->type);
            };

            if (!is_valid_point(n1) || !is_valid_point(n2) || !is_valid_point(n3)) {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            // 使用你定义的特定类型 PLANE_3D_TRAINGLE
            node.type = NodeType::PLANE_3D_TRAINGLE;
            node.name = node_name;

            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            // 绑定三角形专用求解器与打点器
            node.solver = SolveTriangle_3Points_3D<T>;
            node.plotter = PlotTriangle_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateArc_2D(uint64_t p_start_id, uint64_t p_on_id, uint64_t p_end_id,
                              std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p_start_id);
            Node<T>* n2 = GetNode(p_on_id);
            Node<T>* n3 = GetNode(p_end_id);

            if (!is_2d(n1->type) || !is_2d(n2->type) || !is_2d(n3->type))
            {
                throw std::invalid_argument("All arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::ARC_2D;
            node.parents.emplace_back(p_start_id);
            node.parents.emplace_back(p_on_id);
            node.parents.emplace_back(p_end_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            node.solver = SolveArc_3P_2D<T>;
            node.plotter = PlotArc_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateParallelLine_2D(uint64_t ref_line_id, T distance, std::string node_name = "Unnamed")
        {
            Node<T>* ref_node = GetNode(ref_line_id);


            if (!is_2d(ref_node->type) || !is_linear(ref_node->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;
            node.parents.emplace_back(ref_line_id);


            node.data.parallel_line_2d.distance = distance;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            ref_node->children.emplace_back(new_id);

            node.solver = SolveParallelLine_Dist_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateParallelLine_2D(uint64_t ref_line_id, uint64_t target_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* ref_line = GetNode(ref_line_id);
            Node<T>* target_pt = GetNode(target_point_id);


            if (!is_2d(ref_line->type) || !is_linear(ref_line->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }
            if (!is_2d(target_pt->type) || !is_point(target_pt->type))
            {
                throw std::invalid_argument("Target must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;


            node.parents.emplace_back(ref_line_id);
            node.parents.emplace_back(target_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            ref_line->children.emplace_back(new_id);
            target_pt->children.emplace_back(new_id);


            node.solver = SolveParallelLine_Point_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateVerticalLine_2D(uint64_t ref_line_id, uint64_t target_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* ref_line = GetNode(ref_line_id);
            Node<T>* target_pt = GetNode(target_point_id);


            if (!is_2d(ref_line->type) || !is_linear(ref_line->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }
            if (!is_2d(target_pt->type) || !is_point(target_pt->type))
            {
                throw std::invalid_argument("Target must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;

            node.parents.emplace_back(ref_line_id);
            node.parents.emplace_back(target_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            ref_line->children.emplace_back(new_id);
            target_pt->children.emplace_back(new_id);


            node.solver = SolveVerticalLine_Point_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_2D(uint64_t p1_id, uint64_t p2_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) ||
                !(is_2d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_MID;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveMidPoint_2P_2D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_2D(uint64_t segment_id, std::string node_name = "Unnamed")
        {
            Node<T>* seg_node = GetNode(segment_id);


            if (seg_node->type != NodeType::LINE_2D_SEGMENT)
            {
                throw std::invalid_argument("Argument must be a 2D Segment object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_MID;
            node.parents.emplace_back(segment_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            seg_node->children.emplace_back(new_id);

            node.solver = SolveMidPoint_Segment_2D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_3D(uint64_t p1_id, uint64_t p2_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);

            if (!(is_3d(n1->type) && is_point(n1->type)) ||
                !(is_3d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_MID;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveMidPoint_2P_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_3D(uint64_t segment_id, std::string node_name = "Unnamed")
        {
            Node<T>* seg_node = GetNode(segment_id);

            if (seg_node->type != NodeType::LINE_3D_SEGMENT)
            {
                throw std::invalid_argument("Argument must be a 3D Segment object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_MID;
            node.parents.emplace_back(segment_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            seg_node->children.emplace_back(new_id);

            node.solver = SolveMidPoint_Segment_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateSnappedPoint_2D(uint64_t target_id, T gx, T gy, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_SNAP;
            node.parents.emplace_back(target_id);

            node.data.snap_2d.guess_x = gx;
            node.data.snap_2d.guess_y = gy;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            GetNode(target_id)->children.emplace_back(new_id);

            node.solver = SolveSnappedPoint_2D<T>;
            node.plotter = PlotPoint_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateSnappedPoint_3D(uint64_t target_id, T gx, T gy, T gz, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_SNAP;
            node.parents.emplace_back(target_id);

            node.data.snap_3d.guess_x = gx;
            node.data.snap_3d.guess_y = gy;
            node.data.snap_3d.guess_z = gz;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            GetNode(target_id)->children.emplace_back(new_id);

            node.solver = SolveSnappedPoint_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        std::vector<uint64_t> GroupCreatePolyGen_2D(uint64_t center_id, T side_length, size_t num_sides,
                                                    std::string node_name = "Unnamed")
        {
            std::vector<uint64_t> created_ids;
            if (num_sides < 3) return created_ids;


            Node<T>* center_node = GetNode(center_id);
            T cx = center_node->data.point_2d.x;
            T cy = center_node->data.point_2d.y;


            const T PI = static_cast<T>(3.14159265358979323846);
            T circumradius = side_length / (static_cast<T>(2.0) * std::sin(PI / static_cast<T>(num_sides)));


            std::vector<uint64_t> vertex_ids;
            vertex_ids.reserve(num_sides);
            created_ids.reserve(num_sides * 2);


            for (size_t i = 0; i < num_sides; ++i)
            {
                T theta = (static_cast<T>(2.0) * PI * static_cast<T>(i)) / static_cast<T>(num_sides);
                T px = cx + circumradius * std::cos(theta);
                T py = cy + circumradius * std::sin(theta);


                uint64_t pid = CreateFreePoint_2D(px, py, node_name);
                vertex_ids.emplace_back(pid);
                created_ids.emplace_back(pid);
            }


            for (size_t i = 0; i < num_sides; ++i)
            {
                uint64_t p_start = vertex_ids[i];
                uint64_t p_end = vertex_ids[(i + 1) % num_sides];


                uint64_t sid = CreateSegment_2D(p_start, p_end, node_name);
                created_ids.emplace_back(sid);
            }

            return created_ids;
        }


        uint64_t CreatePolyGen_2D(uint64_t center_id, T side_length, uint32_t num_sides,
                                  std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!is_2d(center_node->type) || !is_point(center_node->type))
            {
                throw std::invalid_argument("Center must be a 2D Point.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::POLY_2D_GENERATIVE;
            node.name = node_name;
            node.parents.emplace_back(center_id);


            node.data.poly_gen_2d.side_len = side_length;
            node.data.poly_gen_2d.n = num_sides;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            center_node->children.emplace_back(new_id);


            node.solver = SolvePolyGen_2D<T>;
            node.plotter = PlotPolyGen_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateTangent_Diagram_2D(uint64_t curve_id, uint64_t pivot_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* curve = GetNode(curve_id);
            Node<T>* pivot = GetNode(pivot_point_id);

            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_TANGENT;
            node.name = node_name;


            node.parents.emplace_back(curve_id);
            node.parents.emplace_back(pivot_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            curve->children.emplace_back(new_id);
            pivot->children.emplace_back(new_id);


            node.solver = SolveTangent_Diagram_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }


        uint64_t CreatePlaneInfinity_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id,
                                        std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            if (!(is_3d(n1->type) && is_point(n1->type)) ||
                !(is_3d(n2->type) && is_point(n2->type)) ||
                !(is_3d(n3->type) && is_point(n3->type)))
            {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_3D;
            node.name = node_name;

            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);


            node.solver = SolvePlaneInfinity_3P_3D<T>;

            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }


        uint64_t CreateTangentPlane_3D(uint64_t target_obj_id, uint64_t pivot_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* obj = GetNode(target_obj_id);
            Node<T>* pivot = GetNode(pivot_point_id);

            if (!is_3d(obj->type) || !is_3d(pivot->type) || !is_point(pivot->type))
            {
                throw std::invalid_argument("Requires a 3D object and a 3D pivot point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_TANGENT_3D;
            node.name = node_name;

            node.parents.emplace_back(target_obj_id);
            node.parents.emplace_back(pivot_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            obj->children.emplace_back(new_id);
            pivot->children.emplace_back(new_id);


            node.solver = SolveTangentPlane_Diagram_3D<T>;

            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        std::vector<uint64_t> GroupCreatePlatonicSolid_3D(
            uint64_t center_id,
            T radius,
            int type,
            std::string node_name = "Unnamed")
        {
            std::vector<uint64_t> result_ids;


            Node<T>* center_node = GetNode(center_id);
            const T cx = center_node->data.point_3d.x;
            const T cy = center_node->data.point_3d.y;
            const T cz = center_node->data.point_3d.z;


            auto raw = StaticData::PlatonicLibrary<T>::Get(type);
            if (raw.vertices.empty()) return result_ids;


            std::vector<uint64_t> v_ids;
            v_ids.reserve(raw.vertices.size());
            const T eps = std::numeric_limits<T>::epsilon();

            for (const auto& v : raw.vertices)
            {
                T mag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                T scale = (mag > eps) ? (radius / mag) : radius;

                T tx = cx + v[0] * scale;
                T ty = cy + v[1] * scale;
                T tz = cz + v[2] * scale;


                uint64_t pid = CreateFreePoint_3D(tx, ty, tz, node_name);
                v_ids.emplace_back(pid);
                result_ids.emplace_back(pid);
            }


            std::set<std::pair<size_t, size_t>> edge_registry;

            for (const auto& face : raw.faces)
            {
                for (size_t i = 0; i < face.size(); ++i)
                {
                    size_t idx1 = face[i];
                    size_t idx2 = face[(i + 1) % face.size()];


                    size_t low = std::min(idx1, idx2);
                    size_t high = std::max(idx1, idx2);


                    if (edge_registry.insert({low, high}).second)
                    {
                        uint64_t sid = CreateSegment_3D(v_ids[low], v_ids[high], node_name);
                        result_ids.emplace_back(sid);
                    }
                }
            }


            // ==========================================
            // 5. 第三阶段：创建面 (Planes)
            // ==========================================
            for (const auto& face : raw.faces)
            {
                if (face.size() < 3) continue;

                // 方案 A：如果是三角形（正四、八、二十面体），直接创建
                if (face.size() == 3)
                {
                    uint64_t fid = CreateTriangle_3D(v_ids[face[0]], v_ids[face[1]], v_ids[face[2]], node_name);
                    result_ids.push_back(fid);
                }
                // 方案 B：如果是四边形（正六面体/立方体），保持原有的平行四边形面，因为它刚好能完美覆盖
                else if (face.size() == 4)
                {
                    uint64_t fid = CreatePlane_3D(v_ids[face[0]], v_ids[face[1]], v_ids[face.back()], node_name);
                    result_ids.push_back(fid);
                }
                // 方案 C：如果是五边形（正十二面体）或更多边形，必须切分成三角形扇，防止平面溢出！
                else
                {
                    // 一个 N 边形可以切分成 N - 2 个三角形
                    // 以 face[0] 为公共顶点，依次连接后续顶点
                    for (size_t k = 1; k < face.size() - 1; ++k)
                    {
                        uint64_t fid = CreateTriangle_3D(v_ids[face[0]], v_ids[face[k]], v_ids[face[k + 1]], node_name);
                        result_ids.push_back(fid);
                    }
                }
            }

            return result_ids;
        }


        /**
         * @brief 创建单节点模式的柏拉图多面体
         * @param center_id 中心点 ID
         * @param radius 半径
         * @param type 类型 (4, 6, 8, 12, 20)
         * @param node_name 节点名称
         */
        uint64_t CreatePlatonicSolid_3D(uint64_t center_id, T radius, int type, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!is_3d(center_node->type) || !is_point(center_node->type)) {
                throw std::invalid_argument("Center must be a 3D Point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D; // 借用球体类型或自定义一个新位
            node.name = node_name;
            node.parents.push_back(center_id);

            // 存储生成参数
            node.data.platonic_solid_3d.radius = radius;
            node.data.platonic_solid_3d.type = type;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            center_node->children.push_back(new_id);

            // 绑定专用生成器
            node.solver = SolvePlatonicSolid_3D<T>;
            node.plotter = PlotPlatonicSolid_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateSphere_3D(uint64_t center_id, T radius, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!(is_3d(center_node->type) && is_point(center_node->type))) {
                throw std::invalid_argument("Center must be a 3D Point object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D;
            node.name = node_name;
            node.parents.push_back(center_id);

            // 存入半径
            node.data.sphere_3d.r = radius;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            center_node->children.push_back(new_id);

            // 绑定 Solver 和 Plotter
            node.solver  = SolveSphere_3D<T>;
            node.plotter = PlotSphere_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }
        /**
         * @brief 创建 3D 球体 (四点定球)
         * @param p1_id, p2_id, p3_id, p4_id 四个顶点的 ID
         */
        uint64_t CreateSphere_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, uint64_t p4_id, std::string node_name = "Unnamed")
        {
            // 1. 获取并校验四个点
            Node<T>* ns[4] = { GetNode(p1_id), GetNode(p2_id), GetNode(p3_id), GetNode(p4_id) };
            for(int i=0; i<4; ++i) {
                if (!(is_3d(ns[i]->type) && is_point(ns[i]->type))) {
                    throw std::invalid_argument("All arguments must be 3D Point objects.");
                }
            }

            // 2. 创建球体节点
            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D;
            node.name = node_name;

            // 建立 4 个父节点依赖
            for(int i=0; i<4; ++i) {
                node.parents.push_back(ns[i]->id);
                ns[i]->children.push_back(node.id);
            }

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 3. 绑定四点定球专用求解器
            node.solver  = SolveSphere_4P_3D<T>;
            node.plotter = PlotSphere_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateIntersectionPoint_2D(const std::vector<uint64_t>& parent_ids, T gx, T gy, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_INTERSECT;
            node.name = node_name;

            // 没有任何限制，直接压入所有 ID
            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(node.id);
            }

            node.data.snap_2d.guess_x = gx;
            node.data.snap_2d.guess_y = gy;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            node.solver = SolveIntersectionPoint_2D<T>;
            node.plotter = PlotPoint_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        /**
         * @brief 创建 3D 万能求交曲线/区域
         * @param parent_ids 参与求交的对象 ID 集合
         */
        uint64_t CreateIntersectionCurve_3D(const std::vector<uint64_t>& parent_ids, std::string node_name = "Unnamed")
        {
            if (parent_ids.size() < 2) {
                throw std::invalid_argument("IntersectionCurve requires at least 2 target nodes.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::UNKNOWN; // 可自定义一个 INTERSECT_CURVE 类型
            node.name = node_name;

            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(node.id);
            }

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 核心设计：仅绑定 Solver，没有 Plotter
            node.solver = SolveIntersectionCurve_3D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }
    };
}
