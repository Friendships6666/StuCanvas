#pragma once
#include "config.hpp"
#include "physics_utils.hpp"
#include "video_decoder.hpp"
#include <vector>
#include <tbb/parallel_for.h>
#include <cmath>
#include <algorithm>

namespace BallOne::Render {
    inline void clear(std::vector<uint8_t>& buf, uint32_t color) {
        uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
        tbb::parallel_for(0, WIDTH * HEIGHT, [&](int i) {
            int idx = i * 4;
            buf[idx+0]=r; buf[idx+1]=g; buf[idx+2]=b; buf[idx+3]=255;
        });
    }

    inline float dist_to_segment(float px, float py, float x1, float y1, float x2, float y2) {
        float l2 = (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
        if (l2 == 0.0f) return sqrtf((px-x1)*(px-x1) + (py-y1)*(py-y1));
        float t = std::max(0.0f, std::min(1.0f, ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2));
        return sqrtf((px-(x1+t*(x2-x1)))*(px-(x1+t*(x2-x1))) + (py-(y1+t*(y2-y1)))*(py-(y1+t*(y2-y1))));
    }

inline void draw_evolving_container(std::vector<uint8_t>& buf, float radius, int sides, uint32_t color) {
        float cx = WIDTH/2.0f, cy = HEIGHT/2.0f;
        float r_px = radius * PTM_RATIO;
        float thick = 8.0f;
        uint8_t cr=(color>>16)&0xFF, cg=(color>>8)&0xFF, cb=color&0xFF;

        tbb::parallel_for(0, HEIGHT, [&](int y) {
            for (int x = 0; x < WIDTH; ++x) {
                float dist_val = 1e9;

                // 性能折中：边数很多时（>60），视觉上直接按圆渲染，
                // 边数较少时，严格按多边形边线渲染
                if (sides < 60) {
                    auto verts = BallOne::Physics::get_poly_vertices(r_px, sides);
                    for (size_t i = 0; i < verts.size(); ++i) {
                        float d = dist_to_segment(x-cx, y-cy, verts[i].x, verts[i].y, verts[(i+1)%verts.size()].x, verts[(i+1)%verts.size()].y);
                        if (d < dist_val) dist_val = d;
                    }
                } else {
                    dist_val = std::abs(sqrtf((x-cx)*(x-cx) + (y-cy)*(y-cy)) - r_px);
                }

                if (dist_val <= thick/2.0f) {
                    int idx = (y*WIDTH+x)*4;
                    buf[idx+0]=cr; buf[idx+1]=cg; buf[idx+2]=cb;
                }
            }
        });
    }



    inline void draw_ball(std::vector<uint8_t>& buf, float bx, float by, float br, uint32_t color) {
        uint8_t cr=(color>>16)&0xFF, cg=(color>>8)&0xFF, cb=color&0xFF;
        int y_min = std::max(0, (int)(by - br)), y_max = std::min(HEIGHT - 1, (int)(by + br));
        tbb::parallel_for(y_min, y_max + 1, [&](int y) {
            for (int x = 0; x < WIDTH; ++x) {
                if (((x-bx)*(x-bx)+(y-by)*(y-by)) <= br*br) {
                    int idx = (y * WIDTH + x) * 4;
                    buf[idx+0]=cr; buf[idx+1]=cg; buf[idx+2]=cb;
                }
            }
        });
    }
    // 大笑视频叠加
    inline void blend_overlay(std::vector<uint8_t>& buf, const VideoFrame& overlay, float alpha) {
        int sx = (WIDTH - overlay.width) / 2;
        int sy = (HEIGHT - overlay.height) / 2;
        tbb::parallel_for(0, overlay.height, [&](int y) {
            for (int x = 0; x < overlay.width; ++x) {
                int ty = sy + y, tx = sx + x;
                if (ty < 0 || ty >= HEIGHT || tx < 0 || tx >= WIDTH) continue;
                int o_idx = (y * overlay.width + x) * 4;
                int b_idx = (ty * WIDTH + tx) * 4;
                for (int i = 0; i < 3; ++i) {
                    buf[b_idx + i] = (uint8_t)(buf[b_idx+i]*(1.0f-alpha) + overlay.data[o_idx+i]*alpha);
                }
            }
        });
    }

    inline uint32_t lerp_color(uint32_t c1, uint32_t c2, float t) {
        uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
        uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
        uint8_t r = r1 + t * (r2 - r1);
        uint8_t g = g1 + t * (g2 - g1);
        uint8_t b = b1 + t * (b2 - b1);
        return (r << 16) | (g << 8) | b;
    }

    // 画带透明度的实心圆 (用于轨迹)
    inline void draw_ball_alpha(std::vector<uint8_t>& buf, float bx, float by, float br, uint32_t color, float alpha) {
        uint8_t cr=(color>>16)&0xFF, cg=(color>>8)&0xFF, cb=color&0xFF;
        int y_min = std::max(0, (int)(by - br)), y_max = std::min(HEIGHT - 1, (int)(by + br));
        int x_min = std::max(0, (int)(bx - br)), x_max = std::min(WIDTH - 1, (int)(bx + br));

        tbb::parallel_for(y_min, y_max + 1, [&](int y) {
            for (int x = x_min; x <= x_max; ++x) {
                if (((x-bx)*(x-bx)+(y-by)*(y-by)) <= br*br) {
                    int idx = (y * WIDTH + x) * 4;
                    buf[idx+0] = (uint8_t)(buf[idx+0] * (1.0f - alpha) + cr * alpha);
                    buf[idx+1] = (uint8_t)(buf[idx+1] * (1.0f - alpha) + cg * alpha);
                    buf[idx+2] = (uint8_t)(buf[idx+2] * (1.0f - alpha) + cb * alpha);
                }
            }
        });
    }

}