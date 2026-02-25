#pragma once
#include "config.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace Render {

    // TBB 加速：清空缓冲区
    void clear_buffer(std::vector<uint8_t>& buffer, uint8_t r, uint8_t g, uint8_t b) {
        // 按像素并行填充 (注意 buffer 是 1D 数组，但我们可以按像素块并行)
        int total_pixels = SUPER_WIDTH * SUPER_HEIGHT;
        tbb::parallel_for(tbb::blocked_range<int>(0, total_pixels),
            [&](const tbb::blocked_range<int>& range) {
                for (int i = range.begin(); i != range.end(); ++i) {
                    int idx = i * 4;
                    buffer[idx + 0] = r;
                    buffer[idx + 1] = g;
                    buffer[idx + 2] = b;
                    buffer[idx + 3] = 255;
                }
            });
    }

    // TBB 加速：画实心圆
    void draw_solid_circle_super(std::vector<uint8_t>& buffer, int cx, int cy, int radius, uint32_t color) {
        int r_sq = radius * radius;
        int y_min = std::max(0, cy - radius);
        int y_max = std::min(SUPER_HEIGHT - 1, cy + radius);

        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        // 并行处理每一行
        tbb::parallel_for(tbb::blocked_range<int>(y_min, y_max + 1),
            [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y != range.end(); ++y) {
                    // 行内逻辑
                    int x_min = std::max(0, cx - radius);
                    int x_max = std::min(SUPER_WIDTH - 1, cx + radius);

                    for (int x = x_min; x <= x_max; ++x) {
                        int dx = x - cx;
                        int dy = y - cy;
                        if (dx * dx + dy * dy <= r_sq) {
                            int idx = (y * SUPER_WIDTH + x) * 4;
                            buffer[idx + 0] = r;
                            buffer[idx + 1] = g;
                            buffer[idx + 2] = b;
                        }
                    }
                }
            });
    }

    // TBB 加速：画带旋转开口的圆环
    void draw_arc_super(std::vector<uint8_t>& buffer, int cx, int cy, int radius, int thickness, float hole_half_angle, float rotation_angle, uint32_t color) {
        int r_out = radius + thickness / 2;
        int r_in = radius - thickness / 2;
        int r_out_sq = r_out * r_out;
        int r_in_sq = r_in * r_in;

        int y_min = std::max(0, cy - r_out);
        int y_max = std::min(SUPER_HEIGHT - 1, cy + r_out);

        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        tbb::parallel_for(tbb::blocked_range<int>(y_min, y_max + 1),
            [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y != range.end(); ++y) {
                    int x_min = std::max(0, cx - r_out);
                    int x_max = std::min(SUPER_WIDTH - 1, cx + r_out);

                    for (int x = x_min; x <= x_max; ++x) {
                        int dx = x - cx;
                        int dy = y - cy;
                        int dist_sq = dx * dx + dy * dy;

                        if (dist_sq >= r_in_sq && dist_sq <= r_out_sq) {
                            float angle = std::atan2((float)dy, (float)dx);
                            float diff = angle - rotation_angle;
                            while (diff <= -M_PI) diff += 2.0f * M_PI;
                            while (diff >  M_PI) diff -= 2.0f * M_PI;

                            if (std::abs(diff) > hole_half_angle) {
                                int idx = (y * SUPER_WIDTH + x) * 4;
                                buffer[idx + 0] = r;
                                buffer[idx + 1] = g;
                                buffer[idx + 2] = b;
                            }
                        }
                    }
                }
            });
    }

    // TBB 加速：SSAA 下采样 (最耗时的步骤之一)
    void downsample_buffer(const std::vector<uint8_t>& super_buffer, std::vector<uint8_t>& dest_buffer) {
        tbb::parallel_for(tbb::blocked_range<int>(0, HEIGHT),
            [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y != range.end(); ++y) {
                    for (int x = 0; x < WIDTH; ++x) {
                        int r = 0, g = 0, b = 0;
                        for (int dy = 0; dy < SSAA_FACTOR; ++dy) {
                            for (int dx = 0; dx < SSAA_FACTOR; ++dx) {
                                int sx = x * SSAA_FACTOR + dx;
                                int sy = y * SSAA_FACTOR + dy;
                                int s_idx = (sy * SUPER_WIDTH + sx) * 4;
                                r += super_buffer[s_idx + 0];
                                g += super_buffer[s_idx + 1];
                                b += super_buffer[s_idx + 2];
                            }
                        }
                        int d_idx = (y * WIDTH + x) * 4;
                        dest_buffer[d_idx + 0] = (uint8_t)(r / (SSAA_FACTOR * SSAA_FACTOR));
                        dest_buffer[d_idx + 1] = (uint8_t)(g / (SSAA_FACTOR * SSAA_FACTOR));
                        dest_buffer[d_idx + 2] = (uint8_t)(b / (SSAA_FACTOR * SSAA_FACTOR));
                    }
                }
            });
    }
}