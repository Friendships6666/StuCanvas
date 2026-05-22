//
// Created by friendships666 on 5/22/26.
//

#ifndef STUCANVAS_CONFIG_HPP
#define STUCANVAS_CONFIG_HPP
#include <cstdint>
#include <string>

namespace StuCanvas::Vulkan
{
    enum class VideoCodec
    {
        AV1, // 推荐用于 8K，极致压缩
        HEVC, // 即 H.265，平衡兼容性与画质
        H264 // 即 AVC，最大兼容性（通常限制在 4K）
    };

    struct ExportSettings
    {
        // --- 基础配置 ---
        VideoCodec codec = VideoCodec::AV1;
        uint32_t width = 2560; // 默认 8K
        uint32_t height = 1600;
        std::string outputPath = "output.ivf"; // 原生编码通常输出 IVF 容器

        // --- 编码质量配置 ---
        uint32_t bitRateKbps = 50000; // 50 Mbps，对于 8K AV1 较合适
        uint32_t maxBitRateKbps = 80000;
        uint32_t gopSize = 1; // 关键帧间隔（通常设为与 FPS 一致）

        // 编码速度/质量平衡 (1-7): 1 最快, 7 质量最好 (对应 NVENC 预设)
        uint32_t tuningPreset = 7;
    };
}
#endif //STUCANVAS_CONFIG_HPP