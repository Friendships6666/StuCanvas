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
            T x, y, z;
        } point_3d;


        struct
        {
            T x0, y0, x1, y1;
        } line_2d;

        struct
        {
            T x0, y0, z0, x1, y1, z1;
        } line_3d;

        struct
        {
            T a, b, c, d;
        } plane_3d;

        struct
        {
            T value;
        } scalar;

        struct
        {
            T cx, cy, r;
        } circle_2d;

        struct
        {
            T cx, cy, cz, r;
        } sphere_3d;

        struct
        {
            T x0, y0, z0, x1, y1, z1, r;
        } cylinder_3d;

        struct
        {
            T x, y;
            T lock;
        } snap_2d;

        struct
        {
            T x, y, z;
            T a, b;
        } snap_3d;
    };
}
