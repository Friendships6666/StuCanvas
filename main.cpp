#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using namespace StuCanvas;

int main() {
    // 1. 初始化画布 (使用 double 保证计算精度)
    NLECanvas<double> canvas;

    // 2. 配置相机
    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.fov = 60.0f;
    cam.SetPosition(0, 0, 40); // 相机拉远，观察全局
    cam.SetLookAt(0, 0, 0);

    // 3. 创建一个 60 秒的测试片段 (60 fps * 60s = 3600 frames)
    auto* lineTestClip = canvas.CreateClip(0, 3600);
    lineTestClip->name = "Segment_Stress_Test";

    lineTestClip->update_func = [](Clip<double>& self, uint64_t rel_f, double rel_ms) {
        // 清理上一帧数据
        self.segments.clear();
        self.points.clear();
        self.triangles.clear();
        self.paths.clear();

        double t = rel_ms * 0.001; // 转换为秒

        // ---------------------------------------------------------
        // 测试案例 1：动态彩虹色波浪 (测试颜色插值的平滑度)
        // ---------------------------------------------------------
        const int WAVE_SEGMENTS = 1000;
        const double WAVE_WIDTH = 30.0;

        for (int i = 0; i < WAVE_SEGMENTS; ++i) {
            double x0 = -WAVE_WIDTH/2.0 + (double)i * (WAVE_WIDTH / WAVE_SEGMENTS);
            double x1 = -WAVE_WIDTH/2.0 + (double)(i+1) * (WAVE_WIDTH / WAVE_SEGMENTS);

            double y0 = sin(x0 * 0.5 + t * 2.0) * 5.0;
            double y1 = sin(x1 * 0.5 + t * 2.0) * 5.0;

            SegmentStrip3D<double> wave;

            // 起点颜色
            Point3D<double> p0(x0, y0, 0);
            p0.r = (float)(0.5 + 0.5 * sin(x0 * 0.1 + t));
            p0.g = (float)(0.5 + 0.5 * cos(x0 * 0.1 + t));
            p0.b = 1.0f;
            p0.a = 1.0f;

            // 终点颜色 (必须与下一段起点严格一致)
            Point3D<double> p1(x1, y1, 0);
            p1.r = (float)(0.5 + 0.5 * sin(x1 * 0.1 + t));
            p1.g = (float)(0.5 + 0.5 * cos(x1 * 0.1 + t));
            p1.b = 1.0f;
            p1.a = 1.0f;

            wave.vertices = {p0, p1};
            self.segments.push_back(wave);
        }

        // ---------------------------------------------------------
        // 测试案例 2：旋转的五角星 (测试大角度连接处的重叠)
        // ---------------------------------------------------------
        const int STAR_POINTS = 5;
        double starRadius = 8.0;
        std::vector<Point3D<double>> starVerts;
        for (int i = 0; i < STAR_POINTS * 2; ++i) {
            double r = (i % 2 == 0) ? starRadius : starRadius * 0.4;
            double angle = (double)i * (M_PI / STAR_POINTS) + t;
            Point3D<double> p(r * cos(angle), r * sin(angle), 5.0); // 放在 Z=5 平面

            // 金色到橙色过渡
            p.r = 1.0f;
            p.g = (float)(0.5 + 0.4 * sin(t + (double)i));
            p.b = 0.0f;
            p.a = 1.0f;
            starVerts.push_back(p);
        }

        // 构建连续线段
        for (size_t i = 0; i < starVerts.size(); ++i) {
            SegmentStrip3D<double> edge;
            edge.vertices = { starVerts[i], starVerts[(i + 1) % starVerts.size()] };
            self.segments.push_back(edge);
        }

        // ---------------------------------------------------------
        // 测试案例 3：3D 螺旋线 (测试深度测试与遮挡)
        // ---------------------------------------------------------
        const int HELIX_SAMPLES = 200;
        for (int i = 0; i < HELIX_SAMPLES; ++i) {
            double ang0 = (double)i * 0.2 + t * 3.0;
            double ang1 = (double)(i+1) * 0.2 + t * 3.0;
            double h0 = (double)i * 0.1 - 10.0;
            double h1 = (double)(i+1) * 0.1 - 10.0;

            SegmentStrip3D<double> helix;
            Point3D<double> hp0(12.0 * cos(ang0), h0, 12.0 * sin(ang0));
            hp0.r = 0.0f; hp0.g = 1.0f; hp0.b = 0.5f; hp0.a = 0.8f;

            Point3D<double> hp1(12.0 * cos(ang1), h1, 12.0 * sin(ang1));
            hp1.r = 0.0f; hp1.g = 1.0f; hp1.b = 0.5f; hp1.a = 0.8f;

            helix.vertices = {hp0, hp1};
            self.segments.push_back(helix);
        }
    };

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "StuCanvas Pure Segment Rendering Test" << std::endl;
    std::cout << "1. Rainbow Wave (Color Interpolation)" << std::endl;
    std::cout << "2. Rotating Star (Sharp Corners)" << std::endl;
    std::cout << "3. 3D Helix (Depth & Overlap)" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "Controls: SPACE to Pause, LEFT/RIGHT to step frames." << std::endl;

    // 运行渲染
    canvas.exportVideo();

    return 0;
}