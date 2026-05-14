#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using namespace StuCanvas;

/**
 * 辅助函数：生成一个带辐条的机械轮
 */
void AddMechanicalGear(Clip<double>& self, double x, double y, double z, double radius, double angle, float r, float g, float b) {
    const int TEETH = 12;
    const int PRECISION = 40;

    // 1. 绘制轮盘面 (Triangles)
    Triangles3D<double> disc;
    disc.points.push_back({x, y, z}); // 中心点
    for(int i=0; i<=PRECISION; ++i) {
        double a = angle + (i * 2.0 * M_PI / PRECISION);
        disc.points.push_back({x + radius * cos(a), y + radius * sin(a), z});
    }
    for(int i=1; i<=PRECISION; ++i) {
        disc.indices.push_back(0); disc.indices.push_back(i); disc.indices.push_back(i+1);
    }
    for(auto& p : disc.points) { p.r = r*0.3f; p.g = g*0.3f; p.b = b*0.3f; p.a = 0.6f; }
    self.triangles.push_back(disc);

    // 2. 绘制轮缘与辐条 (Segments - 利用 3% 延长产生发光接头)
    for (int i = 0; i < TEETH; ++i) {
        double a = angle + (i * 2.0 * M_PI / TEETH);
        SegmentStrip3D<double> spoke;
        Point3D<double> pCenter(x, y, z);
        Point3D<double> pEdge(x + radius * 1.2 * cos(a), y + radius * 1.2 * sin(a), z);
        pCenter.r = r; pCenter.g = g; pCenter.b = b; pCenter.a = 1.0f;
        pEdge.r = r;   pEdge.g = g;   pEdge.b = b;   pEdge.a = 1.0f;
        spoke.vertices = {pCenter, pEdge};
        self.segments.push_back(spoke);
    }
}

int main() {
    NLECanvas<double> canvas;

    auto& cam = canvas.GetCameraConfig();
    cam.mode = ProjectionMode::Perspective;
    cam.fov = 70.0f;
    cam.nearPlane = 0.1f;

    // 20 秒机械史诗
    auto* clockClip = canvas.CreateClip(0, 1200);
    clockClip->name = "Chronos_Engine_Active";

    clockClip->update_func = [&cam](Clip<double>& self, uint64_t rel_f, double rel_ms) {
        self.segments.clear();
        self.points.clear();
        self.triangles.clear();
        self.paths.clear();

        double t = rel_ms * 0.001;

        // --- 1. 建立机械结构 ---
        // 大中心轮
        AddMechanicalGear(self, 0, 0, 0, 10.0, t * 0.5, 0.0f, 0.8f, 1.0f);
        // 四个行星轮
        for(int i=0; i<4; ++i) {
            double ang = i * M_PI * 0.5 + t * 0.2;
            double gx = 15.0 * cos(ang);
            double gy = 15.0 * sin(ang);
            AddMechanicalGear(self, gx, gy, 0, 4.0, -t * 1.5, 1.0f, 0.5f, 0.0f);

            // --- 2. 能量电弧 (Path3D) ---
            // 在中心轮和行星轮之间拉出贝塞尔曲线电弧
            Path3D<double> arc;
            Point3D<double> pStart(0, 0, 0);
            Point3D<double> pEnd(gx, gy, 0);
            // 动态控制点，让电弧扭动
            Point3D<double> ctrl1(gx*0.5 + sin(t*10)*3, gy*0.5 + cos(t*10)*3, 5.0);
            Point3D<double> ctrl2(gx*0.7, gy*0.7, -5.0);

            for(auto* p : {&pStart, &ctrl1, &ctrl2, &pEnd}) {
                p->r = 1.0f; p->g = 1.0f; p->b = 1.0f; p->a = 1.0f;
            }
            arc.control_points = {pStart, ctrl1, ctrl2, pEnd};
            self.paths.push_back(arc);
        }

        // --- 3. 润滑火花 (Points) ---
        for (int i = 0; i < 100; ++i) {
            double pT = t + i;
            Point3D<double> spark(cos(pT*2)*15, sin(pT*2)*15, sin(pT*5)*2);
            spark.r = 1.0f; spark.g = 0.9f; spark.b = 0.5f; spark.a = (float)std::abs(sin(t*10+i));
            self.points.push_back(spark);
        }

        // --- 4. 镜头动画 (Chronos Tracking) ---
        const double T_PHASE1 = 6000.0;
        const double T_PHASE2 = 12000.0;

        if (rel_ms < T_PHASE1) {
            // 阶段 1：广角巡航
            double dist = ease_in_out(rel_ms, 60.0, 35.0, 0.0, T_PHASE1);
            double rot = ease_in_out(rel_ms, 0.0, M_PI, 0.0, T_PHASE1);
            cam.SetPosition(dist * cos(rot), dist * 0.5, dist * sin(rot));
            cam.SetLookAt(0, 0, 0);
        }
        else if (rel_ms < T_PHASE2) {
            // 阶段 2：穿过齿轮缝隙 (The Precision Dive)
            // 相机从外围俯冲进 Z=0 平面，并从两个轮子中间钻过去
            double curZ = ease_in_out(rel_ms, 35.0, -20.0, T_PHASE1, T_PHASE2);
            double curX = ease_in_out(rel_ms, 0.0, 12.0, T_PHASE1, T_PHASE2); // 偏离中心以钻过缝隙

            cam.SetPosition(curX, 0.0, curZ);
            cam.SetLookAt(curX, 0.0, curZ - 10.0);

            // 伴随 180 度翻转，增加通过狭窄空间的压迫感
            float roll = ease_in_out((float)rel_ms, 0.0f, 180.0f, (float)T_PHASE1, (float)T_PHASE2);
            cam.SetRotation(cam.GetEulerAngles().x(), cam.GetEulerAngles().y(), roll);
        }
        else {
            // 阶段 3：核心觉醒，视角拉回看整体
            double p = (rel_ms - T_PHASE2) / 8000.0;
            cam.SetPosition(ease_out(rel_ms, 12.0, 0.0, T_PHASE2, 20000.0),
                            ease_out(rel_ms, 0.0, 15.0, T_PHASE2, 20000.0),
                            ease_out(rel_ms, -20.0, 40.0, T_PHASE2, 20000.0));
            cam.SetLookAt(0, 0, 0);
            cam.SetRotation(0, 0, (float)rel_ms * 0.1f);
        }
    };

    std::cout << ">>> THE CHRONOS ENGINE IS ONLINE <<<" << std::endl;
    std::cout << "Using Path3D arcs, Extended Segments, and Flat-Shaded Triangles" << std::endl;

    canvas.render(0, 60);

    return 0;
}