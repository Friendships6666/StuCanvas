#pragma once
#include <Eigen/Dense>
#include <cmath>

namespace gpu {

    /**
     * @brief 创建 WebGPU 标准透视投影矩阵 (右手坐标系, 深度 [0, 1])
     *
     * @param fovRad 垂直视野角度 (弧度)
     * @param aspect 宽高比 (width / height)
     * @param zNear 近裁剪面 (必须 > 0)
     * @param zFar 远裁剪面
     * @return Eigen::Matrix4f
     */
    inline Eigen::Matrix4f createPerspective(float fovRad, float aspect, float zNear, float zFar) {
        float f = 1.0f / std::tan(fovRad * 0.5f);
        Eigen::Matrix4f m = Eigen::Matrix4f::Zero();
        m(0, 0) = f / aspect;
        m(1, 1) = f;
        m(2, 2) = zFar / (zNear - zFar); // WebGPU [0, 1]
        m(2, 3) = (zFar * zNear) / (zNear - zFar);
        m(3, 2) = -1.0f; // 右手系：View 空间 Z 是负的，W 必须取反
        return m;
    }

    /**
     * @brief LookAt 矩阵 (右手坐标系)
     * 注意：如果使用了 Camera 类，通常不需要调用此函数，因为 Camera::getViewMatrix 已经处理了。
     * 但为了完整性，这里提供一个兼容的实现。
     */
    inline Eigen::Matrix4f createLookAt(Eigen::Vector3f eye, Eigen::Vector3f center, Eigen::Vector3f up) {
        // 1. 计算基向量
        // Forward (f): 从 eye 指向 center，但在右手系 View 矩阵中，
        // Z 轴是指向屏幕外的（相机背后），所以实际上基向量 z = normalize(eye - center)
        // 但这里我们习惯算 front = center - eye
        Eigen::Vector3f f = (center - eye).normalized(); // Front

        // Right (s): Front x Up
        Eigen::Vector3f s = f.cross(up).normalized();

        // Up (u): Right x Front
        Eigen::Vector3f u = s.cross(f).normalized();

        Eigen::Matrix4f m = Eigen::Matrix4f::Identity();

        // 旋转部分 (转置的旋转矩阵)
        // Row 0: Right
        m(0, 0) = s.x(); m(0, 1) = s.y(); m(0, 2) = s.z();
        // Row 1: Up
        m(1, 0) = u.x(); m(1, 1) = u.y(); m(1, 2) = u.z();
        // Row 2: -Front (因为 View Space Z 轴指向相机后方)
        m(2, 0) = -f.x(); m(2, 1) = -f.y(); m(2, 2) = -f.z();

        // 平移部分
        m(0, 3) = -s.dot(eye);
        m(1, 3) = -u.dot(eye);
        m(2, 3) = f.dot(eye); // 注意这里的符号，因为 z 轴取反了，所以 dot 也变了

        return m;
    }

} // namespace gpu