// stucanvas/object/sobject_instance.hpp
#pragma once
#include <eigen3/Eigen/Dense>
#include <stdexcept>
#include "family.hpp" // 引入家族定义，用以解算 GetLocalAABB

namespace StuCanvas
{
    template <typename T>
    class SObjectFamily;

    template <typename T>
    struct AABB3D;

    // ========================================================================
    // SObjectInstance 享元空间实例 (100% 纯裸指针，数据导向设计，零 CPU 锁开销)
    // ========================================================================
    template <typename T>
    struct SObjectInstance
    {
        // 1. 物理几何源：指向一整块“完整刚性铁板”的家族包装器只读指针（编译期防呆安全设计）
        const SObjectFamily<T>* source_family = nullptr;

        // 2. 空间摆放状态 (TRS 刚性物理通道) —— 使用高能 Eigen 库
        Eigen::Matrix<T, 3, 1> world_position = Eigen::Matrix<T, 3, 1>::Zero();    // 世界坐标 (T)
        Eigen::Quaternion<T>   world_rotation = Eigen::Quaternion<T>::Identity();  // 四元数旋转 (R)
        Eigen::Matrix<T, 3, 1> world_scales    = Eigen::Matrix<T, 3, 1>::Ones();    // 世界缩放 (S)

        // 3. 画布映射数据载荷层 (不含有任何 Vulkan 依赖，纯 T 精度 3x4 矩阵)
        Eigen::Matrix<T, 3, 4> canvas_matrix  = Eigen::Matrix<T, 3, 4>::Identity();

        // ─── 空间边界解算方法 ───

        // 核心：直接拉取整门家族合并后的局部包围盒 (Local AABB)
        AABB3D<T> GetLocalAABB() const
        {
            if (source_family) [[likely]]
            {
                return source_family->GetLocalAABB();
            }
            
            AABB3D<T> box;
            box.min.setZero();
            box.max.setZero();
            return box;
        }

        // 获取该实例在世界空间（Canvas）下的轴对齐包围盒 (World AABB)
        // 将局部包围盒的 8 个角顶点，用当前烘焙好的 3x4 画布矩阵投影到世界空间，重新包裹
        AABB3D<T> GetWorldAABB() const
        {
            AABB3D<T> world_box;
            if (!source_family) [[unlikely]] return world_box;

            AABB3D<T> local_box = GetLocalAABB();

            // 提取局部包围盒的 8 个角顶点
            Eigen::Matrix<T, 3, 1> corners[8] = {
                {local_box.min.x(), local_box.min.y(), local_box.min.z()},
                {local_box.min.x(), local_box.min.y(), local_box.max.z()},
                {local_box.min.x(), local_box.max.y(), local_box.min.z()},
                {local_box.min.x(), local_box.max.y(), local_box.max.z()},
                {local_box.max.x(), local_box.min.y(), local_box.min.z()},
                {local_box.max.x(), local_box.min.y(), local_box.max.z()},
                {local_box.max.x(), local_box.max.y(), local_box.min.z()},
                {local_box.max.x(), local_box.max.y(), local_box.max.z()}
            };

            // 投影提取：p_world = R * S * p_local + T
            Eigen::Matrix<T, 3, 3> RS = canvas_matrix.template block<3, 3>(0, 0);
            Eigen::Matrix<T, 3, 1> Trans = canvas_matrix.col(3);

            for (auto & corner : corners)
            {
                Eigen::Matrix<T, 3, 1> world_p = RS * corner + Trans;
                world_box.min = world_box.min.cwiseMin(world_p);
                world_box.max = world_box.max.cwiseMax(world_p);
            }
            return world_box;
        }

        // 核心：一键将当前 TRS 和局部重心，单向烘焙成画布空间唯一的 3x4 变换矩阵 [1.1.2]
        // 物理公式：M = T_world * R * S * T_(-c)
        void BakeMatrix() noexcept
        {
            AABB3D<T> local_box = GetLocalAABB();
            Eigen::Matrix<T, 3, 1> center = local_box.GetCenter(); // 获取局部几何中心 c

            // 1. 计算缩放与自转：RS = R * S
            Eigen::Matrix<T, 3, 3> R = world_rotation.toRotationMatrix();
            Eigen::Matrix<T, 3, 3> S = world_scales.asDiagonal();
            Eigen::Matrix<T, 3, 3> RS = R * S;

            // 2. 写入 3x4 矩阵的左半部分（RS 联合变换）
            canvas_matrix.template block<3, 3>(0, 0) = RS;

            // 3. 【核心物理避让】：将重心偏置 T_(-c) 合并进平移列中
            // 公式：T_final = T_world - R * S * c
            canvas_matrix.col(3) = world_position - RS * center;
        }
    };
} // namespace StuCanvas