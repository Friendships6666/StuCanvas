#pragma once
#include <box2d/box2d.h>
#include <string>
#include <vector>
#include <cstdint>

namespace BallOne {
    const int WIDTH = 1080;
    const int HEIGHT = 1920;
    // 1米对应 250 像素，确保在 1080p 屏幕下的视觉比例合适
    const float PTM_RATIO = 250.0f;

    const int FPS = 60;
    const int DURATION = 45;
    const int TOTAL_FRAMES = FPS * DURATION;
    // 逻辑参数
    const float CONTAINER_RADIUS = 2.0f;     // 外框半径（米）
    const float BALL_START_RADIUS = 0.15f;   // 球初始半径
    const float BALL_GROWTH = 0.04f;        // 每次碰撞增加量
    const int START_SIDES = 4;               // 从正方形开始

    // 视觉颜色
    const uint32_t COLOR_BG = 0x050508;
    const uint32_t COLOR_CONTAINER = 0x00FFCC;
    const uint32_t COLOR_BALL = 0xFF0077;

    const std::string OUT_DIR = "/home/friendships666/StuCanvasVideo/BallOne/";
    const std::string AUDIO_SOURCE = "/home/friendships666/Projects/WASMTest/assets/audios/video/nothing.ogg";
    const std::string FONT_FILE = "/home/friendships666/Projects/WASMTest/assets/fonts/NotoSansSC-Regular.ttf";

    struct ImpactEvent { float time; float volume; };
}