#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using namespace StuCanvas;

int main() {
    // 1. 初始化画布 (60 FPS)
    NLECanvas<double> canvas;
    uint32_t fps = 60;
    canvas.SetFPS(fps);

    // 2. 配置相机
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.fov = 60.0f;
    cam.SetPosition(0, 0, 20); // 相机距离原点 20 单位
    cam.SetLookAt(0, 0, 0);

    // 3. 创建一个 5 秒的测试片段 (60 fps * 5s = 300 frames)
    auto* simpleClip = canvas.CreateClip(0, 300);
    simpleClip->name = "Simple_5s_Test";

    simpleClip->update_func = [](Clip<double>& self, uint64_t rel_f, double rel_ms) {
        // 清理上一帧数据
        self.segments.clear();
        self.points.clear();

        double t = rel_ms * 0.001; // 转换为秒

        // --- 1. 旋转的正方形 (测试线段渲染) ---
        double size = 5.0;
        double angle = t * 2.0; // 每秒旋转约 114 度

        // 计算旋转后的四个顶点
        auto getRotatedPoint = [&](double ox, double oy) {
            double rx = ox * cos(angle) - oy * sin(angle);
            double ry = ox * sin(angle) + oy * cos(angle);
            Point3D<double> p(rx, ry, 0.0);
            p.r = 0.0f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f; // 青色
            return p;
        };

        Point3D<double> v1 = getRotatedPoint(-size, -size);
        Point3D<double> v2 = getRotatedPoint(size, -size);
        Point3D<double> v3 = getRotatedPoint(size, size);
        Point3D<double> v4 = getRotatedPoint(-size, size);

        // 添加四条边
        SegmentStrip3D<double> s1, s2, v_strip;
        s1.vertices = {v1, v2};
        s2.vertices = {v2, v3};
        self.segments.push_back(s1);
        self.segments.push_back(s2);

        v_strip.vertices = {v3, v4, v1}; // 连续线段
        self.segments.push_back(v_strip);

        // --- 2. 跳动的核心点 (测试点云渲染) ---
        Point3D<double> center(0, 0, sin(t * 5.0) * 2.0); // 在 Z 轴来回移动
        center.r = 1.0f; center.g = 0.5f; center.b = 0.0f; center.a = 1.0f; // 橙色
        self.points.push_back(center);
    };

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "StuCanvas Simplified 5s Render Test" << std::endl;
    std::cout << "Duration: 5 Seconds (" << 300 << " frames)" << std::endl;
    std::cout << "Logic: Rotating Square + Pulsating Center Point" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    // 执行导出（会自动调用 Headless Vulkan 编码逻辑）
    try {
        canvas.render();
        std::cout << "\nExport Completed Successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\nExport Failed: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}