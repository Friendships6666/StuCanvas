/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

// stucanvas/object/sobject_Instance.hpp
#pragma once
#include <eigen3/Eigen/Dense>
#include <stdexcept>
#include "family.hpp"      // 引入家族定义，用以解算 GetLocalAABB

namespace StuCanvas
{

    template <typename T>
    struct AABB3D;

    struct SObjectAppearance;

    // ========================================================================
    // SObjectInstance 享元空间实例 (100% 纯裸指针，数据导向设计，零 CPU 锁开销)
    // 🚀 已完美契合 64 字节（CPU 缓存行）对齐边界。
    // ========================================================================
    struct VisualConfig;
    template <typename T>
    struct SObjectInstance
    {
        const SObject<T>* source = nullptr;
        const SObjectAppearance* appearance = nullptr;
        // 2. 空间摆放状态 (TRS 刚性物理通道) —— 使用高能 Eigen 库
        Eigen::Matrix<T, 3, 1> world_position = Eigen::Matrix<T, 3, 1>::Zero();    // 世界坐标 (T) (12 字节)
        Eigen::Quaternion<T>   world_rotation = Eigen::Quaternion<T>::Identity();  // 四元数旋转 (R) (16 字节)
        Eigen::Matrix<T, 3, 1> world_scales    = Eigen::Matrix<T, 3, 1>::Ones();    // 世界非等比缩放 (S) (12 字节)



        SObjectInstance() noexcept = default;
        ~SObjectInstance() noexcept = default;

        SObjectInstance(const SObjectInstance&) = delete;
        SObjectInstance& operator=(const SObjectInstance&) = delete;

        SObjectInstance(SObjectInstance&& other) noexcept = default;
        SObjectInstance& operator=(SObjectInstance&& other) noexcept = default;

        // ─── 空间边界解算方法 ───

        // 核心：直接拉取整门家族合并后的局部包围盒 (Local AABB)
        AABB3D<T> GetLocalAABB() const
        {
            if (source) [[likely]]
            {
                return source->GetLocalAABB();
            }

            AABB3D<T> box;
            box.min.setZero();
            box.max.setZero();
            return box;
        }

        // 获取该实例在世界空间（Canvas）下的轴对齐包围盒 (World AABB)
        AABB3D<T> GetWorldAABB() const
        {
            AABB3D<T> world_box;
            if (!source) [[unlikely]] return world_box;

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

            Eigen::Matrix<T, 3, 4> temp_matrix;
            BakeMatrixTo(temp_matrix);

            Eigen::Matrix<T, 3, 3> RS = temp_matrix.template block<3, 3>(0, 0);
            Eigen::Matrix<T, 3, 1> Trans = temp_matrix.col(3);

            for (auto & corner : corners)
            {
                Eigen::Matrix<T, 3, 1> world_p = RS * corner + Trans;
                world_box.min = world_box.min.cwiseMin(world_p);
                world_box.max = world_box.max.cwiseMax(world_p);
            }
            return world_box;
        }

        // 🚀 就地烘焙方法（Bake Matrix JIT）
        void BakeMatrixTo(Eigen::Matrix<T, 3, 4>& out_matrix) const noexcept
        {
            AABB3D<T> local_box = GetLocalAABB();
            Eigen::Matrix<T, 3, 1> center = local_box.GetCenter(); // 获取局部几何中心 c

            // 1. 计算缩放与自转：RS = R * S
            Eigen::Matrix<T, 3, 3> R = world_rotation.toRotationMatrix();
            Eigen::Matrix<T, 3, 3> S = world_scales.asDiagonal();
            Eigen::Matrix<T, 3, 3> RS = R * S;

            // 2. 写入 3x4 矩阵的左半部分
            out_matrix.template block<3, 3>(0, 0) = RS;

            // 3. 将重心偏置合并进平移列中
            out_matrix.col(3) = world_position - RS * center;
        }

    };
} // namespace StuCanvas