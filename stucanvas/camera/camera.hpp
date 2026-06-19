// stucanvas/camera/camera.hpp
#pragma once

#include <cmath>
#include <cstdint>
#include "eigen3/Eigen/Dense"

namespace StuCanvas
{

    enum class ProjectionMode : uint32_t
    {
        Orthographic = 0,
        Perspective = 1
    };

    template <typename T>
    struct CameraConfig
    {
        // 绝对世界空间位置
        T posX = 0, posY = 0, posZ = 10;

        // 绝对世界空间观察点 (LookAt Point)
        T lookX = 0, lookY = 0, lookZ = 0;

        // 方向向量 (归一化后方向不变，故用 float 即可)
        float upX = 0.0f, upY = 1.0f, upZ = 0.0f;

        ProjectionMode mode = ProjectionMode::Perspective;

        float fov = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        float orthoWidth = 10.0f;
        float orthoHeight = 0.0f;

        /**
         * @brief 根据点云的归一化参数，变换相机到 [-1, 1] 的 NDC 空间
         */
        [[nodiscard]] CameraConfig<float> GetNormalizedConfig(T centerX, T centerY, T centerZ, T scale) const
        {
            CameraConfig<float> normalized;

            // 1. 位置变换: (WorldPos - Center) / Scale
            normalized.posX = static_cast<float>((posX - centerX) / scale);
            normalized.posY = static_cast<float>((posY - centerY) / scale);
            normalized.posZ = static_cast<float>((posZ - centerZ) / scale);

            // 2. 观察点变换
            normalized.lookX = static_cast<float>((lookX - centerX) / scale);
            normalized.lookY = static_cast<float>((lookY - centerY) / scale);
            normalized.lookZ = static_cast<float>((lookZ - centerZ) / scale);

            // 3. 方向向量保持不变
            normalized.upX = upX;
            normalized.upY = upY;
            normalized.upZ = upZ;

            // 4. 投影参数缩放: 距离和宽度都要除以 Scale
            float s = static_cast<float>(scale);
            normalized.mode = mode;
            normalized.fov = fov;
            normalized.nearPlane = nearPlane / s;
            normalized.farPlane = farPlane / s;
            normalized.orthoWidth = orthoWidth / s;
            normalized.orthoHeight = orthoHeight / s;

            return normalized;
        }


        /**
         * @brief 设置相机在世界空间的位置
         */
        void SetPosition(T x, T y, T z)
        {
            posX = x;
            posY = y;
            posZ = z;
        }

        /**
         * @brief 设置相机看向的目标点
         */
        void SetLookAt(T x, T y, T z)
        {
            lookX = x;
            lookY = y;
            lookZ = z;
        }


        void SetRotation(float yaw_deg, float pitch_deg, float roll_deg = 0.0f)
        {
            // 转为弧度
            float yaw = yaw_deg * (M_PI / 180.0f);
            float pitch = pitch_deg * (M_PI / 180.0f);
            float roll = roll_deg * (M_PI / 180.0f);

            // 1. 计算前向向量 (Forward Vector)
            // 在右手系/Vulkan中，默认前向是 -Z
            float dx = std::cos(pitch) * std::sin(yaw);
            float dy = std::sin(pitch);
            float dz = -std::cos(pitch) * std::cos(yaw);

            // 更新 LookAt 点：基于当前位置 + 方向向量
            lookX = posX + static_cast<T>(dx);
            lookY = posY + static_cast<T>(dy);
            lookZ = posZ + static_cast<T>(dz);

            // 2. 计算上向量 (Up Vector) 考虑 Roll
            // 如果没有 Roll，Up 通常是 (0, 1, 0)
            // 如果有 Roll，我们需要在垂直于前向向量的平面内旋转 Up 向量
            if (std::abs(roll_deg) < 0.0001f)
            {
                upX = 0.0f;
                upY = 1.0f;
                upZ = 0.0f;
            }
            else
            {
                // 计算右向量 (Right = Forward x WorldUp)
                Eigen::Vector3f f(dx, dy, dz);
                f.normalize();
                Eigen::Vector3f worldUp(0, 1, 0);
                Eigen::Vector3f r = f.cross(worldUp).normalized();
                Eigen::Vector3f u = r.cross(f).normalized(); // 修正后的基准上向量

                // 应用 Roll：Up_final = u*cos(roll) - r*sin(roll)
                Eigen::Vector3f finalUp = u * std::cos(roll) - r * std::sin(roll);
                upX = finalUp.x();
                upY = finalUp.y();
                upZ = finalUp.z();
            }
        }

