/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <type_traits>

namespace StuCanvas
{
    // =========================================================================
    // 🚀 热路径冷分离资产：三个独立的步长包装结构体
    //    由于它们是独特的 C++ 结构体，它们会在 TypeId<T>::get() 中分配到
    //    完全不同、互不冲突的编译期物理槽位，保证了 0 运行时分支检索的高性能。
    // =========================================================================

    // 1. 点离散化步长资产
    template <typename T>
    struct AssetStepPoints
    {
        T value;

    };

    // 2. 连续条带（Strips）离散化步长资产
    template <typename T>
    struct AssetStepStrips
    {
        T value;

    };

    // 3. 三角网格（Triangles）离散化步长资产
    template <typename T>
    struct AssetStepTriangles
    {
        T value;

    };


} // namespace StuCanvas