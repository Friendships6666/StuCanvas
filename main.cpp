#include "stucanvas/canvas/canvas.hpp"
#include <Eigen/Dense>
#include <iostream>
#include <vector>
#include <cmath>

int main() {
    using namespace StuCanvas;

    NLECanvas<double> canvas;

    // 1. 相机初始配置
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.fov = 60.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 500.0f;

    // 2. 创建长动画片段
    Clip<double> clip(0, 2000000);
    clip.name = "Bézier_Stress_Test";
    canvas.AddClip(clip);

    auto active_clips = canvas.GetClipsAtFrame(0);
    if (!active_clips.empty()) {
        auto* target_clip = active_clips[0];

        target_clip->update_func = [target_clip, &cam](uint64_t rel_frame) {
            target_clip->paths.clear();
            target_clip->points.clear(); // 用于锚点

            double t = static_cast<double>(rel_frame) * 0.016;

            // --- A. 相机综合运动：缩放(Zoom) + 轨道(Orbit) ---
            // 距离在 5.0 到 30.0 之间剧烈变化
            double dist = 18.0 + 13.0 * std::sin(t * 0.7);
            cam.posX = dist * std::cos(t * 0.5);
            cam.posZ = dist * std::sin(t * 0.5);
            cam.posY = 10.0 * std::cos(t * 0.3);

            // FOV 动态缩放：产生镜头拉伸感
            cam.fov = 60.0f + static_cast<float>(20.0 * std::sin(t * 1.1));

            // --- B. 曲线自身旋转矩阵 ---
            // 让曲线绕着自己的对角轴线旋转
            Eigen::Vector3d axis(1.0, 1.0, 0.5);
            Eigen::Matrix3d selfRot = Eigen::AngleAxisd(t * 2.0, axis.normalized()).toRotationMatrix();

            // --- C. 定义原始控制点 (S形) ---
            std::vector<Eigen::Vector3d> base_pts = {
                {-5.0, -4.0, 0.0}, // P0
                {-2.0,  8.0, 3.0}, // P1
                { 2.0, -8.0, -3.0},// P2
                { 5.0,  4.0, 0.0}  // P3
            };

            Path3D<double> path;
            for (auto& bp : base_pts) {
                Eigen::Vector3d rotated_p = selfRot * bp;
                Point3D<double> pt{rotated_p.x(), rotated_p.y(), rotated_p.z()};

                // 赋予极端对比色以便观察颜色插值
                pt.r = (bp.x() < 0) ? 1.0f : 0.0f;
                pt.g = (bp.y() > 0) ? 1.0f : 0.0f;
                pt.b = (bp.z() != 0) ? 1.0f : 0.5f;
                pt.a = 1.0f;
                path.control_points.push_back(pt);
            }
            target_clip->paths.push_back(path);

            // --- D. 空间锚点 (关键：保持 AABB 稳定以防牛顿法抖动) ---
            Point3D<double> a1(-12, -12, -12); a1.a = 0;
            Point3D<double> a2( 12,  12,  12); a2.a = 0;
            target_clip->points.push_back(a1);
            target_clip->points.push_back(a2);
        };
    }

    std::cout << ">>> Bézier Dynamic Stress Test Active <<<" << std::endl;
    std::cout << "Observing: Rotation Stability, Distance Scale, and AA quality." << std::endl;

    canvas.render(0, 60);

    return 0;
}