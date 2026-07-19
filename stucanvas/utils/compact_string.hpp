/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <algorithm>
#include <cassert>
#include <string_view>

#include "tiny_vector.hpp"
// 确保能访问到外部实现的 aligned_alloc 辅助函数
#if defined(_MSC_VER)
#include <malloc.h>
#define STUCANVAS_NOINLINE __declspec(noinline)
#else
#define STUCANVAS_NOINLINE __attribute__((noinline))
#endif

namespace StuCanvas::utils
{
    class CompactString
    {
    private:

        // 1. 偏置于字符指针前方的标头
        struct alignas ( 8 ) StringHeader
        {
            uint32_t capacity;
            uint32_t size;
        };

        // 2. 内存对齐参数（保持与 TinyVector 一致的高效对齐逻辑）
        static constexpr size_t Alignment = 8;
        static constexpr size_t HeaderOffset = ( sizeof ( StringHeader ) + Alignment - 1 ) & ~( Alignment - 1 );

        // 核心句柄：仅占 8 字节（一个 naked char 指针）
        char* m_ptr = nullptr;

        [[nodiscard]] inline StringHeader* get_header () const noexcept
        {
            if ( !m_ptr )
            {
                return nullptr;
            }
            return reinterpret_cast< StringHeader* > ( m_ptr - HeaderOffset );
        }

        static char* allocate ( uint32_t capacity )
        {
            if ( capacity == 0 )
            {
                return nullptr;
            }

            // 标头偏移量 + 容量 + 1 字节结束符（\0）
            size_t total_size = HeaderOffset + static_cast< size_t > ( capacity ) + 1;
            
            // 🚀 复用项目底层的自适应分配器，消除跨平台差异与 Leak 警报
            void* raw = ::StuCanvas::utils::detail::aligned_alloc_helper ( total_size, Alignment );

            StringHeader* h = reinterpret_cast< StringHeader* > ( raw );
            h->capacity = capacity;
            h->size = 0;

            char* data = reinterpret_cast< char* > ( raw ) + HeaderOffset;
            data[ 0 ] = '\0';
            return data;
        }

        static void deallocate ( char* ptr ) noexcept
        {
            if ( !ptr )
            {
                return;
            }
            void* raw = ptr - HeaderOffset;
            ::StuCanvas::utils::detail::aligned_free_helper ( raw, Alignment );
        }

    public:

        CompactString () noexcept = default;

        explicit CompactString ( const char* str )
        {
            if ( str )
            {
                uint32_t len = static_cast< uint32_t > ( std::strlen ( str ) );
                if ( len > 0 )
                {
                    m_ptr = allocate ( len );
                    StringHeader* h = get_header ();
                    h->size = len;
                    std::memcpy ( m_ptr, str, len );
                    m_ptr[ len ] = '\0';
                }
            }
        }

        CompactString ( const char* str, uint32_t len )
        {
            if ( str && len > 0 )
            {
                m_ptr = allocate ( len );
                StringHeader* h = get_header ();
                h->size = len;
                std::memcpy ( m_ptr, str, len );
                m_ptr[ len ] = '\0';
            }
        }

        ~CompactString () noexcept
        {
            if ( m_ptr )
            {
                deallocate ( m_ptr );
            }
        }

        // 🚀 O(1) 移动语义：只需交换 8 字节指针，无任何内存分配开销
        CompactString ( CompactString&& other ) noexcept : m_ptr ( other.m_ptr )
        {
            other.m_ptr = nullptr;
        }

