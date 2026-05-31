#pragma once
namespace StuCanvas
{
    struct alignas(16) Point_GPU
    {
        float x, y, z;
        float _pad;
        float r, g, b, a;
    };
}
