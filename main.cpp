#include "stucanvas/canvas/canvas.hpp"
#include <Eigen/Dense>
#include <iostream>
#include <vector>

int main() {
    using namespace StuCanvas;

    NLECanvas<double> canvas;

    // 1. 设置透视相机
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.posX = 8.0; cam.posY = 8.0; cam.posZ = 12.0; // 侧上方斜视视角
    cam.lookX = 0.0; cam.lookY = 0.0; cam.lookZ = 0.0;
    cam.fov = 60.0f;

    // 2. 创建长动画片段
    Clip<double> clip(0, 1000000);
    clip.name = "Triangle_Cube_Test";
    canvas.AddClip(clip);

    auto active_clips = canvas.GetClipsAtFrame(0);
    if (!active_clips.empty()) {
        auto* target_clip = active_clips[0];

        // 定义正方体的 8 个顶点 (边长为 4)
        std::vector<Eigen::Vector3d> cube_verts = {
            {-2.0, -2.0, -2.0}, { 2.0, -2.0, -2.0}, { 2.0,  2.0, -2.0}, {-2.0,  2.0, -2.0}, // 底 0,1,2,3
            {-2.0, -2.0,  2.0}, { 2.0, -2.0,  2.0}, { 2.0,  2.0,  2.0}, {-2.0,  2.0,  2.0}  // 顶 4,5,6,7
        };

        // 绑定更新函数
        target_clip->update_func = [target_clip, cube_verts](uint64_t rel_frame) {
            target_clip->triangles.clear();
            target_clip->points.clear();

            double t = static_cast<double>(rel_frame) * 0.02;

            // 创建一个慢速旋转矩阵
            Eigen::Matrix3d rot;
            rot = Eigen::AngleAxisd(t, Eigen::Vector3d::UnitY())
                * Eigen::AngleAxisd(t * 0.5, Eigen::Vector3d::UnitX());

            // 构建三角面对象
            Triangles3D<double> mesh;

            // 1. 填充顶点数据并应用旋转
            for (int i = 0; i < 8; ++i) {
                Eigen::Vector3d v_rot = rot * cube_verts[i];
                Point3D<double> p{v_rot.x(), v_rot.y(), v_rot.z()};

                // 给正方体顶点赋予彩虹色，测试插值
                p.r = static_cast<float>(0.5 + 0.5 * cube_verts[i].x() / 2.0);
                p.g = static_cast<float>(0.5 + 0.5 * cube_verts[i].y() / 2.0);
                p.b = static_cast<float>(0.5 + 0.5 * cube_verts[i].z() / 2.0);
                p.a = 1.0f;
                mesh.points.push_back(p);
            }

            // 2. 定义 12 个三角形索引 (6个面 x 2个三角形)
            // 注意卷绕顺序 (右手定则，逆时针为正)
            std::vector<uint32_t> indices = {
                0, 3, 2, 0, 2, 1, // 后
                4, 5, 6, 4, 6, 7, // 前
                0, 1, 5, 0, 5, 4, // 下
                2, 3, 7, 2, 7, 6, // 上
                0, 4, 7, 0, 7, 3, // 左
                1, 2, 6, 1, 6, 5  // 右
            };
            mesh.indices = indices;

            target_clip->triangles.push_back(mesh);

            // 3. 锚点保护 (防止归一化抖动)
            Point3D<double> a1(-5, -5, -5); a1.a = 0;
            Point3D<double> a2( 5,  5,  5); a2.a = 0;
            target_clip->points.push_back(a1);
            target_clip->points.push_back(a2);
        };
    }

    std::cout << ">>> Running 3D Triangle Cube Test <<<" << std::endl;
    std::cout << "Using SSBO vertices and hardware derivative normals." << std::endl;

    canvas.render(0, 60);

    return 0;
}