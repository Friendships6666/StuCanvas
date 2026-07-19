/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#include "compact_string.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

// 🚀 复用项目习惯：零成本编译器屏障，防止测试循环被编译器完全擦除
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

// 🚀 复用项目习惯：大间距 Allman 换行风格计时器
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

int main ()
{
    // ====================================================
    // 指标 A：栈空间占用物理对比 (Stack Size Footprint)
    // ====================================================
    std::cout << "=== 1. Stack Memory Footprint ===" << std::endl;
    std::cout << "  sizeof ( std::string )    : " << sizeof ( std::string ) << " bytes" << std::endl;
    std::cout << "  sizeof ( CompactString )  : " << sizeof ( StuCanvas::utils::CompactString ) << " bytes (Saved ~70% stack space)" << std::endl;
    std::cout << "=================================\n" << std::endl;

    // ====================================================
    // 指标 B：短字符串分配对比 (SSO 机制 vs 纯堆分配)
    // ====================================================
    {
        constexpr size_t iterations = 1000000; // 100 万次
        std::cout << "=== Test 1: Small String (10 chars) Allocation & Destruction ===" << std::endl;
        
        {
            Timer t ( "std::string (SSO - Stack Only, No malloc)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                std::string s ( "123456789" );
                do_not_optimize ( s );
            }
        }
        
        {
            Timer t ( "CompactString (Fully Heap Allocated)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                StuCanvas::utils::CompactString s ( "123456789" );
                do_not_optimize ( s );
            }
        }
        std::cout << "----------------------------------------------------------------\n" << std::endl;
    }

    // ====================================================
    // 指标 C：大字符串分配对比 (均绕过 SSO 强制下到堆空间)
    // ====================================================
    {
        constexpr size_t iterations = 500000; // 50 万次
        std::cout << "=== Test 2: Large String (1000 chars) Allocation & Destruction ===" << std::endl;
        std::string large_source ( 1000, 'x' );
        
        {
            Timer t ( "std::string (Heap Allocation)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                std::string s ( large_source.c_str () );
                do_not_optimize ( s );
            }
        }
        
        {
            Timer t ( "CompactString (Heap Allocation)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                StuCanvas::utils::CompactString s ( large_source.c_str () );
                do_not_optimize ( s );
            }
        }
        std::cout << "----------------------------------------------------------------\n" << std::endl;
    }

    // ====================================================
    // 指标 D：深拷贝与内存复用对比 (测试 `=` 重写后避免 reallocation 机制)
    // ====================================================
    {
        constexpr size_t iterations = 5000000; // 500 万次
        std::cout << "=== Test 3: Copying & Assignment (With Pre-allocated Capacity) ===" << std::endl;
        
        std::string std_src ( "This is a medium-sized test string designed for testing copying." );
        StuCanvas::utils::CompactString comp_src ( "This is a medium-sized test string designed for testing copying." );

        {
            std::string std_dst;
            std_dst.reserve ( 128 ); // 预分配
            Timer t ( "std::string copy assignment (reusing capacity)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                std_dst = std_src;
                do_not_optimize ( std_dst );
            }
        }
        
        {
            StuCanvas::utils::CompactString comp_dst;
            comp_dst.reserve ( 128 ); // 预分配
            Timer t ( "CompactString copy assignment (reusing capacity)" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                comp_dst = comp_src;
                do_not_optimize ( comp_dst );
            }
        }
        std::cout << "----------------------------------------------------------------\n" << std::endl;
    }

    // ====================================================
    // 指标 E：连续批量追加效率 (Dynamic Appending)
    // ====================================================
    {
        constexpr size_t iterations = 1000000; // 100 万次
        std::cout << "=== Test 4: Dynamic Appending (Small Chunks) ===" << std::endl;
        
        {
            Timer t ( "std::string append" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                std::string s;
                s.reserve ( 100 );
                s.append ( "hello " );
                s.append ( "world " );
                s.append ( "performance!" );
                do_not_optimize ( s );
            }
        }
        
        {
            Timer t ( "CompactString append" );
            for ( size_t i = 0; i < iterations; ++i )
            {
                StuCanvas::utils::CompactString s;
                s.reserve ( 100 );
                s.append ( "hello " );
                s.append ( "world " );
                s.append ( "performance!" );
                do_not_optimize ( s );
            }
        }
        std::cout << "----------------------------------------------------------------\n" << std::endl;
    }

    // ====================================================
    // 指标 F：字符串相等对比 (测试 optimized std::memcmp 的运行效率)
    // ====================================================
    {
        constexpr size_t iterations = 10000000; // 1000 万次
        std::cout << "=== Test 5: Comparison / Equality Check ===" << std::endl;
        
        std::string std_s1 ( "A moderately long string used to verify if memcmp optimization beats std::string." );
        std::string std_s2 ( "A moderately long string used to verify if memcmp optimization beats std::string." );

        StuCanvas::utils::CompactString comp_s1 ( "A moderately long string used to verify if memcmp optimization beats std::string." );
        StuCanvas::utils::CompactString comp_s2 ( "A moderately long string used to verify if memcmp optimization beats std::string." );

        {
            Timer t ( "std::string operator==" );
            volatile bool result = false;
            for ( size_t i = 0; i < iterations; ++i )
            {
                result = ( std_s1 == std_s2 );
                do_not_optimize ( result );
            }
        }
        
        {
            Timer t ( "CompactString operator==" );
            volatile bool result = false;
            for ( size_t i = 0; i < iterations; ++i )
            {
                result = ( comp_s1 == comp_s2 );
                do_not_optimize ( result );
            }
        }
        std::cout << "----------------------------------------------------------------" << std::endl;
    }

    return 0;
}
