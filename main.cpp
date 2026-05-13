#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <Eigen/Dense>
#include <iostream>
#include <cmath>

int main() {
    using namespace StuCanvas;

    NLECanvas<double> canvas;

    // 1. 设置透视相机 (右手坐标系)
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.posX = 0.0;
    cam.posY = 0.0;
    cam.posZ = 12.0;    // 稍微拉远，给复杂的打结结构留足空间
    cam.lookX = 0.0;
    cam.lookY = 0.0;
    cam.lookZ = 0.0;
    cam.upX = 0.0f; cam.upY = 1.0f; cam.upZ = 0.0f;
    cam.fov = 60.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;

    // 2. 创建动画片段
    Clip<double> clip(0, 999999);
    clip.name = "Neon_Gravity_Knot";

    canvas.AddClip(clip);

    auto active_clips = canvas.GetClipsAtFrame(0);
    if (!active_clips.empty()) {
        auto* target_clip = active_clips[0];

        target_clip->update_func = [target_clip](uint64_t rel_frame) {
            // 清理上一帧的点和线
            target_clip->points.clear();
            target_clip->segments.clear();

            double time = static_cast<double>(rel_frame) / 60.0;

            // --- A. 全局展示旋转矩阵 ---
            // 让整个打结结构在太空中缓缓自转
            Eigen::Vector3d axis(std::sin(time * 0.2), 1.0, std::cos(time * 0.3));
            Eigen::Matrix3d rotMat = Eigen::AngleAxisd(time * 0.4, axis.normalized()).toRotationMatrix();

            // --- B. 生成动态的霓虹线段带 (Segment Strips) ---
            int num_strips = 5;         // 5 条互相缠绕的能量带
            int points_per_strip = 300; // 每条带有 300 个点 (即 299 条独立线段)

            // 动态改变打结的参数，产生形变
            double p = 3.0 + 1.5 * std::sin(time * 0.5);
            double q = 5.0 + 2.0 * std::cos(time * 0.3);

            for (int s = 0; s < num_strips; ++s) {
                SegmentStrip3D<double> strip;
                // 为了避免带子重合，给每一条带子加上相位偏移
                double phase_offset = (static_cast<double>(s) / num_strips) * 2.0 * M_PI;

                for (int i = 0; i < points_per_strip; ++i) {
                    // u 是在一条带子上的相对位置 [0, 2π]
                    double u = (static_cast<double>(i) / (points_per_strip - 1)) * 2.0 * M_PI;

                    // 1. 计算三维坐标 (动态环面结 Torus Knot)
                    double R = 4.0; // 主环半径
                    double r = 1.5 + 0.5 * std::sin(time * 2.0 + u * 4.0); // 截面半径产生蠕动感

                    double x = std::sin(p * u + phase_offset) * (R + r * std::cos(q * u));
                    double y = std::cos(p * u + phase_offset) * (R + r * std::cos(q * u));
                    double z = r * std::sin(q * u);

                    // 应用全局旋转
                    Eigen::Vector3d pt_vec(x, y, z);
                    pt_vec = rotMat * pt_vec;

                    // 2. 创建点并赋予色彩
                    Point3D<double> pt{pt_vec.x(), pt_vec.y(), pt_vec.z()};

                    // 颜色沿着线条平滑渐变，并随时间流动
                    pt.r = static_cast<float>(0.5 + 0.5 * std::sin(u * 2.0 + time * 3.0 + phase_offset));
                    pt.g = static_cast<float>(0.5 + 0.5 * std::cos(u * 3.0 - time * 2.0));
                    pt.b = static_cast<float>(0.5 + 0.5 * std::sin(u * 1.5 + time * 4.0 - phase_offset));
                    pt.a = 1.0f;

                    strip.vertices.push_back(pt);
                }

                // 将组装好的线段带送入 clip
                target_clip->segments.push_back(strip);
            }

            // --- C. 星尘伴飞 (点云粒子) ---
            // 在能量带周围生成一些散落的星光点缀
            int num_particles = 150;
            for (int i = 0; i < num_particles; ++i) {
                // 利用互质大质数生成伪随机分布
                double rnd1 = static_cast<double>((i * 137 + rel_frame * 3) % 1000) / 1000.0;
                double rnd2 = static_cast<double>((i * 251 + rel_frame * 5) % 1000) / 1000.0;
                double rnd3 = static_cast<double>((i * 389 + rel_frame * 7) % 1000) / 1000.0;

                // 让粒子在一个球形空间内游走
                double radius = 7.0 * std::cbrt(rnd1);
                double theta = rnd2 * 2.0 * M_PI;
                double phi = std::acos(2.0 * rnd3 - 1.0);

                Point3D<double> particle;
                particle.x = radius * std::sin(phi) * std::cos(theta);
                particle.y = radius * std::sin(phi) * std::sin(theta);
                particle.z = radius * std::cos(phi);

                // 星尘颜色闪烁 (淡蓝色调)
                particle.r = 0.2f;
                particle.g = 0.8f;
                particle.b = 1.0f;
                particle.a = static_cast<float>(0.5 + 0.5 * std::sin(time * 5.0 + i)); // 随机呼吸闪烁

                target_clip->points.push_back(particle);
            }

            // --- D. 包围盒锚点保护 (必须保留) ---
            // 锁定 [-7.0, 7.0] 的边界，防止线条蠕动时整个画面疯狂抽搐
            Point3D<double> anchorMin{-7.0, -7.0, -7.0}; anchorMin.a = 0.0f;
            Point3D<double> anchorMax{ 7.0,  7.0,  7.0}; anchorMax.a = 0.0f;
            target_clip->points.push_back(anchorMin);
            target_clip->points.push_back(anchorMax);
        };
    }

    std::cout << ">>> Neon Gravity Knot Engaged <<<" << std::endl;
    std::cout << "Rendering Gradient Line Strips + Particles simultaneously..." << std::endl;

    // 3. 开始渲染 (60FPS)
    canvas.render(0, 60);

    return 0;
}