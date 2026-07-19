/***************************************************************************
 * Copyright (c) 2025-2026 Tian Yuxuan (Friendships666)                    *
 *                                                                          *
 * StuCanvas is licensed under Mulan PSL v2.                                *
 * You can use this software according to the terms and conditions of the   *
 * Mulan PSL v2.                                                            *
 * You may obtain a copy of Mulan PSL v2 at:                                *
 *          http://license.coscl.org.cn/MulanPSL2                           *
 *                                                                          *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF     *
 * ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO        *
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.      *
 * See the Mulan PSL v2 for more details.                                   *
 ***************************************************************************/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <eigen3/Eigen/Dense>

// 🚀 核心要求：引入由 C++23 谓词系统生成器自动生成的相机类型描述头文件
#include "camera_types.hpp"

namespace StuCanvas
{
    // =========================================================================
    // 🚀 1. 视口配置 (ViewPort)
    // =========================================================================
    struct ViewPort
    {
        double x = 0.0;
        double y = 0.0;
        double width = 1.0;
        double height = 1.0;
    };

    // 前向声明各具体的物理相机，以便在通用 Camera 中处理 RAII 析构
    struct OrthographicCamera;
    struct PerspectiveCamera;

    // =========================================================================
    // 🚀 2. 泛型相机包装器 (Camera)
    // =========================================================================
    struct Camera
    {
        // 🚀 核心要求：第一个结构体成员为 void* 指向真实数据
        void* real_data = nullptr;

        // 默认构造
        Camera () noexcept = default;

        // 核心安全加固：由于手动管理 void*，必须实现 RAII 移动语义以防内存泄漏
        ~Camera () noexcept
        {
            if ( real_data )
            {
                // 通过将首地址转型为枚举指针，读取具体相机类型的 RTTI 标志
                const CameraType type = *static_cast< const CameraType* > ( real_data );

                switch ( type )
                {
                    case CameraType::Orthographic:
                        delete static_cast< OrthographicCamera* > ( real_data );
                        break;
                    case CameraType::Perspective:
                        delete static_cast< PerspectiveCamera* > ( real_data );
                        break;
                    default:
                        break;
                }
                real_data = nullptr;
            }
        }

        // 禁用拷贝语义，强制保持指针所有权的唯一性
        Camera ( const Camera& ) = delete;
        Camera& operator= ( const Camera& ) = delete;

        // 移动构造
        Camera ( Camera&& other ) noexcept : real_data ( other.real_data )
        {
            other.real_data = nullptr;
        }

        // 移动赋值
        Camera& operator= ( Camera&& other ) noexcept
        {
            if ( this != &other )
            {
                this->~Camera ();   // 物理释放自身现有内存
                real_data = other.real_data;
                other.real_data = nullptr;
            }
            return *this;
        }
    };

    // =========================================================================
    // 🚀 3. 具体的物理相机结构 (首个成员统一对齐 CameraType 标志)
    // =========================================================================

    struct OrthographicCamera
    {
        // 🚀 核心要求：第一个成员必须是枚举标志，用于类型解包
        CameraType type = CameraType::Orthographic;

        // 投影内参 (沿轴对称或非对称视锥边界)
        double left = -1.0;
        double right = 1.0;
        double bottom = -1.0;
        double top = 1.0;
        double near_clip = 0.1;
        double far_clip = 1000.0;

        // 外部参数 (Extrinsics)
        double position[ 3 ] = { 0.0, 0.0, 10.0 };
        double target[ 3 ] = { 0.0, 0.0, 0.0 };
        double up[ 3 ] = { 0.0, 1.0, 0.0 };
    };

    struct PerspectiveCamera
    {
        // 🚀 核心要求：第一个成员必须 is_linear 的透视投影相机
        CameraType type = CameraType::Perspective;

        // 投影内参
        double fov_y = 45.0;   // 垂直视场角 (垂直 FOV)
        double aspect = 1.6;   // 宽高比 (Width / Height)
        double near_clip = 0.1;
        double far_clip = 1000.0;

        // 外部参数 (Extrinsics)
        double position[ 3 ] = { 0.0, 0.0, 10.0 };
        double target[ 3 ] = { 0.0, 0.0, 0.0 };
        double up[ 3 ] = { 0.0, 1.0, 0.0 };
    };

    // =========================================================================
    // 🚀 4. 运行时强类型转化与分发辅助函数 (0 虚函数，0 RTTI 额外体积开销)
    // =========================================================================

    /**
     * @brief 提取当前泛型相机的真实枚举类型
     */
    [[nodiscard]] inline CameraType getCameraType ( const Camera& cam ) noexcept
    {
        if ( !cam.real_data ) [[unlikely]]
        {
            return static_cast< CameraType > ( 0xFFFFFFFF );   // 无效指针
        }
        return *static_cast< const CameraType* > ( cam.real_data );
    }

    /**
     * @brief 泛型相机可变引用向下转换模板
     */
    template < typename T >
    [[nodiscard]] inline T* castCamera ( Camera& cam ) noexcept
    {
        return static_cast< T* > ( cam.real_data );
    }

