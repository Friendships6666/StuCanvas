#pragma once
#include <eigen3/Eigen/Dense>

namespace StuCanvas
{
    struct DAGObject;
    struct DAGObjectInstance
    {
        DAGObject* source;
        void* appearance;

        // 世界坐标 (T) (3个double，共 24 字节)
        double world_position[ 3 ] = { 0.0, 0.0, 0.0 };

        // 四元数旋转 (R) (4个float，共 16 字节)，布局为 [x, y, z, w]，初始化为单位四元数
        float world_rotation[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

        // 世界非等比缩放 (S) (3个double，共 24 字节)
        double world_scales[ 3 ] = { 1.0, 1.0, 1.0 };
    };
}   // namespace StuCanvas
