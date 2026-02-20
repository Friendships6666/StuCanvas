#pragma once
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <utility>

namespace gpu {

enum Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

class Camera {
public:
    Eigen::Vector3f position;
    float yaw, pitch;
    float movementSpeed = 10.0f;
    float mouseSensitivity = 0.1f;

    // 缓存向量以优化 getViewMatrix 和 processKeyboard
    Eigen::Vector3f front, right, up;

    Camera(Eigen::Vector3f startPos = {0.0f, -10.0f, 5.0f})
        : position(std::move(startPos)), yaw(90.0f), pitch(0.0f) {
        updateCameraVectors();
    }

    /**
     * @brief 直接构造 View 矩阵
     * 性能优化：直接填充元素，跳过矩阵乘法 R * T。
     * 布局：RHS, Z-Up, 相机看向 View-Space -Z。
     */
    Eigen::Matrix4f getViewMatrix() const {
        Eigen::Matrix4f view;
        // 第一行：Right
        view.row(0) << right.transpose(), -right.dot(position);
        // 第二行：Up
        view.row(1) << up.transpose(),    -up.dot(position);
        // 第三行：-Front (看向视线反方向)
        view.row(2) << -front.transpose(), front.dot(position);
        // 第四行
        view.row(3) << 0.0f, 0.0f, 0.0f, 1.0f;
        return view;
    }

    /**
     * @brief 键盘移动
     * 性能优化：利用缓存的向量，且针对 FPS 走位简化了平面方向计算。
     */
    void processKeyboard(Camera_Movement direction, float deltaTime) {
        const float velocity = movementSpeed * deltaTime;

        // FPS 走位：前后移动仅在 XY 平面（忽略视线垂直分量）
        Eigen::Vector3f flatFront = {front.x(), front.y(), 0.0f};
        flatFront.normalize();
        // 注：由于 right 是由 front x (0,0,1) 得到，其 Z 轴本就是 0，无需额外处理

        switch (direction) {
            case FORWARD:  position += flatFront * velocity; break;
            case BACKWARD: position -= flatFront * velocity; break;
            case LEFT:     position -= right * velocity; break;
            case RIGHT:    position += right * velocity; break;
            case UP:       position.z() += velocity; break;
            case DOWN:     position.z() -= velocity; break;
        }
    }

    void processMouseMovement(float xoffset, float yoffset) {
        yaw   += -xoffset * mouseSensitivity;
        pitch += yoffset * mouseSensitivity;
        pitch = std::clamp(pitch, -89.0f, 89.0f);

        updateCameraVectors();
    }

private:
    /**
     * @brief 更新正交基
     * 只有旋转改变时调用。
     */
    void updateCameraVectors() {
        static constexpr float deg2rad = 0.0174532925f;
        const float ry = yaw * deg2rad;
        const float rp = pitch * deg2rad;
        const float cp = std::cos(rp);

        // 1. 计算视线方向 (RHS Z-Up)
        front << cp * std::cos(ry),
                 cp * std::sin(ry),
                 std::sin(rp);
        front.normalize();

        // 2. 优化叉乘 (Front x [0,0,1])
        // 结果: x = f.y, y = -f.x, z = 0
        right << front.y(), -front.x(), 0.0f;
        right.normalize();

        // 3. 计算相机 Up
        up = right.cross(front).normalized();
    }
};

} // namespace gpu