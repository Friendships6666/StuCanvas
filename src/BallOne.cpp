#include <iostream>
#include <vector>
#include <deque>          // 新增 deque 支持
#include <filesystem>
#include <algorithm>

#include "video/BallOne/config.hpp"
#include "video/BallOne/render_utils.hpp"
#include "video/BallOne/ffmpeg_utils.hpp"
#include "video/BallOne/physics_utils.hpp"
#include "video/BallOne/video_decoder.hpp"

using namespace BallOne;

// 轨迹点结构体
struct TrailPoint {
    b2Vec2 pos;
    float radius;
};

int main() {
    std::filesystem::create_directories(OUT_DIR);
    std::string laugh_path = "/home/friendships666/Projects/WASMTest/assets/audios/video/DanNiEr.mp4";
    std::string temp_raw = "ball_one_raw.mp4";
    std::string final_out = OUT_DIR + "EvoChallenge_Trail.mp4";

    auto laugh_frames = load_overlay_video(laugh_path, 600);
    auto laugh_pcm = VideoEncoder::extract_audio(laugh_path, 44100);
    auto impact_pcm = VideoEncoder::extract_audio(AUDIO_SOURCE, 44100);

    const int sr = 44100;
    const int channels = 2;
    std::vector<float> main_audio_buf(sr * channels * DURATION, 0.0f);

    int v_idx = 0;
    size_t a_ptr = 0;
    float last_hit_time = -1.0f;
    float ball_r = BALL_START_RADIUS;
    int poly_sides = START_SIDES;

    // 维护过去 10 帧的轨迹队列
    std::deque<TrailPoint> trails;
    const int TRAIL_LENGTH = 30;

    {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {0.0f, 1.4f};
        wd.hitEventThreshold = 0.0001f;
        b2WorldId wId = b2CreateWorld(&wd);
        b2World_SetRestitutionThreshold(wId, 0.0001f);

        b2BodyDef cbd = b2DefaultBodyDef();
        cbd.position = {WIDTH/2.0f/PTM_RATIO, HEIGHT/2.0f/PTM_RATIO};
        b2BodyId cId = b2CreateBody(wId, &cbd);
        Physics::create_thick_container(wId, cId, CONTAINER_RADIUS, poly_sides);

        b2BodyDef bbd = b2DefaultBodyDef();
        bbd.type = b2_dynamicBody; bbd.isBullet = true;
        bbd.position = {cbd.position.x + 0.04f, cbd.position.y + 0.04f};
        b2BodyId bId = b2CreateBody(wId, &bbd);

        b2ShapeDef bsd = b2DefaultShapeDef();
        bsd.material.restitution = 1.05f;
        bsd.enableHitEvents = true;
        b2Circle circle = {{0.0f, 0.0f}, ball_r};
        b2ShapeId bShapeId = b2CreateCircleShape(bId, &bsd, &circle);
        b2Body_SetLinearVelocity(bId, {0.3f, 0.3f});

        VideoEncoder enc(temp_raw);
        std::vector<uint8_t> frame_buf(WIDTH * HEIGHT * 4);

        std::cout << ">>> 开始渲染：带彩色拖尾的进化挑战..." << std::endl;

        for (int f = 0; f < TOTAL_FRAMES; ++f) {
            float now = (float)f / FPS;
            size_t audio_offset = (size_t)(now * sr) * channels;
            size_t samples_per_frame = (sr / FPS) * channels;

            b2World_Step(wId, 1.0f / FPS, 24);
            b2ContactEvents ce = b2World_GetContactEvents(wId);

            if (ce.hitCount > 0) {
                last_hit_time = now;
                if(!impact_pcm.empty()) {
                    for(size_t k=0; k < impact_pcm.size() && (audio_offset + k) < main_audio_buf.size(); ++k)
                        main_audio_buf[audio_offset + k] += impact_pcm[k] * 0.7f;
                }

                if (ball_r < (CONTAINER_RADIUS * 0.95f)) {
                    ball_r += BALL_GROWTH;
                    poly_sides = std::min(poly_sides + 1, 120);

                    if (poly_sides <= 21 || poly_sides % 10 == 0) {
                        int sc = b2Body_GetShapeCount(cId);
                        std::vector<b2ShapeId> shapes(sc);
                        b2Body_GetShapes(cId, shapes.data(), sc);
                        for(auto s : shapes) b2DestroyShape(s, false);
                        Physics::create_thick_container(wId, cId, CONTAINER_RADIUS, poly_sides);
                    }
                    b2DestroyShape(bShapeId, true);
                    b2Circle nc = {{0.0f, 0.0f}, ball_r};
                    bShapeId = b2CreateCircleShape(bId, &bsd, &nc);
                }
            }




            // 记录当前帧物理状态到轨迹队列
            b2Vec2 p = b2Body_GetPosition(bId);
            trails.push_front({p, ball_r});
            if (trails.size() > TRAIL_LENGTH) {
                trails.pop_back(); // 保持队列长度为10
            }

            // --- 渲染层 ---
            Render::clear(frame_buf, COLOR_BG);
            Render::draw_evolving_container(frame_buf, CONTAINER_RADIUS, poly_sides, COLOR_CONTAINER);

            // 倒序绘制轨迹（先画最老的，再画新的，防止旧覆盖新）
            for (int i = trails.size() - 1; i >= 1; --i) {
                float t = 1.0f - ((float)i / (float)TRAIL_LENGTH); // 进度：0.0(最尾端) -> 近似1.0(头部前一帧)

                // 颜色渐变：从电光青(尾巴) 过渡到 荧光粉(头部)
                uint32_t trail_color = Render::lerp_color(COLOR_CONTAINER, COLOR_BALL, t);

                // 透明度渐变：尾巴很淡(0.1)，靠近头部清晰(0.6)
                float alpha = 0.1f + 0.5f * t;

                Render::draw_ball_alpha(frame_buf,
                                        trails[i].pos.x * PTM_RATIO,
                                        trails[i].pos.y * PTM_RATIO,
                                        trails[i].radius * PTM_RATIO,
                                        trail_color, alpha);
            }

            // 绘制当前球 (完全不透明)
            Render::draw_ball(frame_buf, p.x*PTM_RATIO, p.y*PTM_RATIO, ball_r*PTM_RATIO, COLOR_BALL);

            // --- 大笑音画同步逻辑 ---
            if (!laugh_frames.empty()) {
                // 【核心修复】：根据音频的播放进度，精确计算当前应该显示哪一帧画面
                if (!laugh_pcm.empty()) {
                    double progress = (double)a_ptr / laugh_pcm.size(); // 计算播放百分比
                    v_idx = (int)(progress * laugh_frames.size());      // 映射到视频帧
                    if (v_idx >= laugh_frames.size()) v_idx = 0;        // 防止越界
                }

                Render::blend_overlay(frame_buf, laugh_frames[v_idx], 0.6f);

                if (now - last_hit_time < 0.5f) {
                    // 播放状态：只推进音频指针，画面帧会自动跟上
                    if (!laugh_pcm.empty()) {
                        for (size_t k = 0; k < samples_per_frame; ++k) {
                            if (a_ptr + k < laugh_pcm.size() && (audio_offset + k) < main_audio_buf.size()) {
                                main_audio_buf[audio_offset + k] += laugh_pcm[a_ptr + k] * 0.6f;
                            }
                        }
                        a_ptr = (a_ptr + samples_per_frame) % laugh_pcm.size();
                    } else {
                        // 备用：如果视频没有声音，按近似 30FPS 速度推进画面
                        v_idx = (v_idx + (60 / 30)) % laugh_frames.size();
                    }
                } else {
                    // 重置状态：只需要把音频指针归零，画面自然会回到第 0 帧
                    a_ptr = 0;
                }
            }

            enc.encode(frame_buf, f);
            if(f % 120 == 0) std::cout << "\r进度: " << (f * 100 / TOTAL_FRAMES) << "%" << std::flush;
        }
        b2DestroyWorld(wId);
    }

    float max_a = 0;
    for (float s : main_audio_buf) max_a = std::max(max_a, std::abs(s));
    if (max_a > 0.98f) {
        float scale = 0.98f / max_a;
        for (float& s : main_audio_buf) s *= scale;
    }

    std::cout << "\n>>> 模拟结束，进行后期合成..." << std::endl;
    VideoEncoder::finalize(main_audio_buf, temp_raw, final_out);
    std::remove(temp_raw.c_str());

    return 0;
}