        [[nodiscard]] Eigen::Vector3f GetEulerAngles() const
        {
            float dx = static_cast<float>(lookX - posX);
            float dy = static_cast<float>(lookY - posY);
            float dz = static_cast<float>(lookZ - posZ);
            float pitch = std::asin(dy / std::sqrt(dx * dx + dy * dy + dz * dz));
            float yaw = std::atan2(dx, -dz);
            return Eigen::Vector3f(yaw * (180.0f / M_PI), pitch * (180.0f / M_PI), 0.0f);
        }
    };


    template <typename T>
    inline Eigen::Matrix4f ComputeViewMatrix(const CameraConfig<T>& cam)
    {
        using namespace Eigen;
        Vector3f eye((float)cam.posX, (float)cam.posY, (float)cam.posZ);
        Vector3f center((float)cam.lookX, (float)cam.lookY, (float)cam.lookZ);
        Vector3f up(cam.upX, cam.upY, cam.upZ);

        Vector3f f = (center - eye).normalized(); // 前向 (向里)
        Vector3f s = f.cross(up).normalized(); // 右向
        Vector3f u = s.cross(f); // 上向

        Matrix4f view = Matrix4f::Identity();
        view(0, 0) = s.x();
        view(0, 1) = s.y();
        view(0, 2) = s.z();
        view(0, 3) = -s.dot(eye);
        view(1, 0) = u.x();
        view(1, 1) = u.y();
        view(1, 2) = u.z();
        view(1, 3) = -u.dot(eye);
        // 右手系下，相机前方是 -Z 轴，所以这里使用 -f
        view(2, 0) = -f.x();
        view(2, 1) = -f.y();
        view(2, 2) = -f.z();
        view(2, 3) = f.dot(eye);
        view(3, 0) = 0.0f;
        view(3, 1) = 0.0f;
        view(3, 2) = 0.0f;
        view(3, 3) = 1.0f;
        return view;
    }

    template <typename T>
    inline Eigen::Matrix4f ComputeProjectionMatrix(const CameraConfig<T>& cam, float aspect)
    {
        using namespace Eigen;
        Matrix4f proj = Matrix4f::Zero();
        float n = (float)cam.nearPlane;
        float f = (float)cam.farPlane;

        if (cam.mode == ProjectionMode::Perspective)
        {
            float fovRad = cam.fov * (float)M_PI / 180.0f;
            float focal = 1.0f / std::tan(fovRad * 0.5f);
            proj(0, 0) = focal / aspect;
            proj(1, 1) = -focal; // 翻转Y以适应 Vulkan
            proj(2, 2) = f / (n - f); // 将 -Z 映射到 [0, 1]
            proj(2, 3) = (f * n) / (n - f);
            proj(3, 2) = -1.0f; // W = -Z_view (因为可见物体 Z_view 是负的)
        }
        else
        {
            float w = (float)cam.orthoWidth * 0.5f;
            float h = (cam.orthoHeight != 0.0f) ? (cam.orthoHeight * 0.5f) : (w / aspect);
            proj(0, 0) = 1.0f / w;
            proj(1, 1) = -1.0f / h;
            proj(2, 2) = 1.0f / (n - f);
            proj(2, 3) = n / (n - f);
            proj(3, 3) = 1.0f;
        }
        return proj;
    }

} // namespace StuCanvas