        CompactString& operator= ( CompactString&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( m_ptr )
                {
                    deallocate ( m_ptr );
                }
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        // 🚀 拷贝构造
        CompactString ( const CompactString& other )
        {
            if ( other.m_ptr )
            {
                uint32_t len = other.size ();
                m_ptr = allocate ( len );
                StringHeader* h = get_header ();
                h->size = len;
                std::memcpy ( m_ptr, other.m_ptr, len );
                m_ptr[ len ] = '\0';
            }
        }

        // 🚀 极致拷贝赋值优化：容量足够时复用内存，避免重复释放与申请
        CompactString& operator= ( const CompactString& other )
        {
            if ( this != &other )
            {
                if ( other.m_ptr )
                {
                    uint32_t len = other.size ();
                    if ( len > capacity () )
                    {
                        if ( m_ptr )
                        {
                            deallocate ( m_ptr );
                        }
                        m_ptr = allocate ( len );
                    }
                    StringHeader* h = get_header ();
                    h->size = len;
                    std::memcpy ( m_ptr, other.m_ptr, len );
                    m_ptr[ len ] = '\0';
                }
                else
                {
                    if ( m_ptr )
                    {
                        deallocate ( m_ptr );
                        m_ptr = nullptr;
                    }
                }
            }
            return *this;
        }

        // 🚀 预分配容量（复用项目底层的自适应就地重新分配器，压榨吞吐性能）
        void reserve ( uint32_t new_cap )
        {
            uint32_t cur_cap = capacity ();
            if ( new_cap <= cur_cap )
            {
                return;
            }

            uint32_t sz = size ();
            void* old_raw = m_ptr ? ( reinterpret_cast< char* > ( m_ptr ) - HeaderOffset ) : nullptr;
            
            // 额外留出 1 字节结束符的空间
            size_t old_total_size = HeaderOffset + cur_cap + 1;
            size_t new_total_size = HeaderOffset + new_cap + 1;

            // 调用您项目底层的 realloc
            void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper ( old_raw, old_total_size, new_total_size, Alignment );

            StringHeader* h = reinterpret_cast< StringHeader* > ( new_raw );
            h->capacity = new_cap;
            h->size = sz;

            m_ptr = reinterpret_cast< char* > ( new_raw ) + HeaderOffset;
            m_ptr[ sz ] = '\0';
        }

        // 🚀 批量追加
        void append ( const char* str, uint32_t len )
        {
            if ( !str || len == 0 )
            {
                return;
            }

            uint32_t cur_size = size ();
            uint32_t new_size = cur_size + len;

            if ( new_size > capacity () )
            {
                uint32_t new_cap = capacity () == 0 ? 8 : capacity () * 2;
                while ( new_cap < new_size )
                {
                    new_cap *= 2;
                }
                reserve ( new_cap );
            }

            std::memcpy ( m_ptr + cur_size, str, len );
            m_ptr[ new_size ] = '\0';
            get_header ()->size = new_size;
        }

        void append ( const char* str )
        {
            if ( str )
            {
                append ( str, static_cast< uint32_t > ( std::strlen ( str ) ) );
            }
        }

        void append ( const CompactString& other )
        {
            append ( other.data (), other.size () );
        }

        // 极致性能：保持原容量，仅逻辑清零
        void clear () noexcept
        {
            StringHeader* h = get_header ();
            if ( h )
            {
                h->size = 0;
                m_ptr[ 0 ] = '\0';
            }
        }

        // 释放物理内存
        void shrink_to_fit () noexcept
        {
            uint32_t sz = size ();
            if ( sz == 0 )
            {
                if ( m_ptr )
                {
                    deallocate ( m_ptr );
                    m_ptr = nullptr;
                }
            }
            else if ( sz < capacity () )
            {
                void* old_raw = reinterpret_cast< char* > ( m_ptr ) - HeaderOffset;
                size_t old_total_size = HeaderOffset + capacity () + 1;
                size_t new_total_size = HeaderOffset + sz + 1;

                void* new_raw = ::StuCanvas::utils::detail::aligned_realloc_helper ( old_raw, old_total_size, new_total_size, Alignment );
                
                StringHeader* h = reinterpret_cast< StringHeader* > ( new_raw );
                h->capacity = sz;

                m_ptr = reinterpret_cast< char* > ( new_raw ) + HeaderOffset;
            }
        }


        // 🚀 新增：支持从裸 C 风格字符串直接赋值（零拷贝复用内存）
        CompactString& operator= ( const char* str )
        {
            if ( str )
            {
                uint32_t len = static_cast< uint32_t > ( std::strlen ( str ) );
                if ( len > capacity () )
                {
                    if ( m_ptr )
                    {
                        deallocate ( m_ptr );
                    }
                    m_ptr = allocate ( len );
                }
                
                StringHeader* h = get_header ();
                h->size = len;
                std::memcpy ( m_ptr, str, len );
                m_ptr[ len ] = '\0';
            }
            else
            {
                if ( m_ptr )
                {
                    deallocate ( m_ptr );
                    m_ptr = nullptr;
                }
            }
            return *this;
        }

        // 🚀 新增：支持从现代 std::string_view 直接赋值（极致性能，零多余分配）
        CompactString& operator= ( std::string_view sv )
        {
            uint32_t len = static_cast< uint32_t > ( sv.size () );
            if ( len > 0 )
            {
                if ( len > capacity () )
                {
                    if ( m_ptr )
                    {
                        deallocate ( m_ptr );
                    }
                    m_ptr = allocate ( len );
                }
                
                StringHeader* h = get_header ();
                h->size = len;
                std::memcpy ( m_ptr, sv.data (), len );
                m_ptr[ len ] = '\0';
            }
            else
            {
                if ( m_ptr )
                {
                    deallocate ( m_ptr );
                    m_ptr = nullptr;
                }
            }
            return *this;
        }

        [[nodiscard]] inline uint32_t size () const noexcept
        {
            auto* h = get_header ();
            return h ? h->size : 0;
        }

        [[nodiscard]] inline uint32_t capacity () const noexcept
        {
            auto* h = get_header ();
            return h ? h->capacity : 0;
        }

        [[nodiscard]] inline bool empty () const noexcept
        {
            return size () == 0;
        }

        [[nodiscard]] inline const char* c_str () const noexcept
        {
            return m_ptr ? m_ptr : "";
        }

        [[nodiscard]] inline char* data () noexcept
        {
            return m_ptr;
        }

        [[nodiscard]] inline const char* data () const noexcept
        {
            return m_ptr;
        }

        // 🚀 零拷贝无缝转换标准只读视图，完美兼容标准库接口
        [[nodiscard]] inline operator std::string_view () const noexcept
        {
            return std::string_view ( c_str (), size () );
        }

        [[nodiscard]] inline char& operator[] ( size_t idx ) noexcept
        {
            assert ( idx < size () );
            return m_ptr[ idx ];
        }

        [[nodiscard]] inline const char& operator[] ( size_t idx ) const noexcept
        {
            assert ( idx < size () );
            return m_ptr[ idx ];
        }

        [[nodiscard]] inline bool operator== ( const CompactString& other ) const noexcept
        {
            uint32_t sz = size ();
            if ( sz != other.size () )
            {
                return false;
            }
            if ( sz == 0 )
            {
                return true;
            }
            // 🚀 利用系统级极致优化 memcmp 替代普通的 strcmp 遍历，耗时大幅压低
            return std::memcmp ( m_ptr, other.m_ptr, sz ) == 0;
        }

        [[nodiscard]] inline bool operator!= ( const CompactString& other ) const noexcept
        {
            return !( *this == other );
        }
    };
} // namespace StuCanvas::utils
