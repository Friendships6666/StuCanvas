#pragma once
namespace StuCanvas
{
    union NodeData
    {
        struct
        {
            double x, y;
        } point_2d;

        struct
        {
            double x, y, z;
        } point_3d;

        struct
        {
            double x0, y0, x1, y1;
        } line_2d;

        struct
        {
            double x0, y0, z0, x1, y1, z1;
        } line_3d;

        struct
        {
            double a, b, c, d;
        } plane_3d;

        struct
        {
            double value;
        } scalar;

        struct
        {
            double cx, cy, r;
        } circle_2d;

        struct
        {
            double cx, cy, cz, r;
        } sphere_3d;

        struct
        {
            double x0, y0, z0, x1, y1, z1, r;
        } cylinder_3d;

        struct
        {
            double x, y;
            double lock;
        } snap_2d;

        struct
        {
            double x, y, z;
            double a, b;
        } snap_3d;
    };
}   // namespace StuCanvas
