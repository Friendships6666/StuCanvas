#pragma once
namespace StuCanvas
{
    template <typename T>
    union SObjectData
    {
        struct
        {
            T x, y;
        } point_2d;

        struct
        {
            T x, y ,z;
        } point_3d;


        struct
        {
            T x0, y0, x1, y1;
        } line_2d;

        struct
        {
            T a, b, c, d;
        } plane_3d;
    };
}