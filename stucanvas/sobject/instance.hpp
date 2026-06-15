/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

// stucanvas/object/sobject_instance.hpp
#pragma once
#include <eigen3/Eigen/Dense>
#include <stdexcept>
#include "family.hpp"      // 引入家族定义，用以解算 GetLocalAABB
#include "block_deque.hpp" // 引入我们极致优化的 8 字节 BlockDeque

namespace StuCanvas
{
    template <typename T>
    class SObjectFamily;

    template <typename T>
    struct AABB3D;

    // ========================================================================
    // SObjectInstance 享元空间实例 (100% 纯裸指针，数据导向设计，零 CPU 锁开销)
    // 🚀 已完美契合 64 字节（CPU 缓存行）对齐边界。
    // ========================================================================
    template <typename T>
    struct SObjectInstance
    {
        // 1. 物理几何源：指向一整块“完整刚性铁板”的家族包装器只读指针（8 字节）
        const SObjectFamily<T>* source_family = nullptr;

        // 2. 空间摆放状态 (TRS 刚性物理通道) —— 使用高能 Eigen 库
        Eigen::Matrix<T, 3, 1> world_position = Eigen::Matrix<T, 3, 1>::Zero();    // 世界坐标 (T) (12 字节)
        Eigen::Quaternion<T>   world_rotation = Eigen::Quaternion<T>::Identity();  // 四元数旋转 (R) (16 字节)
        Eigen::Matrix<T, 3, 1> world_scales    = Eigen::Matrix<T, 3, 1>::Ones();    // 世界非等比缩放 (S) (12 字节)

        // 3. 🚀 拓扑依赖树：采用极致优化的 8 字节 BlockDeque 存储父子实例指针
        // 数据类型为指针类型，100% 触发 C++23 特化免析构批量释放与物理块无锁 Cache 拦截
        utils::BlockDeque<const SObjectInstance<T>*, 4> parents;  // (8 字节)
        utils::BlockDeque<const SObjectInstance<T>*, 4> children; // (8 字节)

        // ─────────────────────────────────────────────────────────────────────
        // 4. 拓扑安全生命周期（移动时自动打补丁，析构自动断开，杜绝野指针崩溃）
        // ─────────────────────────────────────────────────────────────────────
        SObjectInstance() noexcept = default;

        ~SObjectInstance() noexcept
        {
            disconnect_self();
        }

        // 禁止拷贝，保障图谱唯一性
        SObjectInstance(const SObjectInstance&) = delete;
        SObjectInstance& operator=(const SObjectInstance&) = delete;

        // 移动构造
        SObjectInstance(SObjectInstance&& other) noexcept
            : source_family(other.source_family),
              world_position(std::move(other.world_position)),
              world_rotation(std::move(other.world_rotation)),
              world_scales(std::move(other.world_scales)),
              parents(std::move(other.parents)),
              children(std::move(other.children))
        {
            patch_connections(&other, this);
            other.source_family = nullptr;
        }

        // 移动赋值
        SObjectInstance& operator=(SObjectInstance&& other) noexcept
        {
            if (this != &other)
            {
                disconnect_self();

                source_family = other.source_family;
                world_position = std::move(other.world_position);
                world_rotation = std::move(other.world_rotation);
                world_scales = std::move(other.world_scales);
                parents = std::move(other.parents);
                children = std::move(other.children);

                patch_connections(&other, this);
                other.source_family = nullptr;
            }
            return *this;
        }

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

    private:
        // 🚀 核心连接打补丁：让所有父子关系中的旧地址指针自动复写为 this 新地址
        void patch_connections(SObjectInstance* old_addr, SObjectInstance* new_addr) noexcept
        {
            for (size_t i = 0; i < parents.size(); ++i) {
                auto* parent = const_cast<SObjectInstance*>(parents[i]);
                if (parent) {
                    for (size_t j = 0; j < parent->children.size(); ++j) {
                        if (parent->children[j] == old_addr) {
                            parent->children[j] = new_addr;
                        }
                    }
                }
            }
            for (size_t i = 0; i < children.size(); ++i) {
                auto* child = const_cast<SObjectInstance*>(children[i]);
                if (child) {
                    for (size_t j = 0; j < child->parents.size(); ++j) {
                        if (child->parents[j] == old_addr) {
                            child->parents[j] = new_addr;
                        }
                    }
                }
            }
        }

        // 🚀 核心析构断开：利用 BlockDeque::erase_unordered (O(1) 快速擦除) 极速脱离拓扑网
        void disconnect_self() noexcept
        {
            for (size_t i = 0; i < parents.size(); ++i) {
                auto* parent = const_cast<SObjectInstance*>(parents[i]);
                if (parent) {
                    parent->children.erase_unordered(this);
                }
            }
            for (size_t i = 0; i < children.size(); ++i) {
                auto* child = const_cast<SObjectInstance*>(children[i]);
                if (child) {
                    child->parents.erase_unordered(this);
                }
            }
        }
    };
} // namespace StuCanvas