    /**
     * @brief 泛型相机只读引用向下转换模板
     */
    template < typename T >
    [[nodiscard]] inline const T* castCamera ( const Camera& cam ) noexcept
    {
        return static_cast< const T* > ( cam.real_data );
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 4.1 共享代数分发提取器：极速生成 Camera View & Projection 矩阵
    // ─────────────────────────────────────────────────────────────────────────

    inline Eigen::Matrix4d computeViewMatrixHelper ( const double position[ 3 ], const double target[ 3 ],
                                                     const double up[ 3 ] ) noexcept
    {
        const Eigen::Vector3d pos ( position[ 0 ], position[ 1 ], position[ 2 ] );
        const Eigen::Vector3d tar ( target[ 0 ], target[ 1 ], target[ 2 ] );
        const Eigen::Vector3d u ( up[ 0 ], up[ 1 ], up[ 2 ] );

        const Eigen::Vector3d z_axis = ( pos - tar ).normalized ();
        const Eigen::Vector3d x_axis = u.cross ( z_axis ).normalized ();
        const Eigen::Vector3d y_axis = z_axis.cross ( x_axis );

        Eigen::Matrix4d view = Eigen::Matrix4d::Identity ();
        view.block< 1, 3 > ( 0, 0 ) = x_axis.transpose ();
        view.block< 1, 3 > ( 1, 0 ) = y_axis.transpose ();
        view.block< 1, 3 > ( 2, 0 ) = z_axis.transpose ();
        view ( 0, 3 ) = -x_axis.dot ( pos );
        view ( 1, 3 ) = -y_axis.dot ( pos );
        view ( 2, 3 ) = -z_axis.dot ( pos );

        return view;
    }

    /**
     * @brief 统一的分发函数：在运行时通过 C-style 多态对齐，极速计算视图矩阵
     */
    [[nodiscard]] inline Eigen::Matrix4d computeCameraViewMatrix ( const Camera& cam ) noexcept
    {
        const CameraType type = getCameraType ( cam );

        switch ( type )
        {
            case CameraType::Orthographic:
            {
                const auto* ortho = castCamera< OrthographicCamera > ( cam );
                return computeViewMatrixHelper ( ortho->position, ortho->target, ortho->up );
            }
            case CameraType::Perspective:
            {
                const auto* persp = castCamera< PerspectiveCamera > ( cam );
                return computeViewMatrixHelper ( persp->position, persp->target, persp->up );
            }
            default:
                return Eigen::Matrix4d::Identity ();
        }
    }

    /**
     * @brief 统一的分发函数：在运行时根据相机投影几何，极速生成 3D 投影矩阵
     */
    /**
     * @brief 统一的分发函数：在运行时根据相机投影几何，极速生成 3D 投影矩阵 (Vulkan 1.4 标准版)
     */
    [[nodiscard]] inline Eigen::Matrix4d computeCameraProjectionMatrix ( const Camera& cam ) noexcept
    {
        const CameraType type = getCameraType ( cam );

        switch ( type )
        {
            case CameraType::Orthographic:
            {
                const auto* ortho = castCamera< OrthographicCamera > ( cam );
                Eigen::Matrix4d proj = Eigen::Matrix4d::Zero ();

                // 🚀 核心重构：Vulkan 标准正交投影公式 (近平面映射到 0.0，远平面映射到 1.0)
                proj ( 0, 0 ) = 2.0 / ( ortho->right - ortho->left );
                proj ( 1, 1 ) = 2.0 / ( ortho->top - ortho->bottom );
                proj ( 2, 2 ) = -1.0 / ( ortho->far_clip - ortho->near_clip );   // 🚀 Vulkan 对齐
                proj ( 0, 3 ) = -( ortho->right + ortho->left ) / ( ortho->right - ortho->left );
                proj ( 1, 3 ) = -( ortho->top + ortho->bottom ) / ( ortho->top - ortho->bottom );
                proj ( 2, 3 ) = -ortho->near_clip / ( ortho->far_clip - ortho->near_clip );   // 🚀 Vulkan 对齐
                proj ( 3, 3 ) = 1.0;

                return proj;
            }
            case CameraType::Perspective:
            {
                const auto* persp = castCamera< PerspectiveCamera > ( cam );
                const double tan_half_fov = std::tan ( ( persp->fov_y * 3.14159265358979323846 / 180.0 ) * 0.5 );
                Eigen::Matrix4d proj = Eigen::Matrix4d::Zero ();

                // 🚀 核心重构：Vulkan 标准透视投影公式 (近平面映射到 0.0，远平面映射到 1.0)
                proj ( 0, 0 ) = 1.0 / ( persp->aspect * tan_half_fov );
                proj ( 1, 1 ) = 1.0 / tan_half_fov;
                proj ( 2, 2 ) = -persp->far_clip / ( persp->far_clip - persp->near_clip );   // 🚀 Vulkan 对齐
                proj ( 2, 3 ) = -( persp->far_clip * persp->near_clip ) /
                                ( persp->far_clip - persp->near_clip );   // 🚀 Vulkan 对齐
                proj ( 3, 2 ) = -1.0;

                return proj;
            }
            default:
                return Eigen::Matrix4d::Identity ();
        }
    }
}   // namespace StuCanvas
