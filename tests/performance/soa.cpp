/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <random>
#include <limits>
#include <cmath>
// 🚀 载入您的高性能 TinyVector
#include "tiny_vector.hpp"
using namespace StuCanvas;

// 🚀 保持项目习惯：零成本编译器屏障，防止测试循环被编译器完全擦除
template < typename T >
void do_not_optimize ( const T& val )
{
#if defined ( __GNUC__ ) || defined ( __clang__ )
    asm volatile ( "" : : "g" ( &val ) : "memory" );
#else
    const volatile void* p = static_cast< const volatile void* > ( &val );
    ( void ) p;
#endif
}

// 🚀 保持项目习惯：大间距 Allman 换行风格计时器
struct Timer
{
    std::string name;
    std::chrono::high_resolution_clock::time_point start;

    Timer ( std::string n ) : name ( std::move ( n ) ), start ( std::chrono::high_resolution_clock::now () )
    {
    }

    ~Timer ()
    {
        auto end = std::chrono::high_resolution_clock::now ();
        auto duration = std::chrono::duration< double, std::milli > ( end - start ).count ();
        std::cout << "  " << std::left << std::setw ( 50 ) << name << " : "
                  << std::right << std::setw ( 10 ) << std::fixed << std::setprecision ( 3 )
                  << duration << " ms" << std::endl;
    }
};

// =========================================================================
// 1. AoS (结构体数组) 数据布局定义
// =========================================================================
struct AoSPoint
{
    double x;
    double y;
    double z;
};

// =========================================================================
// 2. SoA (数组结构体) 数据布局定义
// =========================================================================
template < typename Container >
struct SoAPointCloud
{
    Container x;
    Container y;
    Container z;

    void resize ( size_t sz )
    {
        x.resize ( static_cast< uint32_t > ( sz ) );
        y.resize ( static_cast< uint32_t > ( sz ) );
        z.resize ( static_cast< uint32_t > ( sz ) );
    }
};

// =========================================================================
// 3. AoS 检索算法：寻找距离 query 最近的点的索引与距离
// =========================================================================
template < typename Container >
size_t findNearestAoS ( const Container& cloud, const AoSPoint& query, double& min_dist_sq )
{
    size_t nearest_idx = 0;
    double min_d2 = std::numeric_limits< double >::max ();
    size_t sz = cloud.size ();

    for ( size_t i = 0; i < sz; ++i )
    {
        double dx = cloud[ i ].x - query.x;
        double dy = cloud[ i ].y - query.y;
        double dz = cloud[ i ].z - query.z;
        double d2 = ( dx * dx ) + ( dy * dy ) + ( dz * dz );

        if ( d2 < min_d2 )
        {
            min_d2 = d2;
            nearest_idx = i;
        }
    }
    
    min_dist_sq = min_d2;
    return nearest_idx;
}

// =========================================================================
// 4. SoA 检索算法：寻找距离 query 最近的点的索引与距离
// =========================================================================
template < typename Container >
size_t findNearestSoA ( const SoAPointCloud< Container >& cloud, const AoSPoint& query, double& min_dist_sq )
{
    size_t nearest_idx = 0;
    double min_d2 = std::numeric_limits< double >::max ();
    size_t sz = cloud.x.size ();

    // 🚀 性能点：直接解引原始首地址指针进行高速连续偏移，免去 inline operator[] 的寻址开销
    const double* rx = cloud.x.data ();
    const double* ry = cloud.y.data ();
    const double* rz = cloud.z.data ();

    for ( size_t i = 0; i < sz; ++i )
    {
        double dx = rx[ i ] - query.x;
        double dy = ry[ i ] - query.y;
        double dz = rz[ i ] - query.z;
        double d2 = ( dx * dx ) + ( dy * dy ) + ( dz * dz );

        if ( d2 < min_d2 )
        {
            min_d2 = d2;
            nearest_idx = i;
        }
    }

    min_dist_sq = min_d2;
    return nearest_idx;
}

