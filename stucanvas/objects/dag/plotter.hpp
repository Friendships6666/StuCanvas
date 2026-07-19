/***************************************************************************
 * Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
 *                                                                          *
 * Distributed under the terms of the MIT License.                          *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#pragma once

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/blocked_range2d.h>
#include <oneapi/tbb/blocked_range3d.h>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/task_arena.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <mutex>
#include <vector>

#include "assets.hpp"   // 确保能访问到先前定义的 DAGAssets::PointCloud2D_SoA
#include "function.hpp"
#include "l_shade.hpp"   // 确保能访问到先前定义的 utils::optimization::l_shade
#include "marching_cubes_tables.hpp"
namespace StuCanvas
{
    namespace detail
    {
        // =========================================================================
        // 🚀 零日志、全 TinyVector 驱动的自适应四叉树局部细分引擎
        // =========================================================================
        inline void subdivide_quadtree ( const utils::StuFunction< double ( double, double ) >& f, double x1, double x2,
                                         double y1, double y2, double min_w, double min_h,
                                         const utils::optimization::l_shade_parameters< double, 2 >& de_params,
                                         unsigned int max_threads, size_t depth, double g_xmin, double g_xmax,
                                         double g_ymin, double g_ymax, DAGAssets::PointCloud2D_SoA& out_cloud,
                                         std::mutex& out_mutex )
        {
            // 🚀 任务消峰限制：只在浅层（depth < 2）开启多线程并行分发，防止协程/线程爆炸
            constexpr size_t tbb_depth_threshold = 2;
            bool enable_tbb = ( depth < tbb_depth_threshold );
            unsigned int current_threads = enable_tbb ? max_threads : 1;

            // 拷贝用户指定的 L-SHADE 外部描述参数，就地覆写当前子象限的物理搜索边界与核心数
            utils::optimization::l_shade_parameters< double, 2 > params = de_params;
            params.lower_bounds = { x1, y1 };
            params.upper_bounds = { x2, y2 };
            params.threads = current_threads;

            // 🚀 核心优化：对于绘图，提前退出是必然开启的！
            // 一旦 L-SHADE 种群在演化过程中发现残差小于 10^-5 (即 1e-5)，代表该区域百分百存在根。
            // 此时算法会利用 target_attained 机制瞬间提前收工并退出，避免无谓的代际算力空转！
            params.enable_early_exit = true;
            params.early_exit_value_low = 0.0;
            params.early_exit_value_high = 1e-5;   // 10^-5

            auto cost_func = [ & ] ( double x, double y ) -> double { return std::abs ( f ( x, y ) ); };

            // 1. 运行高并发演化算法 L-SHADE 搜寻该区间内的残差极小值点
            std::array< double, 2 > best_solution = utils::optimization::l_shade ( cost_func, params );
            double final_cost = std::abs ( f ( best_solution[ 0 ], best_solution[ 1 ] ) );

            // 2. 判定当前区间是否存在根：唯一标准为数值小于 10^-5
            bool has_root = ( final_cost < 1e-5 );

            if ( !has_root )
            {
                return;   // 无根，物理剪枝，直接退出
            }

            // 3. 达到最小分辨率限制，写入 SoA 容器配置结果并退出
            if ( ( x2 - x1 ) <= min_w && ( y2 - y1 ) <= min_h )
            {
                std::scoped_lock lock ( out_mutex );

                // 直接推入 SoA 扁平数组
                out_cloud.x.push_back ( ( x1 + x2 ) / 2.0 );
                out_cloud.y.push_back ( ( y1 + y2 ) / 2.0 );
                return;
            }

            // 4. 四叉树均分计算
            double cx = ( x1 + x2 ) / 2.0;
            double cy = ( y1 + y2 ) / 2.0;

            // 5. 10% 边界膨胀（两端各自向外延伸子区间大小的 5%）
            double sw = cx - x1;
            double sh = cy - y1;
            double dx = 0.05 * sw;
            double dy = 0.05 * sh;

            auto clamp_x = [ & ] ( double val ) { return std::clamp ( val, g_xmin, g_xmax ); };
            auto clamp_y = [ & ] ( double val ) { return std::clamp ( val, g_ymin, g_ymax ); };

            // 6. 递归分裂四个经过 10% 膨胀保护的子区域
            if ( enable_tbb )
            {
                oneapi::tbb::parallel_invoke (
                    [ & ] ()
                    {
                        subdivide_quadtree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                             clamp_y ( cy + dy ), min_w, min_h, de_params, max_threads, depth + 1,
                                             g_xmin, g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_quadtree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                             clamp_y ( cy + dy ), min_w, min_h, de_params, max_threads, depth + 1,
                                             g_xmin, g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_quadtree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                             clamp_y ( y2 + dy ), min_w, min_h, de_params, max_threads, depth + 1,
                                             g_xmin, g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_quadtree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                             clamp_y ( y2 + dy ), min_w, min_h, de_params, max_threads, depth + 1,
                                             g_xmin, g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                    } );
            }
            else
            {
                subdivide_quadtree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                     clamp_y ( cy + dy ), min_w, min_h, de_params, max_threads, depth + 1, g_xmin,
                                     g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                subdivide_quadtree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                     clamp_y ( cy + dy ), min_w, min_h, de_params, max_threads, depth + 1, g_xmin,
                                     g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                subdivide_quadtree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                     clamp_y ( y2 + dy ), min_w, min_h, de_params, max_threads, depth + 1, g_xmin,
                                     g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
                subdivide_quadtree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                     clamp_y ( y2 + dy ), min_w, min_h, de_params, max_threads, depth + 1, g_xmin,
                                     g_xmax, g_ymin, g_ymax, out_cloud, out_mutex );
            }
        }

    }   // namespace detail

    // =========================================================================
    // 🚀 外部调用主接口 (完全零冗余、对齐项目最优解)
    // =========================================================================
    inline void plotImplicit2D ( const utils::StuFunction< double ( double, double ) >& f, double min_block_width,
                                 double min_block_height,
                                 const utils::optimization::l_shade_parameters< double, 2 >& de_params,
                                 DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        std::mutex out_mutex;

        // 🚀 0 冗余提取：直接从描述参数 de_params 中自适应读取初始世界边界和物理线程配置
        double x_min = de_params.lower_bounds[ 0 ];
        double x_max = de_params.upper_bounds[ 0 ];
        double y_min = de_params.lower_bounds[ 1 ];
        double y_max = de_params.upper_bounds[ 1 ];

        unsigned int max_threads = de_params.threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                // 启动初始自举划分
                detail::subdivide_quadtree ( f, x_min, x_max, y_min, y_max, min_block_width, min_block_height,
                                             de_params, max_threads,
                                             0,   // 初始深度为 0
                                             x_min, x_max, y_min, y_max, out_cloud, out_mutex );
            } );
    }
    namespace detail
    {
        // =========================================================================
        // 🚀 零日志、全 TinyVector 驱动的自适应八叉树局部细分引擎
        // =========================================================================
        inline void subdivide_octree ( const utils::StuFunction< double ( double, double, double ) >& f, double x1,
                                       double x2, double y1, double y2, double z1, double z2, double min_w,
                                       double min_h, double min_d,
                                       const utils::optimization::l_shade_parameters< double, 3 >& de_params,
                                       unsigned int max_threads, size_t depth, double g_xmin, double g_xmax,
                                       double g_ymin, double g_ymax, double g_zmin, double g_zmax,
                                       DAGAssets::PointCloud3D_SoA& out_cloud, std::mutex& out_mutex )
        {
            // 🚀 八叉树专用任务消峰：由于 8 分裂因子极大，仅在 depth < 1 处并行即可完美榨干 CPU
            constexpr size_t tbb_depth_threshold = 1;
            bool enable_tbb = ( depth < tbb_depth_threshold );
            unsigned int current_threads = enable_tbb ? max_threads : 1;

            // 拷贝用户指定的 L-SHADE 外部描述参数，就地覆写当前子立方体的物理搜索边界与核心数
            utils::optimization::l_shade_parameters< double, 3 > params = de_params;
            params.lower_bounds = { x1, y1, z1 };
            params.upper_bounds = { x2, y2, z2 };
            params.threads = current_threads;

            // 强制开启 1 阶 L-SHADE 物理级提前退出
            params.enable_early_exit = true;
            params.early_exit_value_low = 0.0;
            params.early_exit_value_high = 1e-5;   // 10^-5

            auto cost_func = [ & ] ( double x, double y, double z ) -> double { return std::abs ( f ( x, y, z ) ); };

            // 1. 运行三维高并发演化算法 L-SHADE 搜寻该立方体内是否存在曲面根
            std::array< double, 3 > best_solution = utils::optimization::l_shade ( cost_func, params );
            double final_cost = std::abs ( f ( best_solution[ 0 ], best_solution[ 1 ], best_solution[ 2 ] ) );

            // 2. 判定当前立方体区间是否存在根：唯一标准为数值小于 10^-5
            bool has_root = ( final_cost < 1e-5 );

            if ( !has_root )
            {
                return;   // 无根，物理剪枝，直接退出
            }

            // 3. 达到最小分辨率限制，写入 SoA 容器配置结果并退出
            if ( ( x2 - x1 ) <= min_w && ( y2 - y1 ) <= min_h && ( z2 - z1 ) <= min_d )
            {
                std::scoped_lock lock ( out_mutex );

                // 直接推入 3D SoA 扁平数组，零多余内存开销
                out_cloud.x.push_back ( ( x1 + x2 ) / 2.0 );
                out_cloud.y.push_back ( ( y1 + y2 ) / 2.0 );
                out_cloud.z.push_back ( ( z1 + z2 ) / 2.0 );
                return;
            }

            // 4. 八叉树中点面分裂计算
            double cx = ( x1 + x2 ) / 2.0;
            double cy = ( y1 + y2 ) / 2.0;
            double cz = ( z1 + z2 ) / 2.0;

            // 5. 10% 边界膨胀（两端各自向外延伸子区间大小的 5%）
            double sw = cx - x1;
            double sh = cy - y1;
            double sd = cz - z1;
            double dx = 0.05 * sw;
            double dy = 0.05 * sh;
            double dz = 0.05 * sd;

            auto clamp_x = [ & ] ( double val ) { return std::clamp ( val, g_xmin, g_xmax ); };
            auto clamp_y = [ & ] ( double val ) { return std::clamp ( val, g_ymin, g_ymax ); };
            auto clamp_z = [ & ] ( double val ) { return std::clamp ( val, g_zmin, g_zmax ); };

            // 6. 递归分裂八个经过 10% 膨胀保护的子立方体区域（使用 oneTBB 进行高能并行分发）
            if ( enable_tbb )
            {
                oneapi::tbb::parallel_invoke (
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                           clamp_y ( cy + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                           clamp_y ( cy + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                           clamp_y ( y2 + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                           clamp_y ( y2 + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                           clamp_y ( cy + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                           clamp_y ( cy + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                           clamp_y ( y2 + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    },
                    [ & ] ()
                    {
                        subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                           clamp_y ( y2 + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h,
                                           min_d, de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax,
                                           g_zmin, g_zmax, out_cloud, out_mutex );
                    } );
            }
            else
            {
                subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                   clamp_y ( cy + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                   clamp_y ( cy + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                   clamp_y ( y2 + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                   clamp_y ( y2 + dy ), clamp_z ( z1 - dz ), clamp_z ( cz + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( y1 - dy ),
                                   clamp_y ( cy + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( y1 - dy ),
                                   clamp_y ( cy + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( x1 - dx ), clamp_x ( cx + dx ), clamp_y ( cy - dy ),
                                   clamp_y ( y2 + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
                subdivide_octree ( f, clamp_x ( cx - dx ), clamp_x ( x2 + dx ), clamp_y ( cy - dy ),
                                   clamp_y ( y2 + dy ), clamp_z ( cz - dz ), clamp_z ( z2 + dz ), min_w, min_h, min_d,
                                   de_params, max_threads, depth + 1, g_xmin, g_xmax, g_ymin, g_ymax, g_zmin, g_zmax,
                                   out_cloud, out_mutex );
            }
        }

    }   // namespace detail

    // =========================================================================
    // 🚀 外部调用主接口 (完全零冗余、对齐 3D 几何处理最优解)
    // =========================================================================
    inline void plotImplicit3D ( const utils::StuFunction< double ( double, double, double ) >& f,
                                 double min_block_width, double min_block_height, double min_block_depth,
                                 const utils::optimization::l_shade_parameters< double, 3 >& de_params,
                                 DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        // 0 冗余提取：直接从描述参数 de_params 中自适应读取初始世界三维边界和物理线程配置
        double x_min = de_params.lower_bounds[ 0 ];
        double x_max = de_params.upper_bounds[ 0 ];
        double y_min = de_params.lower_bounds[ 1 ];
        double y_max = de_params.upper_bounds[ 1 ];
        double z_min = de_params.lower_bounds[ 2 ];
        double z_max = de_params.upper_bounds[ 2 ];

        unsigned int max_threads = de_params.threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                // 启动初始自举划分
                detail::subdivide_octree ( f, x_min, x_max, y_min, y_max, z_min, z_max, min_block_width,
                                           min_block_height, min_block_depth, de_params, max_threads,
                                           0,   // 初始深度为 0
                                           x_min, x_max, y_min, y_max, z_min, z_max, out_cloud, out_mutex );
            } );
    }
    namespace detail
    {
        // 🚀 线性插值器：计算等值线在网格边界上的精确过零点
        inline std::pair< double, double > interpolate ( double xa, double ya, double va, double xb, double yb,
                                                         double vb ) noexcept
        {
            if ( std::abs ( va - vb ) < 1e-12 )
            {
                return { ( xa + xb ) / 2.0, ( ya + yb ) / 2.0 };
            }
            double t = -va / ( vb - va );
            return { xa + t * ( xb - xa ), ya + t * ( yb - ya ) };
        }

        // 🚀 叶子节点：标准的 16 种状态 Marching Squares 拓扑生成器
        inline void generate_segments ( double x1, double x2, double y1, double y2, double v00, double v10, double v11,
                                        double v01, DAGAssets::LineStrip2D_SoA& out_strip, std::mutex& out_mutex )
        {
            // 通过四角正负状态计算出 0~15 的拓扑状态索引
            int index = ( v00 < 0.0 ? 1 : 0 ) | ( v10 < 0.0 ? 2 : 0 ) | ( v11 < 0.0 ? 4 : 0 ) | ( v01 < 0.0 ? 8 : 0 );

            if ( index == 0 || index == 15 )
            {
                return;   // 无过零线段，直接退出
            }

            std::pair< double, double > p00 = { x1, y1 };
            std::pair< double, double > p10 = { x2, y1 };
            std::pair< double, double > p11 = { x2, y2 };
            std::pair< double, double > p01 = { x1, y2 };

            // 计算四条边界上的插值零点
            std::pair< double, double > e0 =
                interpolate ( p00.first, p00.second, v00, p10.first, p10.second, v10 );   // Bottom
            std::pair< double, double > e1 =
                interpolate ( p10.first, p10.second, v10, p11.first, p11.second, v11 );   // Right
            std::pair< double, double > e2 =
                interpolate ( p01.first, p01.second, v01, p11.first, p11.second, v11 );   // Top
            std::pair< double, double > e3 =
                interpolate ( p00.first, p00.second, v00, p01.first, p01.second, v01 );   // Left

            std::pair< double, double > s1, s2;
            std::pair< double, double > s3, s4;
            bool double_line = false;

            switch ( index )
            {
                case 1:
                    s1 = e3;
                    s2 = e0;
                    break;
                case 2:
                    s1 = e0;
                    s2 = e1;
                    break;
                case 3:
                    s1 = e3;
                    s2 = e1;
                    break;
                case 4:
                    s1 = e2;
                    s2 = e1;
                    break;
                case 5:
                    s1 = e3;
                    s2 = e2;
                    s3 = e0;
                    s4 = e1;
                    double_line = true;
                    break;   // 歧义对角线
                case 6:
                    s1 = e0;
                    s2 = e2;
                    break;
                case 7:
                    s1 = e3;
                    s2 = e2;
                    break;
                case 8:
                    s1 = e3;
                    s2 = e2;
                    break;
                case 9:
                    s1 = e0;
                    s2 = e2;
                    break;
                case 10:
                    s1 = e3;
                    s2 = e0;
                    s3 = e2;
                    s4 = e1;
                    double_line = true;   // 歧义对角线
                case 11:
                    s1 = e2;
                    s2 = e1;
                    break;
                case 12:
                    s1 = e3;
                    s2 = e1;
                    break;
                case 13:
                    s1 = e0;
                    s2 = e1;
                    break;
                case 14:
                    s1 = e3;
                    s2 = e0;
                    break;
                default:
                    break;
            }

            {
                std::scoped_lock lock ( out_mutex );
                out_strip.x.push_back ( s1.first );
                out_strip.y.push_back ( s1.second );
                out_strip.x.push_back ( s2.first );
                out_strip.y.push_back ( s2.second );
                if ( double_line )
                {
                    out_strip.x.push_back ( s3.first );
                    out_strip.y.push_back ( s3.second );
                    out_strip.x.push_back ( s4.first );
                    out_strip.y.push_back ( s4.second );
                }
            }
        }

        // 🚀 递归子划分：就地 2x2 分裂，自适应追逐细分等值线
        inline void marching_squares_subdivide ( const utils::StuFunction< double ( double, double ) >& f, double x1,
                                                 double x2, double y1, double y2, double v00, double v10, double v11,
                                                 double v01, size_t depth, size_t max_depth,
                                                 DAGAssets::LineStrip2D_SoA& out_strip, std::mutex& out_mutex )
        {
            // 达到极限细分深度，停止细分，直接插值出高精度的线段几何
            if ( depth == max_depth )
            {
                generate_segments ( x1, x2, y1, y2, v00, v10, v11, v01, out_strip, out_mutex );
                return;
            }

            double cx = ( x1 + x2 ) / 2.0;
            double cy = ( y1 + y2 ) / 2.0;

            // 就地动态采样中点（只有在这个局部有根的小区域才会发生计算）
            double vc = f ( cx, cy );
            double vb = f ( cx, y1 );
            double vt = f ( cx, y2 );
            double vl = f ( x1, cy );
            double vr = f ( x2, cy );

            // 检查 4 个子网格中哪些跨越零点（有根），选择性开启下一级细分（Narrow-band 降维打击）
            auto check_root = [] ( double a, double b, double c, double d ) -> bool
            {
                return !( ( a >= 0.0 && b >= 0.0 && c >= 0.0 && d >= 0.0 ) ||
                          ( a < 0.0 && b < 0.0 && c < 0.0 && d < 0.0 ) );
            };

            // 1. 左下子格 [x1, cx] x [y1, cy]
            if ( check_root ( v00, vb, vc, vl ) )
            {
                marching_squares_subdivide ( f, x1, cx, y1, cy, v00, vb, vc, vl, depth + 1, max_depth, out_strip,
                                             out_mutex );
            }
            // 2. 右下子格 [cx, x2] x [y1, cy]
            if ( check_root ( vb, v10, vr, vc ) )
            {
                marching_squares_subdivide ( f, cx, x2, y1, cy, vb, v10, vr, vc, depth + 1, max_depth, out_strip,
                                             out_mutex );
            }
            // 3. 左上子格 [x1, cx] x [cy, y2]
            if ( check_root ( vl, vc, vt, v01 ) )
            {
                marching_squares_subdivide ( f, x1, cx, cy, y2, vl, vc, vt, v01, depth + 1, max_depth, out_strip,
                                             out_mutex );
            }
            // 4. 右上子格 [cx, x2] x [cy, y2]
            if ( check_root ( vc, vr, v11, vt ) )
            {
                marching_squares_subdivide ( f, cx, x2, cy, y2, vc, vr, v11, vt, depth + 1, max_depth, out_strip,
                                             out_mutex );
            }
        }

    }   // namespace detail

    // =========================================================================
    // 🚀 外部调用主接口 (分层并行、共享角0重复计算、完全解耦)
    // =========================================================================
    inline void marchingSquares2D ( const utils::StuFunction< double ( double, double ) >& f, double x_min,
                                    double x_max, double y_min, double y_max,
                                    double step,                      // 粗网格离散步长
                                    uint32_t max_subdivision_depth,   // 最大自适应 2x2 细分深度
                                    unsigned int threads, DAGAssets::LineStrip2D_SoA& out_strip )
    {
        out_strip.x.clear ();
        out_strip.y.clear ();
        std::mutex out_mutex;

        // 计算粗网格行列数
        size_t M = std::max ( size_t ( 1 ), static_cast< size_t > ( std::round ( ( x_max - x_min ) / step ) ) );
        size_t N = std::max ( size_t ( 1 ), static_cast< size_t > ( std::round ( ( y_max - y_min ) / step ) ) );

        double dx = ( x_max - x_min ) / M;
        double dy = ( y_max - y_min ) / N;

        unsigned int max_threads = threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                // 🚀 核心优化：分配一个一维扁平缓存，用于暂存 (M+1) * (N+1) 个粗网格顶点的值
                utils::TinyVector< double > grid_values;
                grid_values.resize ( static_cast< uint32_t > ( ( M + 1 ) * ( N + 1 ) ) );

                // 🚀 核心优化：并行的、无任何重复地计算所有共享角顶点的值，物理上每个格点仅求值 1 次！
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, M + 1 ),
                                            [ & ] ( const oneapi::tbb::blocked_range< size_t >& r )
                                            {
                                                for ( size_t i = r.begin (); i < r.end (); ++i )
                                                {
                                                    double x = x_min + i * dx;
                                                    size_t offset = i * ( N + 1 );
                                                    for ( size_t j = 0; j <= N; ++j )
                                                    {
                                                        double y = y_min + j * dy;
                                                        grid_values[ offset + j ] = f ( x, y );
                                                    }
                                                }
                                            } );

                // 🚀 核心优化：通过 TBB 2D 范围分发器并行扫描所有 M * N 个粗网格单元，就地触发局部自适应细分
                oneapi::tbb::parallel_for (
                    oneapi::tbb::blocked_range2d< size_t > ( 0, M, 0, N ),
                    [ & ] ( const oneapi::tbb::blocked_range2d< size_t >& r )
                    {
                        for ( size_t i = r.rows ().begin (); i != r.rows ().end (); ++i )
                        {
                            double x1 = x_min + i * dx;
                            double x2 = x1 + dx;
                            size_t o0 = i * ( N + 1 );
                            size_t o1 = ( i + 1 ) * ( N + 1 );

                            for ( size_t j = r.cols ().begin (); j != r.cols ().end (); ++j )
                            {
                                double y1 = y_min + j * dy;
                                double y2 = y1 + dy;

                                // 直接以 O(1) 开销读取预存的共享角的值，0 重复计算
                                double v00 = grid_values[ o0 + j ];
                                double v10 = grid_values[ o1 + j ];
                                double v11 = grid_values[ o1 + j + 1 ];
                                double v01 = grid_values[ o0 + j + 1 ];

                                // 快速零交点判定
                                bool has_root = !( ( v00 >= 0.0 && v10 >= 0.0 && v11 >= 0.0 && v01 >= 0.0 ) ||
                                                   ( v00 < 0.0 && v10 < 0.0 && v11 < 0.0 && v01 < 0.0 ) );

                                if ( has_root )
                                {
                                    detail::marching_squares_subdivide ( f, x1, x2, y1, y2, v00, v10, v11, v01,
                                                                         0,   // 初始深度为 0
                                                                         max_subdivision_depth, out_strip, out_mutex );
                                }
                            }
                        }
                    } );
            } );
    }
    struct Point3D
    {
        double x;
        double y;
        double z;
    };

    namespace detail
    {
        // 🚀 线性插值器：计算等值面在网格边界上的精确过零点
        inline Point3D interpolate ( double xa, double ya, double za, double va, double xb, double yb, double zb,
                                     double vb ) noexcept
        {
            if ( std::abs ( va - vb ) < 1e-12 )
            {
                return { ( xa + xb ) / 2.0, ( ya + yb ) / 2.0, ( za + zb ) / 2.0 };
            }
            double t = -va / ( vb - va );
            return { xa + t * ( xb - xa ), ya + t * ( yb - ya ), za + t * ( zb - za ) };
        }

        // 🚀 核心：对 2x2x2 细分出来的每个子立方体进行标准的 256 位拓扑重建与面连接
        //    (完全对齐您的 char 类型 triTable，0 窄化警告)
        inline void generate_cube_triangles ( double x1, double x2, double y1, double y2, double z1, double z2,
                                              double v0, double v1, double v2, double v3, double v4, double v5,
                                              double v6, double v7, DAGAssets::TriangleMesh3D_SoA& local_mesh )
        {
            // 通过 8 顶点正负状态计算出 0~255 的拓扑状态码
            int cubeindex = 0;
            if ( v0 < 0.0 )
            {
                cubeindex |= 1;
            }
            if ( v1 < 0.0 )
            {
                cubeindex |= 2;
            }
            if ( v2 < 0.0 )
            {
                cubeindex |= 4;
            }
            if ( v3 < 0.0 )
            {
                cubeindex |= 8;
            }
            if ( v4 < 0.0 )
            {
                cubeindex |= 16;
            }
            if ( v5 < 0.0 )
            {
                cubeindex |= 32;
            }
            if ( v6 < 0.0 )
            {
                cubeindex |= 64;
            }
            if ( v7 < 0.0 )
            {
                cubeindex |= 128;
            }

            int edge_mask = edgeTable[ cubeindex ];
            if ( edge_mask == 0 )
            {
                return;
            }

            // 线性插值生成 12 条棱上的顶点数据
            std::array< Point3D, 12 > vertlist;
            if ( edge_mask & 1 )
            {
                vertlist[ 0 ] = interpolate ( x1, y1, z1, v0, x2, y1, z1, v1 );
            }
            if ( edge_mask & 2 )
            {
                vertlist[ 1 ] = interpolate ( x2, y1, z1, v1, x2, y2, z1, v2 );
            }
            if ( edge_mask & 4 )
            {
                vertlist[ 2 ] = interpolate ( x2, y2, z1, v2, x1, y2, z1, v3 );
            }
            if ( edge_mask & 8 )
            {
                vertlist[ 3 ] = interpolate ( x1, y2, z1, v3, x1, y1, z1, v0 );
            }
            if ( edge_mask & 16 )
            {
                vertlist[ 4 ] = interpolate ( x1, y1, z2, v4, x2, y1, z2, v5 );
            }
            if ( edge_mask & 32 )
            {
                vertlist[ 5 ] = interpolate ( x2, y1, z2, v5, x2, y2, z2, v6 );
            }
            if ( edge_mask & 64 )
            {
                vertlist[ 6 ] = interpolate ( x2, y2, z2, v6, x1, y2, z2, v7 );
            }
            if ( edge_mask & 128 )
            {
                vertlist[ 7 ] = interpolate ( x1, y2, z2, v7, x1, y1, z2, v4 );
            }
            if ( edge_mask & 256 )
            {
                vertlist[ 8 ] = interpolate ( x1, y1, z1, v0, x1, y1, z2, v4 );
            }
            if ( edge_mask & 512 )
            {
                vertlist[ 9 ] = interpolate ( x2, y1, z1, v1, x2, y1, z2, v5 );
            }
            if ( edge_mask & 1024 )
            {
                vertlist[ 10 ] = interpolate ( x2, y2, z1, v2, x2, y2, z2, v6 );
            }
            if ( edge_mask & 2048 )
            {
                vertlist[ 11 ] = interpolate ( x1, y2, z1, v3, x1, y2, z2, v7 );
            }

            // 零开销面片拓扑连接并暂存入局部缓存
            for ( int i = 0; triTable[ cubeindex ][ i ] != -1; i += 3 )
            {
                Point3D pA = vertlist[ static_cast< size_t > ( triTable[ cubeindex ][ i ] ) ];
                Point3D pB = vertlist[ static_cast< size_t > ( triTable[ cubeindex ][ i + 1 ] ) ];
                Point3D pC = vertlist[ static_cast< size_t > ( triTable[ cubeindex ][ i + 2 ] ) ];

                uint32_t base_idx = static_cast< uint32_t > ( local_mesh.x.size () );
                local_mesh.x.push_back ( pA.x );
                local_mesh.y.push_back ( pA.y );
                local_mesh.z.push_back ( pA.z );

                local_mesh.x.push_back ( pB.x );
                local_mesh.y.push_back ( pB.y );
                local_mesh.z.push_back ( pB.z );

                local_mesh.x.push_back ( pC.x );
                local_mesh.y.push_back ( pC.y );
                local_mesh.z.push_back ( pC.z );

                local_mesh.indices.push_back ( base_idx );
                local_mesh.indices.push_back ( base_idx + 1 );
                local_mesh.indices.push_back ( base_idx + 2 );
            }
        }

    }   // namespace detail

    // =========================================================================
    // 🚀 外部调用主接口 (分层并行、共享角0重复计算、2x2x2 极速细分、HPC 局部锁合并)
    // =========================================================================
    inline void marchingCubes3D ( const utils::StuFunction< double ( double, double, double ) >& f, double x_min,
                                  double x_max, double y_min, double y_max, double z_min, double z_max,
                                  double step,   // 粗网格离散步长
                                  unsigned int threads, DAGAssets::TriangleMesh3D_SoA& out_mesh )
    {
        out_mesh.x.clear ();
        out_mesh.y.clear ();
        out_mesh.z.clear ();
        out_mesh.indices.clear ();
        std::mutex out_mutex;

        // 计算粗网格行列数
        size_t M = std::max ( size_t ( 1 ), static_cast< size_t > ( std::round ( ( x_max - x_min ) / step ) ) );
        size_t N = std::max ( size_t ( 1 ), static_cast< size_t > ( std::round ( ( y_max - y_min ) / step ) ) );
        size_t K = std::max ( size_t ( 1 ), static_cast< size_t > ( std::round ( ( z_max - z_min ) / step ) ) );

        double dx = ( x_max - x_min ) / M;
        double dy = ( y_max - y_min ) / N;
        double dz = ( z_max - z_min ) / K;

        unsigned int max_threads = threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                // 1. 连续扁平排布缓冲区：存储所有 (M+1) * (N+1) * (K+1) 个粗网格交点的值
                utils::TinyVector< double > grid_values;
                grid_values.resize ( static_cast< uint32_t > ( ( M + 1 ) * ( N + 1 ) * ( K + 1 ) ) );

                // 1.1 并行、零重复计算所有共享角顶点的值
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range2d< size_t > ( 0, M + 1, 0, N + 1 ),
                                            [ & ] ( const oneapi::tbb::blocked_range2d< size_t >& r )
                                            {
                                                for ( size_t i = r.rows ().begin (); i != r.rows ().end (); ++i )
                                                {
                                                    double x = x_min + i * dx;
                                                    size_t slice_offset = i * ( N + 1 ) * ( K + 1 );
                                                    for ( size_t j = r.cols ().begin (); j != r.cols ().end (); ++j )
                                                    {
                                                        double y = y_min + j * dy;
                                                        size_t row_offset = slice_offset + j * ( K + 1 );
                                                        for ( size_t k = 0; k <= K; ++k )
                                                        {
                                                            double z = z_min + k * dz;
                                                            grid_values[ row_offset + k ] = f ( x, y, z );
                                                        }
                                                    }
                                                }
                                            } );

                // 2. 并行扫视 M * N * K 个三维网格单元，就地对跨零单元触发 2x2x2 细分
                oneapi::tbb::parallel_for (
                    oneapi::tbb::blocked_range3d< size_t > ( 0, M, 0, N, 0, K ),
                    [ & ] ( const oneapi::tbb::blocked_range3d< size_t >& r )
                    {
                        // 🚀 核心：为每个并发任务建立局部 thread-safe 缓冲网格，彻底消灭内存锁冲突！
                        DAGAssets::TriangleMesh3D_SoA local_mesh;

                        for ( size_t i = r.pages ().begin (); i != r.pages ().end (); ++i )
                        {
                            double x1 = x_min + i * dx;
                            double x2 = x1 + dx;
                            size_t s0 = i * ( N + 1 ) * ( K + 1 );
                            size_t s1 = ( i + 1 ) * ( N + 1 ) * ( K + 1 );

                            for ( size_t j = r.rows ().begin (); j != r.rows ().end (); ++j )
                            {
                                double y1 = y_min + j * dy;
                                double y2 = y1 + dy;
                                size_t r00 = s0 + j * ( K + 1 );
                                size_t r01 = s0 + ( j + 1 ) * ( K + 1 );
                                size_t r10 = s1 + j * ( K + 1 );
                                size_t r11 = s1 + ( j + 1 ) * ( K + 1 );

                                for ( size_t k = r.cols ().begin (); k != r.cols ().end (); ++k )
                                {
                                    double z1 = z_min + k * dz;
                                    double z2 = z1 + dz;

                                    // 极速 O(1) 提取预存的共享八角的值
                                    double v0 = grid_values[ r00 + k ];
                                    double v1 = grid_values[ r10 + k ];
                                    double v2 = grid_values[ r11 + k ];
                                    double v3 = grid_values[ r01 + k ];
                                    double v4 = grid_values[ r00 + k + 1 ];
                                    double v5 = grid_values[ r10 + k + 1 ];
                                    double v6 = grid_values[ r11 + k + 1 ];
                                    double v7 = grid_values[ r01 + k + 1 ];

                                    // 快速零交点判定
                                    bool has_root = !( ( v0 >= 0.0 && v1 >= 0.0 && v2 >= 0.0 && v3 >= 0.0 &&
                                                         v4 >= 0.0 && v5 >= 0.0 && v6 >= 0.0 && v7 >= 0.0 ) ||
                                                       ( v0 < 0.0 && v1 < 0.0 && v2 < 0.0 && v3 < 0.0 && v4 < 0.0 &&
                                                         v5 < 0.0 && v6 < 0.0 && v7 < 0.0 ) );

                                    if ( has_root )
                                    {
                                        // 🚀 3. 触发 2x2x2 局部极速细分（共 8 个子立方体单元）
                                        // 细分网格交点数仅为 3 * 3 * 3 = 27 个，在栈上分配，内存极其紧凑
                                        std::array< double, 27 > sub_grid;
                                        double sdx = dx / 2.0;
                                        double sdy = dy / 2.0;
                                        double sdz = dz / 2.0;

                                        // 计算局部 27 个交点处的值，无任何多余分配，Cache 极度友好
                                        for ( size_t u = 0; u <= 2; ++u )
                                        {
                                            double sx = x1 + u * sdx;
                                            size_t u_offset = u * 9;
                                            for ( size_t v = 0; v <= 2; ++v )
                                            {
                                                double sy = y1 + v * sdy;
                                                size_t uv_offset = u_offset + v * 3;
                                                for ( size_t w = 0; w <= 2; ++w )
                                                {
                                                    double sz = z1 + w * sdz;
                                                    sub_grid[ uv_offset + w ] = f ( sx, sy, sz );
                                                }
                                            }
                                        }

                                        // 对 2 * 2 * 2 = 8 个子格进行标准的 Marching Cubes 拓扑重建
                                        for ( size_t u = 0; u < 2; ++u )
                                        {
                                            double sx1 = x1 + u * sdx;
                                            double sx2 = sx1 + sdx;
                                            size_t u0 = u * 9;
                                            size_t u1 = ( u + 1 ) * 9;

                                            for ( size_t v = 0; v < 2; ++v )
                                            {
                                                double sy1 = y1 + v * sdy;
                                                double sy2 = sy1 + sdy;
                                                size_t uv00 = u0 + v * 3;
                                                size_t uv01 = u0 + ( v + 1 ) * 3;
                                                size_t uv10 = u1 + v * 3;
                                                size_t uv11 = u1 + ( v + 1 ) * 3;

                                                for ( size_t w = 0; w < 2; ++w )
                                                {
                                                    double sz1 = z1 + w * sdz;
                                                    double sz2 = sz1 + sdz;

                                                    double sv0 = sub_grid[ uv00 + w ];
                                                    double sv1 = sub_grid[ uv10 + w ];
                                                    double sv2 = sub_grid[ uv11 + w ];
                                                    double sv3 = sub_grid[ uv01 + w ];
                                                    double sv4 = sub_grid[ uv00 + w + 1 ];
                                                    double sv5 = sub_grid[ uv10 + w + 1 ];
                                                    double sv6 = sub_grid[ uv11 + w + 1 ];
                                                    double sv7 = sub_grid[ uv01 + w + 1 ];

                                                    detail::generate_cube_triangles ( sx1, sx2, sy1, sy2, sz1, sz2, sv0,
                                                                                      sv1, sv2, sv3, sv4, sv5, sv6, sv7,
                                                                                      local_mesh );
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 🚀 合流：将当前子任务的所有局部数据一次性原子写入全局 out_mesh，吞吐量达到瓶颈极限
                        if ( !local_mesh.x.empty () )
                        {
                            std::scoped_lock lock ( out_mutex );
                            uint32_t global_base = static_cast< uint32_t > ( out_mesh.x.size () );

                            // 利用 TinyVector 的 append 接口批量拷贝，0 多余内存分配
                            out_mesh.x.append ( local_mesh.x );
                            out_mesh.y.append ( local_mesh.y );
                            out_mesh.z.append ( local_mesh.z );

                            for ( uint32_t idx : local_mesh.indices )
                            {
                                out_mesh.indices.push_back ( global_base + idx );
                            }
                        }
                    } );
            } );
    }
    inline void stuplot_implicit2D (
        const utils::StuFunction< double ( double, double ) >& scalar_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& interval_fn,
        const DAGAssets::LShade& de_params, double x_min, double x_max, double y_min, double y_max,
        double min_block_width, double min_block_height, double epsilon, DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        // 清理输出缓冲区
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        std::mutex out_mutex;

        // 内部物理内存块
        struct Box
        {
            double x0, x1, y0, y1;
        };

        // 复用引擎底层的纯栈容器，杜绝任何 std::vector 的堆碎片开销
        utils::TinyVector< Box > leaf_tasks;
        utils::TinyVector< Box > stack;

        stack.push_back ( { x_min, x_max, y_min, y_max } );

        // --- 阶段 1：使用区间算术进行四叉树拓扑剪枝 ---
        while ( !stack.empty () )
        {
            Box t = stack.back ();
            stack.pop_back ();

            // 转换区域为双轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto res_ia = interval_fn ( ix, iy );

            // 若本块绝对不可能有根，物理剪枝
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 未达下限，继续切分
            if ( ( t.x1 - t.x0 ) > min_block_width || ( t.y1 - t.y0 ) > min_block_height )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;

                stack.push_back ( { t.x0, mx, t.y0, my } );
                stack.push_back ( { mx, t.x1, t.y0, my } );
                stack.push_back ( { t.x0, mx, my, t.y1 } );
                stack.push_back ( { mx, t.x1, my, t.y1 } );
            }
            else
            {
                // 抵达分辨率阈值，推入物理计算列队
                leaf_tasks.push_back ( t );
            }
        }

        if ( leaf_tasks.empty () )
        {
            return;
        }

        // --- 阶段 2：基于 TBB 调度的 Newton-LSHADE 融合求解 ---
        // 彻底剔除外层 Arena，完全信赖 TBB 自身嵌套任务窃取的绝佳调度
        oneapi::tbb::parallel_for (
            oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
            [ & ] ( const oneapi::tbb::blocked_range< size_t >& r )
            {
                // 线程局部微存，彻底剥离全局锁并发竞争
                utils::TinyVector< double > local_x;
                utils::TinyVector< double > local_y;

                for ( size_t i = r.begin (); i != r.end (); ++i )
                {
                    const Box& b = leaf_tasks[ i ];

                    double cx = ( b.x0 + b.x1 ) * 0.5;
                    double cy = ( b.y0 + b.y1 ) * 0.5;
                    bool found = false;

                    // 1. 牛顿差分正交迭代（限定最大 5 次尝试）
                    for ( int iter = 0; iter < 5; ++iter )
                    {
                        auto verify_ix =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cx - epsilon, cx + epsilon ) );
                        auto verify_iy =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cy - epsilon, cy + epsilon ) );
                        auto verify_res = interval_fn ( verify_ix, verify_iy );

                        // IA 证明已入根区，立停
                        if ( utils::detals::possible_root ( verify_res ) &&
                             !utils::detals::is_unbounded ( verify_res ) )
                        {
                            found = true;
                            break;
                        }

                        // 尚未逼近，基于 Epsilon 数值差分计算二维梯度
                        double f_val = scalar_fn ( cx, cy );
                        double f_x_plus = scalar_fn ( cx + epsilon, cy );
                        double f_x_minus = scalar_fn ( cx - epsilon, cy );
                        double f_y_plus = scalar_fn ( cx, cy + epsilon );
                        double f_y_minus = scalar_fn ( cx, cy - epsilon );

                        double df_dx = ( f_x_plus - f_x_minus ) / ( 2.0 * epsilon );
                        double df_dy = ( f_y_plus - f_y_minus ) / ( 2.0 * epsilon );
                        double grad_sq = df_dx * df_dx + df_dy * df_dy;

                        // 遇奇异点梯度消失，中止迭代移交 L-SHADE
                        if ( grad_sq < 1e-12 )
                        {
                            break;
                        }

                        // 执行向等值面的物理正交投影迭代
                        cx -= f_val * df_dx / grad_sq;
                        cy -= f_val * df_dy / grad_sq;

                        // 防止迭代发散脱离当前网格块
                        cx = std::clamp ( cx, b.x0, b.x1 );
                        cy = std::clamp ( cy, b.y0, b.y1 );
                    }

                    // 2. 局部 5 次兜底失败，火力全开启动 L-SHADE
                    if ( !found )
                    {
                        utils::optimization::l_shade_parameters< double, 2 > de_params_local;
                        de_params_local.lower_bounds = { b.x0, b.y0 };
                        de_params_local.upper_bounds = { b.x1, b.y1 };
                        de_params_local.NP_init = de_params.initial_population_size;
                        de_params_local.NP_min = de_params.min_population_size;
                        de_params_local.max_evaluations = de_params.max_evaluations;
                        de_params_local.seed = de_params.seed + static_cast< uint64_t > ( i );

                        // 零干预 TBB (threads=0)，不强制单线程，高级调度自动避免风暴
                        de_params_local.enable_early_exit = false;
                        de_params_local.threads = 0;

                        auto cost_func = [ & ] ( double x, double y ) -> double
                        { return std::abs ( scalar_fn ( x, y ) ); };

                        std::array< double, 2 > best_solution =
                            utils::optimization::l_shade ( cost_func, de_params_local );
                        cx = best_solution[ 0 ];
                        cy = best_solution[ 1 ];

                        // L-SHADE 收敛点执行最终极 Epsilon 区间验证
                        auto verify_ix =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cx - epsilon, cx + epsilon ) );
                        auto verify_iy =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cy - epsilon, cy + epsilon ) );
                        auto verify_res = interval_fn ( verify_ix, verify_iy );

                        if ( utils::detals::possible_root ( verify_res ) &&
                             !utils::detals::is_unbounded ( verify_res ) )
                        {
                            found = true;
                        }
                    }

                    // 3. 根节点有效性汇编
                    if ( found )
                    {
                        local_x.push_back ( cx );
                        local_y.push_back ( cy );
                    }
                }

                // 4. 批处理内存合流 (Span + memcpy 极速穿透)
                if ( !local_x.empty () )
                {
                    std::scoped_lock lock ( out_mutex );
                    out_cloud.x.append ( std::span< const double > ( local_x.begin (), local_x.size () ) );
                    out_cloud.y.append ( std::span< const double > ( local_y.begin (), local_y.size () ) );
                }
            } );
    }

    inline void stuplot_implicit3D ( const utils::StuFunction< double ( double, double, double ) >& scalar_fn,
                                     const utils::StuFunction< utils::IntervalSet< double > (
                                         const utils::IntervalSet< double >&, const utils::IntervalSet< double >&,
                                         const utils::IntervalSet< double >& ) >& interval_fn,
                                     const DAGAssets::LShade& de_params, double x_min, double x_max, double y_min,
                                     double y_max, double z_min, double z_max, double min_block_width,
                                     double min_block_height, double min_block_depth, double epsilon,
                                     DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        // 清理 3D 输出缓冲区
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        // 内部三维物理内存块
        struct Box3D
        {
            double x0, x1, y0, y1, z0, z1;
        };

        // 复用引擎底层的纯栈容器，杜绝堆碎片开销
        utils::TinyVector< Box3D > leaf_tasks;
        utils::TinyVector< Box3D > stack;

        stack.push_back ( { x_min, x_max, y_min, y_max, z_min, z_max } );

        // --- 阶段 1：使用区间算术进行八叉树拓扑剪枝 ---
        while ( !stack.empty () )
        {
            Box3D t = stack.back ();
            stack.pop_back ();

            // 转换区域为三轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto iz = utils::IntervalSet< double > ( utils::Interval< double > ( t.z0, t.z1 ) );
            auto res_ia = interval_fn ( ix, iy, iz );

            // 若本块绝对不可能存在零等值面，直接丢弃（核心加速区）
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 未达空间分辨率下限，继续 8 等分
            if ( ( t.x1 - t.x0 ) > min_block_width || ( t.y1 - t.y0 ) > min_block_height ||
                 ( t.z1 - t.z0 ) > min_block_depth )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;
                double mz = ( t.z0 + t.z1 ) * 0.5;

                // Bottom 4 sub-boxes (z0 到 mz)
                stack.push_back ( { t.x0, mx, t.y0, my, t.z0, mz } );
                stack.push_back ( { mx, t.x1, t.y0, my, t.z0, mz } );
                stack.push_back ( { t.x0, mx, my, t.y1, t.z0, mz } );
                stack.push_back ( { mx, t.x1, my, t.y1, t.z0, mz } );

                // Top 4 sub-boxes (mz 到 z1)
                stack.push_back ( { t.x0, mx, t.y0, my, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, t.y0, my, mz, t.z1 } );
                stack.push_back ( { t.x0, mx, my, t.y1, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, my, t.y1, mz, t.z1 } );
            }
            else
            {
                // 抵达分辨率阈值，推入物理计算列队
                leaf_tasks.push_back ( t );
            }
        }

        if ( leaf_tasks.empty () )
        {
            return;
        }

        // --- 阶段 2：基于 TBB 调度的 3D Newton-LSHADE 融合求解 ---
        oneapi::tbb::parallel_for (
            oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
            [ & ] ( const oneapi::tbb::blocked_range< size_t >& r )
            {
                // 线程局部缓存，彻底剥离全局锁并发竞争
                utils::TinyVector< double > local_x;
                utils::TinyVector< double > local_y;
                utils::TinyVector< double > local_z;

                for ( size_t i = r.begin (); i != r.end (); ++i )
                {
                    const Box3D& b = leaf_tasks[ i ];

                    double cx = ( b.x0 + b.x1 ) * 0.5;
                    double cy = ( b.y0 + b.y1 ) * 0.5;
                    double cz = ( b.z0 + b.z1 ) * 0.5;
                    bool found = false;

                    // 1. 三维牛顿差分正交迭代（限定最大 5 次尝试）
                    for ( int iter = 0; iter < 5; ++iter )
                    {
                        auto verify_ix =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cx - epsilon, cx + epsilon ) );
                        auto verify_iy =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cy - epsilon, cy + epsilon ) );
                        auto verify_iz =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cz - epsilon, cz + epsilon ) );
                        auto verify_res = interval_fn ( verify_ix, verify_iy, verify_iz );

                        // IA 证明已入根的三维体素区间，立停
                        if ( utils::detals::possible_root ( verify_res ) &&
                             !utils::detals::is_unbounded ( verify_res ) )
                        {
                            found = true;
                            break;
                        }

                        // 基于 Epsilon 步长求取三维数值偏导 (法线向量)
                        double f_val = scalar_fn ( cx, cy, cz );
                        double f_x_plus = scalar_fn ( cx + epsilon, cy, cz );
                        double f_x_minus = scalar_fn ( cx - epsilon, cy, cz );
                        double f_y_plus = scalar_fn ( cx, cy + epsilon, cz );
                        double f_y_minus = scalar_fn ( cx, cy - epsilon, cz );
                        double f_z_plus = scalar_fn ( cx, cy, cz + epsilon );
                        double f_z_minus = scalar_fn ( cx, cy, cz - epsilon );

                        double df_dx = ( f_x_plus - f_x_minus ) / ( 2.0 * epsilon );
                        double df_dy = ( f_y_plus - f_y_minus ) / ( 2.0 * epsilon );
                        double df_dz = ( f_z_plus - f_z_minus ) / ( 2.0 * epsilon );

                        double grad_sq = df_dx * df_dx + df_dy * df_dy + df_dz * df_dz;

                        // 若梯度消失 (如处在曲面中心或局部极值点)，中止迭代移交 L-SHADE
                        if ( grad_sq < 1e-12 )
                        {
                            break;
                        }

                        // 沿负法线方向执行向等值面的物理正交投影迭代
                        cx -= f_val * df_dx / grad_sq;
                        cy -= f_val * df_dy / grad_sq;
                        cz -= f_val * df_dz / grad_sq;

                        // 防止高斯发散越界，将游走点限制在当前八叉树子立方体边界内
                        cx = std::clamp ( cx, b.x0, b.x1 );
                        cy = std::clamp ( cy, b.y0, b.y1 );
                        cz = std::clamp ( cz, b.z0, b.z1 );
                    }

                    // 2. 局部 5 次兜底失败，火力全开启动 3D L-SHADE
                    if ( !found )
                    {
                        utils::optimization::l_shade_parameters< double, 3 > de_params_local;
                        de_params_local.lower_bounds = { b.x0, b.y0, b.z0 };
                        de_params_local.upper_bounds = { b.x1, b.y1, b.z1 };
                        de_params_local.NP_init = de_params.initial_population_size;
                        de_params_local.NP_min = de_params.min_population_size;
                        de_params_local.max_evaluations = de_params.max_evaluations;
                        de_params_local.seed = de_params.seed + static_cast< uint64_t > ( i );

                        // 零干预 TBB (threads=0)，信任高级 Work-Stealing 窃取调度，杜绝风暴
                        de_params_local.enable_early_exit = false;
                        de_params_local.threads = 0;

                        auto cost_func = [ & ] ( double x, double y, double z ) -> double
                        { return std::abs ( scalar_fn ( x, y, z ) ); };

                        std::array< double, 3 > best_solution =
                            utils::optimization::l_shade ( cost_func, de_params_local );
                        cx = best_solution[ 0 ];
                        cy = best_solution[ 1 ];
                        cz = best_solution[ 2 ];

                        // L-SHADE 收敛点执行最终极 Epsilon 体素验证
                        auto verify_ix =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cx - epsilon, cx + epsilon ) );
                        auto verify_iy =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cy - epsilon, cy + epsilon ) );
                        auto verify_iz =
                            utils::IntervalSet< double > ( utils::Interval< double > ( cz - epsilon, cz + epsilon ) );
                        auto verify_res = interval_fn ( verify_ix, verify_iy, verify_iz );

                        if ( utils::detals::possible_root ( verify_res ) &&
                             !utils::detals::is_unbounded ( verify_res ) )
                        {
                            found = true;
                        }
                    }

                    // 3. 根节点汇编
                    if ( found )
                    {
                        local_x.push_back ( cx );
                        local_y.push_back ( cy );
                        local_z.push_back ( cz );
                    }
                }

                // 4. 批处理内存合流 (Span + memcpy 极速穿透)
                if ( !local_x.empty () )
                {
                    std::scoped_lock lock ( out_mutex );
                    out_cloud.x.append ( std::span< const double > ( local_x.begin (), local_x.size () ) );
                    out_cloud.y.append ( std::span< const double > ( local_y.begin (), local_y.size () ) );
                    out_cloud.z.append ( std::span< const double > ( local_z.begin (), local_z.size () ) );
                }
            } );
    }
    // =========================================================================
    // 🚀 外部调用主接口：区间算术 (IA) 增强版 Marching Squares 2D
    // =========================================================================
    inline void marchingSquares2DIA (
        const utils::StuFunction< double ( double, double ) >& scalar_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& interval_fn,
        double x_min, double x_max, double y_min, double y_max,
        double step,                      // 粗网格离散步长
        uint32_t max_subdivision_depth,   // 最大自适应 2x2 细分深度
        unsigned int threads,             // 完全透传的用户线程配置
        DAGAssets::LineStrip2D_SoA& out_strip )
    {
        out_strip.x.clear ();
        out_strip.y.clear ();
        std::mutex out_mutex;

        // 内部物理内存块
        struct Box
        {
            double x0, x1, y0, y1;
        };

        // 🚀 核心优化 1：复用引擎底层的纯栈容器，杜绝任何堆碎片开销
        utils::TinyVector< Box > leaf_tasks;
        utils::TinyVector< Box > stack;

        stack.push_back ( { x_min, x_max, y_min, y_max } );

        // 计算 IA 四叉树修剪的下限（粗网格的 10x10 左右范围）
        double target_w = 10.0 * step;
        double target_h = 10.0 * step;

        // --- 阶段 1：使用区间算术进行四叉树拓扑剪枝 ---
        while ( !stack.empty () )
        {
            Box t = stack.back ();
            stack.pop_back ();

            // 转换区域为双轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto res_ia = interval_fn ( ix, iy );

            // 若本块绝对不可能有根，物理剪枝丢弃
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 若区块仍大于 10x10 个 step 大小，继续四叉细分
            if ( ( t.x1 - t.x0 ) > target_w || ( t.y1 - t.y0 ) > target_h )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;

                stack.push_back ( { t.x0, mx, t.y0, my } );
                stack.push_back ( { mx, t.x1, t.y0, my } );
                stack.push_back ( { t.x0, mx, my, t.y1 } );
                stack.push_back ( { mx, t.x1, my, t.y1 } );
            }
            else
            {
                // 抵达 10x10 分辨率阈值，推入物理计算列队
                leaf_tasks.push_back ( t );
            }
        }

        if ( leaf_tasks.empty () )
        {
            return;
        }

        // --- 阶段 2：并行展开叶子任务，稍微扩充 5% 并调用纯 Marching Squares ---
        unsigned int max_threads = threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const oneapi::tbb::blocked_range< size_t >& r )
                                            {
                                                // 🚀 核心优化 2：Task 级的局部缓存，聚积该 Chunk 下所有小块的结果
                                                DAGAssets::LineStrip2D_SoA local_strip;

                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const Box& b = leaf_tasks[ i ];

                                                    // 扩张 5% 范围，确保等值线在网格交界处无缝衔接
                                                    double w = b.x1 - b.x0;
                                                    double h = b.y1 - b.y0;
                                                    double dx = w * 0.05;
                                                    double dy = h * 0.05;

                                                    // 强制钳制在全局边界内，避免计算域外异常求值
                                                    double ex0 = std::max ( x_min, b.x0 - dx );
                                                    double ex1 = std::min ( x_max, b.x1 + dx );
                                                    double ey0 = std::max ( y_min, b.y0 - dy );
                                                    double ey1 = std::min ( y_max, b.y1 + dy );

                                                    DAGAssets::LineStrip2D_SoA temp_strip;

                                                    // 🚀 核心纠正：绝对信任 TBB 的高级调度算法！
                                                    // 完完整整透传外部的 threads 参数，底层将优雅地处理 Task Arena
                                                    // 嵌套复用
                                                    marchingSquares2D ( scalar_fn, ex0, ex1, ey0, ey1, step,
                                                                        max_subdivision_depth,
                                                                        threads,   // <--- 无条件透传用户指定的并发度
                                                                        temp_strip );

                                                    // 将当前小块生成的几何吸入当前 Chunk 的局部缓冲中
                                                    if ( !temp_strip.x.empty () )
                                                    {
                                                        local_strip.x.append ( std::span< const double > (
                                                            temp_strip.x.begin (), temp_strip.x.size () ) );
                                                        local_strip.y.append ( std::span< const double > (
                                                            temp_strip.y.begin (), temp_strip.y.size () ) );
                                                    }
                                                }

                                                // 🚀 核心优化 3：每个 Chunk 仅触发 1 次锁竞争，利用 Span + memcpy
                                                // 瞬间吸入全局缓冲
                                                if ( !local_strip.x.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_strip.x.append ( std::span< const double > (
                                                        local_strip.x.begin (), local_strip.x.size () ) );
                                                    out_strip.y.append ( std::span< const double > (
                                                        local_strip.y.begin (), local_strip.y.size () ) );
                                                }
                                            } );
            } );
    }
    // =========================================================================
    // 🚀 外部调用主接口：区间算术 (IA) 增强版 Marching Cubes 3D
    // =========================================================================
    inline void marchingCubes3DIA ( const utils::StuFunction< double ( double, double, double ) >& scalar_fn,
                                    const utils::StuFunction< utils::IntervalSet< double > (
                                        const utils::IntervalSet< double >&, const utils::IntervalSet< double >&,
                                        const utils::IntervalSet< double >& ) >& interval_fn,
                                    double x_min, double x_max, double y_min, double y_max, double z_min, double z_max,
                                    double step,            // 粗网格离散步长
                                    unsigned int threads,   // 完全透传的用户线程配置
                                    DAGAssets::TriangleMesh3D_SoA& out_mesh )
    {
        out_mesh.x.clear ();
        out_mesh.y.clear ();
        out_mesh.z.clear ();
        out_mesh.indices.clear ();
        std::mutex out_mutex;

        // 内部三维物理内存块
        struct Box3D
        {
            double x0, x1, y0, y1, z0, z1;
        };

        // 🚀 核心优化 1：复用引擎底层的纯栈容器，杜绝任何堆碎片开销
        utils::TinyVector< Box3D > leaf_tasks;
        utils::TinyVector< Box3D > stack;

        stack.push_back ( { x_min, x_max, y_min, y_max, z_min, z_max } );

        // 计算 IA 八叉树修剪的下限（粗网格的 10x10x10 左右范围）
        double target_w = 10.0 * step;
        double target_h = 10.0 * step;
        double target_d = 10.0 * step;

        // --- 阶段 1：使用区间算术进行八叉树拓扑剪枝 ---
        while ( !stack.empty () )
        {
            Box3D t = stack.back ();
            stack.pop_back ();

            // 转换区域为三轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto iz = utils::IntervalSet< double > ( utils::Interval< double > ( t.z0, t.z1 ) );
            auto res_ia = interval_fn ( ix, iy, iz );

            // 若本块绝对不可能有根，物理剪枝丢弃（3D 空间下此步剔除收益巨大）
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 若区块尺寸仍大于下限，继续八叉细分
            if ( ( t.x1 - t.x0 ) > target_w || ( t.y1 - t.y0 ) > target_h || ( t.z1 - t.z0 ) > target_d )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;
                double mz = ( t.z0 + t.z1 ) * 0.5;

                // 底部 4 个子格 (z0 -> mz)
                stack.push_back ( { t.x0, mx, t.y0, my, t.z0, mz } );
                stack.push_back ( { mx, t.x1, t.y0, my, t.z0, mz } );
                stack.push_back ( { t.x0, mx, my, t.y1, t.z0, mz } );
                stack.push_back ( { mx, t.x1, my, t.y1, t.z0, mz } );

                // 顶部 4 个子格 (mz -> z1)
                stack.push_back ( { t.x0, mx, t.y0, my, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, t.y0, my, mz, t.z1 } );
                stack.push_back ( { t.x0, mx, my, t.y1, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, my, t.y1, mz, t.z1 } );
            }
            else
            {
                // 抵达分辨率阈值，推入物理计算列队
                leaf_tasks.push_back ( t );
            }
        }

        if ( leaf_tasks.empty () )
        {
            return;
        }

        // --- 阶段 2：并行展开叶子任务，稍微扩充 5% 并调用纯 Marching Cubes ---
        unsigned int max_threads = threads;
        if ( max_threads == 0 )
        {
            max_threads = oneapi::tbb::info::default_concurrency ();
        }

        oneapi::tbb::task_arena arena ( max_threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for (
                    oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                    [ & ] ( const oneapi::tbb::blocked_range< size_t >& r )
                    {
                        // 🚀 核心优化 2：Task 级的局部缓存与复用。
                        // 避免每次进入循环都申请和释放三角网格内存。
                        DAGAssets::TriangleMesh3D_SoA local_mesh;
                        DAGAssets::TriangleMesh3D_SoA temp_mesh;

                        for ( size_t i = r.begin (); i != r.end (); ++i )
                        {
                            const Box3D& b = leaf_tasks[ i ];

                            // 扩张 5% 范围，确保等值面在 3D 网格交界处无缝闭合
                            double w = b.x1 - b.x0;
                            double h = b.y1 - b.y0;
                            double d = b.z1 - b.z0;
                            double dx = w * 0.05;
                            double dy = h * 0.05;
                            double dz = d * 0.05;

                            // 强制钳制在全局边界内，避免域外异常越界
                            double ex0 = std::max ( x_min, b.x0 - dx );
                            double ex1 = std::min ( x_max, b.x1 + dx );
                            double ey0 = std::max ( y_min, b.y0 - dy );
                            double ey1 = std::min ( y_max, b.y1 + dy );
                            double ez0 = std::max ( z_min, b.z0 - dz );
                            double ez1 = std::min ( z_max, b.z1 + dz );

                            // O(1) 极速清零结构体，物理容量维持不变
                            temp_mesh.x.clear ();
                            temp_mesh.y.clear ();
                            temp_mesh.z.clear ();
                            temp_mesh.indices.clear ();

                            // 🚀 无条件透传 threads 给底层的 Marching Cubes
                            marchingCubes3D ( scalar_fn, ex0, ex1, ey0, ey1, ez0, ez1, step,
                                              threads,   // <--- 信任 TBB 调度
                                              temp_mesh );

                            // 🚀 核心逻辑 3：安全平移三角形顶点索引 (Index Base Offset)
                            if ( !temp_mesh.x.empty () )
                            {
                                uint32_t local_base = static_cast< uint32_t > ( local_mesh.x.size () );

                                local_mesh.x.append (
                                    std::span< const double > ( temp_mesh.x.begin (), temp_mesh.x.size () ) );
                                local_mesh.y.append (
                                    std::span< const double > ( temp_mesh.y.begin (), temp_mesh.y.size () ) );
                                local_mesh.z.append (
                                    std::span< const double > ( temp_mesh.z.begin (), temp_mesh.z.size () ) );

                                // 对临时网格中的每一个面片索引，加上已在 local 缓冲区的顶点数
                                for ( uint32_t idx : temp_mesh.indices )
                                {
                                    local_mesh.indices.push_back ( local_base + idx );
                                }
                            }
                        }

                        // 🚀 核心优化 4：仅在 Chunk 结束时参与全局自旋锁竞争
                        if ( !local_mesh.x.empty () )
                        {
                            std::scoped_lock lock ( out_mutex );

                            uint32_t global_base = static_cast< uint32_t > ( out_mesh.x.size () );

                            out_mesh.x.append (
                                std::span< const double > ( local_mesh.x.begin (), local_mesh.x.size () ) );
                            out_mesh.y.append (
                                std::span< const double > ( local_mesh.y.begin (), local_mesh.y.size () ) );
                            out_mesh.z.append (
                                std::span< const double > ( local_mesh.z.begin (), local_mesh.z.size () ) );

                            // 再次执行全局索引平移
                            for ( uint32_t idx : local_mesh.indices )
                            {
                                out_mesh.indices.push_back ( global_base + idx );
                            }
                        }
                    } );
            } );
    }
    inline void stuplot_implicit2D_IA_pure (
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& interval_fn,
        double x_min, double x_max, double y_min, double y_max, double min_block_width, double min_block_height,
        DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        // 无锁清理输出缓冲
        out_cloud.x.clear ();
        out_cloud.y.clear ();

        struct Box
        {
            double x0, x1, y0, y1;
        };

        // 纯栈容器，杜绝 std::stack 带来的默认 std::deque 碎片分配
        utils::TinyVector< Box > stack;
        stack.push_back ( { x_min, x_max, y_min, y_max } );

        while ( !stack.empty () )
        {
            Box t = stack.back ();
            stack.pop_back ();

            // 转换区域为双轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto res_ia = interval_fn ( ix, iy );

            // 物理剪枝：数学上证明该矩形内绝对不存在 f(x,y)=0
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 若区块仍大于用户指定的分辨率，继续四叉细分
            if ( ( t.x1 - t.x0 ) > min_block_width || ( t.y1 - t.y0 ) > min_block_height )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;

                // 压入四个子象限
                stack.push_back ( { t.x0, mx, t.y0, my } );
                stack.push_back ( { mx, t.x1, t.y0, my } );
                stack.push_back ( { t.x0, mx, my, t.y1 } );
                stack.push_back ( { mx, t.x1, my, t.y1 } );
            }
            else
            {
                // 抵达极限分辨率且包含根区，将区块中心点直接推入 SoA 扁平数组
                out_cloud.x.push_back ( ( t.x0 + t.x1 ) * 0.5 );
                out_cloud.y.push_back ( ( t.y0 + t.y1 ) * 0.5 );
            }
        }
    }


    // =========================================================================
    // 🚀 外部调用主接口：纯区间算术 (IA) 3D 隐式点云生成器 (单核心版)
    // =========================================================================
    /**
     * @brief 纯区间算术 3D 隐式等值面点云绘图器 (完全串行)
     *
     * 核心特性：
     * 1. 极致的三维八叉树空间剪枝，只追逐可能存在等值面的拓扑边缘。
     * 2. 没有任何求导、没有任何近似算子，绝对可靠的纯数学空间细分。
     */
    inline void stuplot_implicit3D_IA_pure (
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& interval_fn,
        double x_min, double x_max, double y_min, double y_max, double z_min, double z_max, double min_block_width,
        double min_block_height, double min_block_depth, DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        // 无锁清理输出缓冲
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();

        struct Box3D
        {
            double x0, x1, y0, y1, z0, z1;
        };

        // 极速单线程堆外栈
        utils::TinyVector< Box3D > stack;
        stack.push_back ( { x_min, x_max, y_min, y_max, z_min, z_max } );

        while ( !stack.empty () )
        {
            Box3D t = stack.back ();
            stack.pop_back ();

            // 转换区域为三轴区间集
            auto ix = utils::IntervalSet< double > ( utils::Interval< double > ( t.x0, t.x1 ) );
            auto iy = utils::IntervalSet< double > ( utils::Interval< double > ( t.y0, t.y1 ) );
            auto iz = utils::IntervalSet< double > ( utils::Interval< double > ( t.z0, t.z1 ) );
            auto res_ia = interval_fn ( ix, iy, iz );

            // 物理剪枝：数学上证明该体素立方体内绝对不存在 f(x,y,z)=0
            if ( !utils::detals::possible_root ( res_ia ) )
            {
                continue;
            }

            // 若区块仍大于指定的三维分辨率，继续八叉细分
            if ( ( t.x1 - t.x0 ) > min_block_width || ( t.y1 - t.y0 ) > min_block_height ||
                 ( t.z1 - t.z0 ) > min_block_depth )
            {
                double mx = ( t.x0 + t.x1 ) * 0.5;
                double my = ( t.y0 + t.y1 ) * 0.5;
                double mz = ( t.z0 + t.z1 ) * 0.5;

                // 压入底部 4 个子格
                stack.push_back ( { t.x0, mx, t.y0, my, t.z0, mz } );
                stack.push_back ( { mx, t.x1, t.y0, my, t.z0, mz } );
                stack.push_back ( { t.x0, mx, my, t.y1, t.z0, mz } );
                stack.push_back ( { mx, t.x1, my, t.y1, t.z0, mz } );

                // 压入顶部 4 个子格
                stack.push_back ( { t.x0, mx, t.y0, my, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, t.y0, my, mz, t.z1 } );
                stack.push_back ( { t.x0, mx, my, t.y1, mz, t.z1 } );
                stack.push_back ( { mx, t.x1, my, t.y1, mz, t.z1 } );
            }
            else
            {
                // 抵达极限三维分辨率且包含等值面，将体素中心点直接推入点云缓冲
                out_cloud.x.push_back ( ( t.x0 + t.x1 ) * 0.5 );
                out_cloud.y.push_back ( ( t.y0 + t.y1 ) * 0.5 );
                out_cloud.z.push_back ( ( t.z0 + t.z1 ) * 0.5 );
            }
        }
    }
    // =========================================================================
    // 🚀 辅助函数：极速一维包围盒交叠检测 (O(N) 扫描 IntervalSet，N 通常为 1~2)
    // =========================================================================
    inline bool intersects_range ( const utils::IntervalSet< double >& set, double v_min, double v_max ) noexcept
    {
        if ( set.is_poisoned () || set.intervals.empty () )
        {
            return false;
        }
        for ( const auto& iv : set.intervals )
        {
            if ( iv.lower <= v_max && iv.upper >= v_min )
            {
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // 🚀 1. 纯区间算术 (IA) 2D 参数曲线点云生成器 (二叉树细分 t)
    // =========================================================================
    /**
     * @brief 完全串行的 2D 参数曲线 x=f(t), y=g(t) 绘图器
     */
    inline void stuplot_parametric2D_IA_pure (
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& x_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& y_fn,
        double t_min, double t_max, double x_min, double x_max, double y_min, double y_max, double min_t_step,
        DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();

        struct BoxT
        {
            double t0, t1;
        };

        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();

            auto it = utils::IntervalSet< double > ( utils::Interval< double > ( b.t0, b.t1 ) );

            // X 轴越界修剪
            auto ix = x_fn ( it );
            if ( !intersects_range ( ix, x_min, x_max ) )
                continue;

            // Y 轴越界修剪
            auto iy = y_fn ( it );
            if ( !intersects_range ( iy, y_min, y_max ) )
                continue;

            // 若区块仍大于指定分辨率，二叉细分 t 域
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                // 抵达极限分辨率且在视野内，严格求解中心点坐标推入点云
                double mt = ( b.t0 + b.t1 ) * 0.5;
                auto exact_t = utils::IntervalSet< double > ( utils::Interval< double > ( mt, mt ) );

                auto cx = x_fn ( exact_t ).to_hull ();
                auto cy = y_fn ( exact_t ).to_hull ();

                if ( !cx.is_poisoned () && !cy.is_poisoned () )
                {
                    out_cloud.x.push_back ( cx.center () );
                    out_cloud.y.push_back ( cy.center () );
                }
            }
        }
    }


    // =========================================================================
    // 🚀 2. 纯区间算术 (IA) 3D 参数曲线点云生成器 (二叉树细分 t)
    // =========================================================================
    /**
     * @brief 完全串行的 3D 参数曲线 x=f(t), y=g(t), z=h(t) 绘图器
     */
    inline void stuplot_parametric3D_IA_pure (
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& x_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& y_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& z_fn,
        double t_min, double t_max, double x_min, double x_max, double y_min, double y_max, double z_min, double z_max,
        double min_t_step, DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();

        struct BoxT
        {
            double t0, t1;
        };

        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();

            auto it = utils::IntervalSet< double > ( utils::Interval< double > ( b.t0, b.t1 ) );

            auto ix = x_fn ( it );
            if ( !intersects_range ( ix, x_min, x_max ) )
                continue;

            auto iy = y_fn ( it );
            if ( !intersects_range ( iy, y_min, y_max ) )
                continue;

            auto iz = z_fn ( it );
            if ( !intersects_range ( iz, z_min, z_max ) )
                continue;

            // 二叉树一维切分
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                auto exact_t = utils::IntervalSet< double > ( utils::Interval< double > ( mt, mt ) );

                auto cx = x_fn ( exact_t ).to_hull ();
                auto cy = y_fn ( exact_t ).to_hull ();
                auto cz = z_fn ( exact_t ).to_hull ();

                if ( !cx.is_poisoned () && !cy.is_poisoned () && !cz.is_poisoned () )
                {
                    out_cloud.x.push_back ( cx.center () );
                    out_cloud.y.push_back ( cy.center () );
                    out_cloud.z.push_back ( cz.center () );
                }
            }
        }
    }


    // =========================================================================
    // 🚀 3. 纯区间算术 (IA) 3D 参数曲面点云生成器 (四叉树细分 u, v)
    // =========================================================================
    /**
     * @brief 完全串行的 3D 参数曲面 x=f(u,v), y=g(u,v), z=h(u,v) 绘图器
     */
    inline void stuplot_parametricSurface3D_IA_pure (
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& x_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& y_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& z_fn,
        double u_min, double u_max, double v_min, double v_max, double x_min, double x_max, double y_min, double y_max,
        double z_min, double z_max, double min_u_step, double min_v_step, DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();

        struct BoxUV
        {
            double u0, u1, v0, v1;
        };

        utils::TinyVector< BoxUV > stack;
        stack.push_back ( { u_min, u_max, v_min, v_max } );

        while ( !stack.empty () )
        {
            BoxUV b = stack.back ();
            stack.pop_back ();

            auto iu = utils::IntervalSet< double > ( utils::Interval< double > ( b.u0, b.u1 ) );
            auto iv = utils::IntervalSet< double > ( utils::Interval< double > ( b.v0, b.v1 ) );

            // 对 u,v 域的曲面包围盒进行精确相交检验
            auto ix = x_fn ( iu, iv );
            if ( !intersects_range ( ix, x_min, x_max ) )
                continue;

            auto iy = y_fn ( iu, iv );
            if ( !intersects_range ( iy, y_min, y_max ) )
                continue;

            auto iz = z_fn ( iu, iv );
            if ( !intersects_range ( iz, z_min, z_max ) )
                continue;

            // 四叉树细分参数域
            if ( ( b.u1 - b.u0 ) > min_u_step || ( b.v1 - b.v0 ) > min_v_step )
            {
                double mu = ( b.u0 + b.u1 ) * 0.5;
                double mv = ( b.v0 + b.v1 ) * 0.5;

                stack.push_back ( { b.u0, mu, b.v0, mv } );
                stack.push_back ( { mu, b.u1, b.v0, mv } );
                stack.push_back ( { b.u0, mu, mv, b.v1 } );
                stack.push_back ( { mu, b.u1, mv, b.v1 } );
            }
            else
            {
                double mu = ( b.u0 + b.u1 ) * 0.5;
                double mv = ( b.v0 + b.v1 ) * 0.5;

                auto exact_u = utils::IntervalSet< double > ( utils::Interval< double > ( mu, mu ) );
                auto exact_v = utils::IntervalSet< double > ( utils::Interval< double > ( mv, mv ) );

                auto cx = x_fn ( exact_u, exact_v ).to_hull ();
                auto cy = y_fn ( exact_u, exact_v ).to_hull ();
                auto cz = z_fn ( exact_u, exact_v ).to_hull ();

                if ( !cx.is_poisoned () && !cy.is_poisoned () && !cz.is_poisoned () )
                {
                    out_cloud.x.push_back ( cx.center () );
                    out_cloud.y.push_back ( cy.center () );
                    out_cloud.z.push_back ( cz.center () );
                }
            }
        }
    }
    namespace detail
    {
        // 🚀 辅助：计算 AABB 中心距离的平方 (2D/3D 通用模板)
        template < size_t Dim >
        inline double calc_sq_dist_to_center ( const std::array< double, Dim >& p,
                                               const std::array< double, Dim >& center )
        {
            double dist_sq = 0.0;
            for ( size_t i = 0; i < Dim; ++i )
            {
                double d = p[ i ] - center[ i ];
                dist_sq += d * d;
            }
            return dist_sq;
        }
    }   // namespace detail

    // =========================================================================
    // 🚀 1. 2D 参数曲线混合绘图器 (二叉树细分 t)
    // =========================================================================
    inline void stuplot_parametric2D_Hybrid (
        const utils::StuFunction< double ( double ) >& x_fn, const utils::StuFunction< double ( double ) >& y_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& x_ia,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& y_ia,
        const DAGAssets::LShade& de_params, double t_min, double t_max, double x_center,
        double y_center,   // AABB 中心目标
        double min_t_step, double epsilon, unsigned int threads, DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        std::mutex out_mutex;

        struct BoxT
        {
            double t0, t1;
        };
        utils::TinyVector< BoxT > leaf_tasks;
        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        // 阶段 1: 二叉树细分参数域
        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        // 阶段 2: 并行 L-SHADE 优化 + IA 验证
        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for (
                    oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                    [ & ] ( const auto& r )
                    {
                        utils::TinyVector< double > lx, ly;
                        for ( size_t i = r.begin (); i != r.end (); ++i )
                        {
                            const auto& b = leaf_tasks[ i ];

                            // 目标：最小化到中心点的距离
                            auto cost_func = [ & ] ( double t ) -> double
                            { return std::pow ( x_fn ( t ) - x_center, 2 ) + std::pow ( y_fn ( t ) - y_center, 2 ); };

                            utils::optimization::l_shade_parameters< double, 1 > lp;
                            lp.lower_bounds = { b.t0 };
                            lp.upper_bounds = { b.t1 };
                            lp.NP_init = de_params.initial_population_size;
                            lp.NP_min = de_params.min_population_size;
                            lp.max_evaluations = de_params.max_evaluations;
                            lp.seed = de_params.seed + i;
                            lp.enable_early_exit = false;
                            lp.threads = 0;

                            auto best_t = utils::optimization::l_shade ( cost_func, lp )[ 0 ];

                            // IA 验证
                            auto it = utils::IntervalSet< double > (
                                utils::Interval< double > ( best_t - epsilon, best_t + epsilon ) );
                            auto res_x = x_ia ( it );
                            auto res_y = y_ia ( it );

                            if ( utils::detals::possible_root ( res_x ) && !utils::detals::is_unbounded ( res_x ) &&
                                 utils::detals::possible_root ( res_y ) && !utils::detals::is_unbounded ( res_y ) )
                            {
                                lx.push_back ( x_fn ( best_t ) );
                                ly.push_back ( y_fn ( best_t ) );
                            }
                        }
                        if ( !lx.empty () )
                        {
                            std::scoped_lock lock ( out_mutex );
                            out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                            out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                        }
                    } );
            } );
    }

    // =========================================================================
    // 🚀 2. 3D 参数曲线混合绘图器 (二叉树细分 t)
    // =========================================================================
    inline void stuplot_parametric3D_Hybrid (
        const utils::StuFunction< double ( double ) >& x_fn, const utils::StuFunction< double ( double ) >& y_fn,
        const utils::StuFunction< double ( double ) >& z_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& x_ia,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& y_ia,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) >& z_ia,
        const DAGAssets::LShade& de_params, double t_min, double t_max, double x_center, double y_center,
        double z_center, double min_t_step, double epsilon, unsigned int threads,
        DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        struct BoxT
        {
            double t0, t1;
        };
        utils::TinyVector< BoxT > leaf_tasks;
        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const auto& r )
                                            {
                                                utils::TinyVector< double > lx, ly, lz;
                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const auto& b = leaf_tasks[ i ];
                                                    auto cost_func = [ & ] ( double t ) -> double
                                                    {
                                                        return std::pow ( x_fn ( t ) - x_center, 2 ) +
                                                               std::pow ( y_fn ( t ) - y_center, 2 ) +
                                                               std::pow ( z_fn ( t ) - z_center, 2 );
                                                    };
                                                    utils::optimization::l_shade_parameters< double, 1 > lp;
                                                    lp.lower_bounds = { b.t0 };
                                                    lp.upper_bounds = { b.t1 };
                                                    lp.NP_init = de_params.initial_population_size;
                                                    lp.max_evaluations = de_params.max_evaluations;
                                                    lp.seed = de_params.seed + i;
                                                    lp.threads = 0;

                                                    auto best_t = utils::optimization::l_shade ( cost_func, lp )[ 0 ];
                                                    auto it = utils::IntervalSet< double > ( utils::Interval< double > (
                                                        best_t - epsilon, best_t + epsilon ) );
                                                    if ( utils::detals::possible_root ( x_ia ( it ) ) &&
                                                         utils::detals::possible_root ( y_ia ( it ) ) &&
                                                         utils::detals::possible_root ( z_ia ( it ) ) )
                                                    {
                                                        lx.push_back ( x_fn ( best_t ) );
                                                        ly.push_back ( y_fn ( best_t ) );
                                                        lz.push_back ( z_fn ( best_t ) );
                                                    }
                                                }
                                                if ( !lx.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                                                    out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                                                    out_cloud.z.append ( std::span ( lz.data (), lz.size () ) );
                                                }
                                            } );
            } );
    }

    // =========================================================================
    // 🚀 3. 3D 参数曲面混合绘图器 (四叉树细分 u, v)
    // =========================================================================
    inline void stuplot_parametricSurface3D_Hybrid (
        const utils::StuFunction< double ( double, double ) >& x_fn,
        const utils::StuFunction< double ( double, double ) >& y_fn,
        const utils::StuFunction< double ( double, double ) >& z_fn,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& x_ia,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& y_ia,
        const utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                 const utils::IntervalSet< double >& ) >& z_ia,
        const DAGAssets::LShade& de_params, double u_min, double u_max, double v_min, double v_max, double x_center,
        double y_center, double z_center, double min_u_step, double min_v_step, double epsilon, unsigned int threads,
        DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        struct BoxUV
        {
            double u0, u1, v0, v1;
        };
        utils::TinyVector< BoxUV > leaf_tasks;
        utils::TinyVector< BoxUV > stack;
        stack.push_back ( { u_min, u_max, v_min, v_max } );

        // 阶段 1: 参数域四叉树细分
        while ( !stack.empty () )
        {
            BoxUV b = stack.back ();
            stack.pop_back ();
            if ( ( b.u1 - b.u0 ) > min_u_step || ( b.v1 - b.v0 ) > min_v_step )
            {
                double mu = ( b.u0 + b.u1 ) * 0.5;
                double mv = ( b.v0 + b.v1 ) * 0.5;
                stack.push_back ( { b.u0, mu, b.v0, mv } );
                stack.push_back ( { mu, b.u1, b.v0, mv } );
                stack.push_back ( { b.u0, mu, mv, b.v1 } );
                stack.push_back ( { mu, b.u1, mv, b.v1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const auto& r )
                                            {
                                                utils::TinyVector< double > lx, ly, lz;
                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const auto& b = leaf_tasks[ i ];
                                                    auto cost_func = [ & ] ( double u, double v ) -> double
                                                    {
                                                        return std::pow ( x_fn ( u, v ) - x_center, 2 ) +
                                                               std::pow ( y_fn ( u, v ) - y_center, 2 ) +
                                                               std::pow ( z_fn ( u, v ) - z_center, 2 );
                                                    };
                                                    utils::optimization::l_shade_parameters< double, 2 > lp;
                                                    lp.lower_bounds = { b.u0, b.v0 };
                                                    lp.upper_bounds = { b.u1, b.v1 };
                                                    lp.NP_init = de_params.initial_population_size;
                                                    lp.max_evaluations = de_params.max_evaluations;
                                                    lp.seed = de_params.seed + i;
                                                    lp.threads = 0;

                                                    auto res = utils::optimization::l_shade ( cost_func, lp );
                                                    double bu = res[ 0 ], bv = res[ 1 ];

                                                    auto iu = utils::IntervalSet< double > (
                                                        utils::Interval< double > ( bu - epsilon, bu + epsilon ) );
                                                    auto iv = utils::IntervalSet< double > (
                                                        utils::Interval< double > ( bv - epsilon, bv + epsilon ) );

                                                    if ( utils::detals::possible_root ( x_ia ( iu, iv ) ) &&
                                                         utils::detals::possible_root ( y_ia ( iu, iv ) ) &&
                                                         utils::detals::possible_root ( z_ia ( iu, iv ) ) )
                                                    {
                                                        lx.push_back ( x_fn ( bu, bv ) );
                                                        ly.push_back ( y_fn ( bu, bv ) );
                                                        lz.push_back ( z_fn ( bu, bv ) );
                                                    }
                                                }
                                                if ( !lx.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                                                    out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                                                    out_cloud.z.append ( std::span ( lz.data (), lz.size () ) );
                                                }
                                            } );
            } );
    }
    inline void stuplot_parametric2D_LShade_pure ( const utils::StuFunction< double ( double ) >& x_fn,
                                                   const utils::StuFunction< double ( double ) >& y_fn,
                                                   const DAGAssets::LShade& de_params, double t_min, double t_max,
                                                   double x_target, double y_target, double min_t_step,
                                                   unsigned int threads, DAGAssets::PointCloud2D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        std::mutex out_mutex;

        struct BoxT
        {
            double t0, t1;
        };
        utils::TinyVector< BoxT > leaf_tasks;
        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        // 阶段 1: 纯参数域二叉细分
        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        // 阶段 2: 并行搜索每个局部块的最优解
        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const auto& r )
                                            {
                                                utils::TinyVector< double > lx, ly;
                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const auto& b = leaf_tasks[ i ];

                                                    auto cost_func = [ & ] ( double t ) -> double
                                                    {
                                                        double dx = x_fn ( t ) - x_target;
                                                        double dy = y_fn ( t ) - y_target;
                                                        return dx * dx + dy * dy;
                                                    };

                                                    utils::optimization::l_shade_parameters< double, 1 > lp;
                                                    lp.lower_bounds = { b.t0 };
                                                    lp.upper_bounds = { b.t1 };
                                                    lp.NP_init = de_params.initial_population_size;
                                                    lp.NP_min = de_params.min_population_size;
                                                    lp.max_evaluations = de_params.max_evaluations;
                                                    lp.seed = de_params.seed + i;
                                                    lp.enable_early_exit = false;
                                                    lp.threads = 0;

                                                    auto best_t = utils::optimization::l_shade ( cost_func, lp )[ 0 ];
                                                    lx.push_back ( x_fn ( best_t ) );
                                                    ly.push_back ( y_fn ( best_t ) );
                                                }
                                                if ( !lx.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                                                    out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                                                }
                                            } );
            } );
    }

    // =========================================================================
    // 🚀 2. 3D 参数曲线纯 L-SHADE 绘图器 (二叉树细分 t)
    // =========================================================================
    inline void stuplot_parametric3D_LShade_pure ( const utils::StuFunction< double ( double ) >& x_fn,
                                                   const utils::StuFunction< double ( double ) >& y_fn,
                                                   const utils::StuFunction< double ( double ) >& z_fn,
                                                   const DAGAssets::LShade& de_params, double t_min, double t_max,
                                                   double x_target, double y_target, double z_target, double min_t_step,
                                                   unsigned int threads, DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        struct BoxT
        {
            double t0, t1;
        };
        utils::TinyVector< BoxT > leaf_tasks;
        utils::TinyVector< BoxT > stack;
        stack.push_back ( { t_min, t_max } );

        while ( !stack.empty () )
        {
            BoxT b = stack.back ();
            stack.pop_back ();
            if ( ( b.t1 - b.t0 ) > min_t_step )
            {
                double mt = ( b.t0 + b.t1 ) * 0.5;
                stack.push_back ( { b.t0, mt } );
                stack.push_back ( { mt, b.t1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const auto& r )
                                            {
                                                utils::TinyVector< double > lx, ly, lz;
                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const auto& b = leaf_tasks[ i ];
                                                    auto cost_func = [ & ] ( double t ) -> double
                                                    {
                                                        double dx = x_fn ( t ) - x_target;
                                                        double dy = y_fn ( t ) - y_target;
                                                        double dz = z_fn ( t ) - z_target;
                                                        return dx * dx + dy * dy + dz * dz;
                                                    };
                                                    utils::optimization::l_shade_parameters< double, 1 > lp;
                                                    lp.lower_bounds = { b.t0 };
                                                    lp.upper_bounds = { b.t1 };
                                                    lp.NP_init = de_params.initial_population_size;
                                                    lp.max_evaluations = de_params.max_evaluations;
                                                    lp.seed = de_params.seed + i;
                                                    lp.threads = 0;

                                                    auto best_t = utils::optimization::l_shade ( cost_func, lp )[ 0 ];
                                                    lx.push_back ( x_fn ( best_t ) );
                                                    ly.push_back ( y_fn ( best_t ) );
                                                    lz.push_back ( z_fn ( best_t ) );
                                                }
                                                if ( !lx.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                                                    out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                                                    out_cloud.z.append ( std::span ( lz.data (), lz.size () ) );
                                                }
                                            } );
            } );
    }

    // =========================================================================
    // 🚀 3. 3D 参数曲面纯 L-SHADE 绘图器 (四叉树细分 u, v)
    // =========================================================================
    inline void stuplot_parametricSurface3D_LShade_pure ( const utils::StuFunction< double ( double, double ) >& x_fn,
                                                          const utils::StuFunction< double ( double, double ) >& y_fn,
                                                          const utils::StuFunction< double ( double, double ) >& z_fn,
                                                          const DAGAssets::LShade& de_params, double u_min,
                                                          double u_max, double v_min, double v_max, double x_target,
                                                          double y_target, double z_target, double min_u_step,
                                                          double min_v_step, unsigned int threads,
                                                          DAGAssets::PointCloud3D_SoA& out_cloud )
    {
        out_cloud.x.clear ();
        out_cloud.y.clear ();
        out_cloud.z.clear ();
        std::mutex out_mutex;

        struct BoxUV
        {
            double u0, u1, v0, v1;
        };
        utils::TinyVector< BoxUV > leaf_tasks;
        utils::TinyVector< BoxUV > stack;
        stack.push_back ( { u_min, u_max, v_min, v_max } );

        // 阶段 1: 参数空间四叉细分
        while ( !stack.empty () )
        {
            BoxUV b = stack.back ();
            stack.pop_back ();
            if ( ( b.u1 - b.u0 ) > min_u_step || ( b.v1 - b.v0 ) > min_v_step )
            {
                double mu = ( b.u0 + b.u1 ) * 0.5;
                double mv = ( b.v0 + b.v1 ) * 0.5;
                stack.push_back ( { b.u0, mu, b.v0, mv } );
                stack.push_back ( { mu, b.u1, b.v0, mv } );
                stack.push_back ( { b.u0, mu, mv, b.v1 } );
                stack.push_back ( { mu, b.u1, mv, b.v1 } );
            }
            else
            {
                leaf_tasks.push_back ( b );
            }
        }

        oneapi::tbb::task_arena arena ( threads == 0 ? oneapi::tbb::info::default_concurrency () : threads );
        arena.execute (
            [ & ] ()
            {
                oneapi::tbb::parallel_for ( oneapi::tbb::blocked_range< size_t > ( 0, leaf_tasks.size () ),
                                            [ & ] ( const auto& r )
                                            {
                                                utils::TinyVector< double > lx, ly, lz;
                                                for ( size_t i = r.begin (); i != r.end (); ++i )
                                                {
                                                    const auto& b = leaf_tasks[ i ];
                                                    auto cost_func = [ & ] ( double u, double v ) -> double
                                                    {
                                                        double dx = x_fn ( u, v ) - x_target;
                                                        double dy = y_fn ( u, v ) - y_target;
                                                        double dz = z_fn ( u, v ) - z_target;
                                                        return dx * dx + dy * dy + dz * dz;
                                                    };
                                                    utils::optimization::l_shade_parameters< double, 2 > lp;
                                                    lp.lower_bounds = { b.u0, b.v0 };
                                                    lp.upper_bounds = { b.u1, b.v1 };
                                                    lp.NP_init = de_params.initial_population_size;
                                                    lp.max_evaluations = de_params.max_evaluations;
                                                    lp.seed = de_params.seed + i;
                                                    lp.threads = 0;

                                                    auto res = utils::optimization::l_shade ( cost_func, lp );
                                                    lx.push_back ( x_fn ( res[ 0 ], res[ 1 ] ) );
                                                    ly.push_back ( y_fn ( res[ 0 ], res[ 1 ] ) );
                                                    lz.push_back ( z_fn ( res[ 0 ], res[ 1 ] ) );
                                                }
                                                if ( !lx.empty () )
                                                {
                                                    std::scoped_lock lock ( out_mutex );
                                                    out_cloud.x.append ( std::span ( lx.data (), lx.size () ) );
                                                    out_cloud.y.append ( std::span ( ly.data (), ly.size () ) );
                                                    out_cloud.z.append ( std::span ( lz.data (), lz.size () ) );
                                                }
                                            } );
            } );
    }
}   // namespace StuCanvas
