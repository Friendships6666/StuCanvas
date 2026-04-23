/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/
#pragma once
namespace StuCanvas
{
    template <typename T>
    struct Point2D
    {
        using value_type = T;
        T x, y;
    };

    template <typename T>
    struct Point3D
    {
        using value_type = T;
        T x, y, z;
    };


}
