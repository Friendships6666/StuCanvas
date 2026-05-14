#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <Eigen/Dense>
#include <iostream>
#include <cmath>
#include <vector>

int main() {
    using namespace StuCanvas;

    NLECanvas<double> canvas;

    // 1. 初始化透视相机
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.fov = 60.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;

    // 2. 创建动画片段
    Clip<double> clip(0, 5000);
    clip.name = "Cube_Genesis";
    canvas.AddClip(clip);

    auto active_clips = canvas.GetClipsAtFrame(0);
    if (!active_clips.empty()) {
        auto* target_clip = active_clips[0];

        // --- A. 预定义正方体几何结构 ---
        // 8 个顶点坐标 (边长为 6 的正方体，中心在原点)
        std::vector<Eigen::Vector3d> verts = {
            {-3.0, -3.0, -3.0}, { 3.0, -3.0, -3.0}, { 3.0, -3.0,  3.0}, {-3.0, -3.0,  3.0}, // 底面 0,1,2,3
            {-3.0,  3.0, -3.0}, { 3.0,  3.0, -3.0}, { 3.0,  3.0,  3.0}, {-3.0,  3.0,  3.0}  // 顶面 4,5,6,7
        };

        // 12 条棱的生长编排数据结构
        struct EdgeGrowth {
            int v_start, v_end;  // 起点和终点索引
            double t_start, t_end; // 生长的起始和结束时间(秒)
        };

        // 精心编排的接力生长顺序：底面圈 -> 竖直立柱 -> 顶面圈
        std::vector<EdgeGrowth> edges = {
            // 1. 底面顺时针展开
            {0, 1, 0.0, 1.5}, {1, 2, 1.0, 2.5}, {2, 3, 2.0, 3.5}, {3, 0, 3.0, 4.5},
            // 2. 四根立柱向上拔起 (带有略微的错位差)
            {0, 4, 4.0, 5.5}, {1, 5, 4.2, 5.7}, {2, 6, 4.4, 5.9}, {3, 7, 4.6, 6.1},
            // 3. 顶面顺时针封口
            {4, 5, 5.5, 7.0}, {5, 6, 6.5, 8.0}, {6, 7, 7.5, 9.0}, {7, 4, 8.5, 10.0}
        };

        // 绑定更新函数
        target_clip->update_func = [target_clip, verts, edges, &cam](uint64_t rel_frame) {
            target_clip->points.clear();
            target_clip->segments.clear();

            double t = static_cast<double>(rel_frame) / 60.0; // 当前秒数

            // --- B. 优雅的运镜动画 ---
            // 相机绕 Y 轴做 360 度缓慢环绕，同时带有轻微的上下升降
            double cam_radius = 16.0;
            cam.posX = cam_radius * std::cos(t * 0.4);
            cam.posZ = cam_radius * std::sin(t * 0.4);
            cam.posY = 6.0 * std::sin(t * 0.2); // 呼吸感升降

            // 永远死死盯住正方体中心
            cam.lookX = 0.0; cam.lookY = 0.0; cam.lookZ = 0.0;

            // --- C. 绘制“蓝图”底点 (全息质感) ---
            // 即使棱还没长出来，先在 8 个角落放置微弱的淡蓝色光点，暗示结构位置
            for (const auto& v : verts) {
                Point3D<double> corner{v.x(), v.y(), v.z()};
                corner.r = 0.0f; corner.g = 0.8f; corner.b = 1.0f;
                corner.a = 0.3f; // 低透明度
                target_clip->points.push_back(corner);
            }

            // --- D. 贝塞尔接力生长逻辑 ---
            for (const auto& edge : edges) {
                // 如果当前时间还没到该线段的生长起点，跳过绘制
                if (t < edge.t_start) continue;

                // 利用 animation.hpp 里的贝塞尔缓动函数计算当前进度 [0.0, 1.0]
                // 内部已做 clamp，如果 t > t_end，progress 自动锁定为 1.0
                double progress = ease_in_out<double, double>(
                    t, 0.0, 1.0, edge.t_start, edge.t_end
                );

                // 根据进度计算当前终点的插值坐标
                Eigen::Vector3d p0 = verts[edge.v_start];
                Eigen::Vector3d p1 = verts[edge.v_end];
                Eigen::Vector3d current_p1 = p0 + (p1 - p0) * progress;

                // 组装生成的线段
                SegmentStrip3D<double> strip;
                Point3D<double> pt0{p0.x(), p0.y(), p0.z()};
                Point3D<double> pt1{current_p1.x(), current_p1.y(), current_p1.z()};

                // 色彩渐变：生长的根部是深蓝色，尖端是高亮的赛博青色
                pt0.r = 0.0f; pt0.g = 0.5f; pt0.b = 1.0f; pt0.a = 1.0f;
                pt1.r = 0.0f; pt1.g = 1.0f; pt1.b = 0.8f; pt1.a = 1.0f;

                strip.vertices.push_back(pt0);
                strip.vertices.push_back(pt1);
                target_clip->segments.push_back(strip);

                // --- E. 激光生长尖端“火花” ---
                // 如果当前线段正在生长中（未完成），在笔尖加上一颗高亮白色粒子

            }

            // --- F. 坐标空间锚点保护 ---
            // 锁定 [-10, 10] 的世界空间，防止包围盒缩放导致画面剧烈跳动
            Point3D<double> a1(-10, -10, -10); a1.a = 0;
            Point3D<double> a2(10, 10, 10); a2.a = 0;
            target_clip->points.push_back(a1);
            target_clip->points.push_back(a2);
        };
    }

    std::cout << ">>> Cube Genesis Animation Initialized <<<" << std::endl;
    std::cout << "Watch the 12 edges grow sequentially with Bezier easing." << std::endl;

    canvas.render(0, 60);

    return 0;
}