int main ()
{
    // 🚀 测试规模：1,000,000（一百万）个三维点，物理内存占用约 24 MB（混合 L3 与 DRAM 压测）
    constexpr size_t N = 1000000;
    constexpr size_t benchmark_loops = 50; // 重复运行 50 次取平均值

    std::cout << "=== Generating Point Cloud (" << N << " points) ===" << std::endl;

    // 填充随机测试数据
    std::mt19937 gen ( 12345 );
    std::uniform_real_distribution< double > dis ( -1000.0, 1000.0 );

    AoSPoint query { dis ( gen ), dis ( gen ), dis ( gen ) };

    // 初始化 std::vector 容器
    std::vector< AoSPoint > std_aos ( N );
    SoAPointCloud< std::vector< double > > std_soa;
    std_soa.resize ( N );

    // 初始化 utils::TinyVector 容器
    utils::TinyVector< AoSPoint > tiny_aos;
    tiny_aos.resize ( N );
    SoAPointCloud< utils::TinyVector< double > > tiny_soa;
    tiny_soa.resize ( N );

    for ( size_t i = 0; i < N; ++i )
    {
        double x = dis ( gen );
        double y = dis ( gen );
        double z = dis ( gen );

        // 填充 std
        std_aos[ i ] = { x, y, z };
        std_soa.x[ i ] = x;
        std_soa.y[ i ] = y;
        std_soa.z[ i ] = z;

        // 填充 tiny
        tiny_aos[ i ] = { x, y, z };
        tiny_soa.x[ i ] = x;
        tiny_soa.y[ i ] = y;
        tiny_soa.z[ i ] = z;
    }

    std::cout << "=== Point Cloud Generation Complete ===" << std::endl;
    std::cout << "Query Point: (" << query.x << ", " << query.y << ", " << query.z << ")\n" << std::endl;

    // ====================================================
    // 对比 1：std::vector 性能测试
    // ====================================================
    std::cout << "=== 1. std::vector Layout Benchmark (50 Runs) ===" << std::endl;
    {
        double d2 = 0.0;
        size_t idx = 0;
        {
            Timer t ( "std::vector <AoS> (Array of Structures)" );
            for ( size_t loop = 0; loop < benchmark_loops; ++loop )
            {
                idx = findNearestAoS ( std_aos, query, d2 );
                do_not_optimize ( idx );
            }
        }
        std::cout << "  [Result] Nearest Index: " << idx << ", Dist: " << std::sqrt ( d2 ) << std::endl;
    }
    {
        double d2 = 0.0;
        size_t idx = 0;
        {
            Timer t ( "std::vector <SoA> (Structure of Arrays)" );
            for ( size_t loop = 0; loop < benchmark_loops; ++loop )
            {
                idx = findNearestSoA ( std_soa, query, d2 );
                do_not_optimize ( idx );
            }
        }
        std::cout << "  [Result] Nearest Index: " << idx << ", Dist: " << std::sqrt ( d2 ) << "\n" << std::endl;
    }

    // ====================================================
    // 对比 2：utils::TinyVector 性能测试（验证您的自定义容器）
    // ====================================================
    std::cout << "=== 2. utils::TinyVector Layout Benchmark (50 Runs) ===" << std::endl;
    {
        double d2 = 0.0;
        size_t idx = 0;
        {
            Timer t ( "utils::TinyVector <AoS> (Array of Structures)" );
            for ( size_t loop = 0; loop < benchmark_loops; ++loop )
            {
                idx = findNearestAoS ( tiny_aos, query, d2 );
                do_not_optimize ( idx );
            }
        }
        std::cout << "  [Result] Nearest Index: " << idx << ", Dist: " << std::sqrt ( d2 ) << std::endl;
    }
    {
        double d2 = 0.0;
        size_t idx = 0;
        {
            Timer t ( "utils::TinyVector <SoA> (Structure of Arrays)" );
            for ( size_t loop = 0; loop < benchmark_loops; ++loop )
            {
                idx = findNearestSoA ( tiny_soa, query, d2 );
                do_not_optimize ( idx );
            }
        }
        std::cout << "  [Result] Nearest Index: " << idx << ", Dist: " << std::sqrt ( d2 ) << std::endl;
    }

    return 0;
}
