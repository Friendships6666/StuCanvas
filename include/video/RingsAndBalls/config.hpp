#pragma once
#include <box2d/box2d.h>
#include <string>
#include <cstdint>
#include <vector>

const int WIDTH = 1080;
const int HEIGHT = 1920;
const int SSAA_FACTOR = 2;
const int SUPER_WIDTH = WIDTH * SSAA_FACTOR;
const int SUPER_HEIGHT = HEIGHT * SSAA_FACTOR;
const float PTM_RATIO = 100.0f;

const uint64_t CATEGORY_RING = 0x0001;
const uint64_t CATEGORY_BALL = 0x0002;
const std::string BASE_OUT_PATH = "/home/friendships666/StuCanvasVideo/RingAndBalls/";
const std::string AUDIO_SOURCE = "/home/friendships666/Projects/WASMTest/assets/audios/video/impact1.ogg";
const std::string FONT_PATH = "/home/friendships666/Projects/WASMTest/assets/fonts/NotoSansSC-Regular.ttf";

const int FPS = 60;
const int DURATION = 10; // 每段视频 30 秒
const int TOTAL_FRAMES = FPS * DURATION;

struct ScenarioConfig {
    int id;
    int ball_count;
    int ring_layers;
    float ring_thickness;
    float hole_angle;
    float gravity;
    float base_radius;
    float spacing;
    std::string temp_video_name;
    std::string final_video_name;
    std::string video_title; // 存储随机选中的标题
};

struct Ball {
    b2BodyId id;
    uint32_t color;
    float last_hit_time;
};

struct ImpactEvent {
    float time;
    float volume;
};