#include "stucanvas/canvas/canvas.hpp"
#include "stucanvas/canvas/animation.hpp"
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "  StuCanvas NVENC Hardware Transcoder Test " << std::endl;
    std::cout << "=========================================\n" << std::endl;

    try {
        // 1. 实例化 NLECanvas (使用双精度坐标进行高精度动画计算)
        StuCanvas::NLECanvas<double> canvas;
        canvas.SetFPS(60);

        // 2. 配置摄像机属性 (3D 透视视图)
        auto& camera = canvas.GetCameraConfig();
        camera.SetPosition(6.0, 6.0, 10.0);
        camera.SetLookAt(0.0, 0.0, 0.0);
        camera.upX = 0.0f;
        camera.upY = 1.0f;
        camera.upZ = 0.0f;
        camera.mode = StuCanvas::ProjectionMode::Perspective;
        camera.fov = 50.0f;

        // 3. 配置全局渲染和硬件转码属性
        auto& settings = canvas.GetSettings();
        settings.msaaSamples = VK_SAMPLE_COUNT_4_BIT; // 选定 4x MSAA

        auto& exportCfg = settings.exportConfigs;
        // 修复命名空间：VideoCodec 属于 StuCanvas::Vulkan
        exportCfg.codec = StuCanvas::Vulkan::VideoCodec::HEVC;
        exportCfg.width = 1920;
        exportCfg.height = 1080;
        exportCfg.outputPath = "nvenc_animation.h265"; // 生成 H.265 原始码流
        exportCfg.bitRateKbps = 15000;                // 平均码率 15 Mbps
        exportCfg.maxBitRateKbps = 25000;             // 最大峰值码率 25 Mbps
        exportCfg.gopSize = 60;                       // 关键帧(I帧)间隔为 60 帧
        exportCfg.tuningPreset = 6;                   // 编码速度与质量平衡预设 (P6)

        std::cout << "[Setup] Transcoder configured for " << exportCfg.width << "x" << exportCfg.height
                  << " @ 60 FPS (HEVC / H.265)" << std::endl;

        // 4. 创建动画片段 (时间线单位：帧数，范围为 0 至 180)
        auto animation_callback = [](StuCanvas::Clip<double>& clip, uint64_t rel_frame, double rel_ms) {
            clip.points.clear();
            clip.segments.clear();

            // 依据帧数计算渐进的旋转角度偏移
            double angle_offset = rel_frame * 0.06;

            // 显式声明模板参数 <uint64_t, double>，彻底解决 uint64_t 与 unsigned long long 冲突问题
            double scale_factor = StuCanvas::ease_in_out<uint64_t, double>(rel_frame, 0.1, 1.8, 0, 180);

            // A. 生成一个运动中的 3D 双螺旋点云结构
            const int num_points = 250;
            for (int i = 0; i < num_points; ++i) {
                double t = static_cast<double>(i) / num_points;
                double r = t * scale_factor * 3.5;
                double theta = t * 6.0 * M_PI + angle_offset;
                double z = (t - 0.5) * scale_factor * 5.0;

                // 螺旋分支一
                StuCanvas::Point3D<double> p1;
                p1.x = r * std::cos(theta);
                p1.y = r * std::sin(theta);
                p1.z = z;
                p1.r = static_cast<float>(t);
                p1.g = static_cast<float>(1.0 - t);
                p1.b = static_cast<float>(0.5 + 0.5 * std::sin(angle_offset));
                p1.a = 1.0f;
                clip.points.push_back(p1);

                // 螺旋分支二 (相位偏移 180 度)
                StuCanvas::Point3D<double> p2 = p1;
                p2.x = r * std::cos(theta + M_PI);
                p2.y = r * std::sin(theta + M_PI);
                p2.g = static_cast<float>(t * 0.5);
                p2.b = static_cast<float>(1.0 - t);
                clip.points.push_back(p2);
            }

            // B. 生成一个位于中心的旋转波浪线（折线段）
            StuCanvas::SegmentStrip3D<double> spine;
            spine.closed = false;
            const int num_spine_nodes = 30;
            for (int i = 0; i < num_spine_nodes; ++i) {
                double t = static_cast<double>(i) / (num_spine_nodes - 1);
                double z = (t - 0.5) * 6.0;
                double theta = angle_offset * 1.5 + t * M_PI * 2.0;

                StuCanvas::Point3D<double> node;
                node.x = 1.0 * std::cos(theta);
                node.y = 1.0 * std::sin(theta);
                node.z = z;
                node.r = 1.0f;
                node.g = 1.0f;
                node.b = 0.1f;
                node.a = 1.0f;
                spine.vertices.push_back(node);
            }
            clip.segments.push_back(spine);
        };

        // 将该回调函数片段挂载到时间线中
        canvas.CreateClip(0ULL, 180ULL, animation_callback);
        std::cout << "[Timeline] Animated clip registered (Frame: 0 - 180)." << std::endl;

        // 5. 触发物理显存零拷贝的高性能硬转码
        std::cout << "[Transcode] Exporting rendering frames to GPU NVENC stream..." << std::endl;
        canvas.exportYuv(0ULL, 180ULL);

        std::cout << "\n[Success] All processes completed! Output saved to: " << exportCfg.outputPath << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ Error occurred: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}