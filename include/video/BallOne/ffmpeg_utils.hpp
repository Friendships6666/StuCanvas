#pragma once
#include "config.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace BallOne {
    class VideoEncoder {
    public:
        VideoEncoder(const std::string& path) {
            avformat_alloc_output_context2(&fmt, nullptr, nullptr, path.c_str());

            // --- 【修改 1】：寻找 NVIDIA NVENC 硬件编码器 ---
            const AVCodec* c = avcodec_find_encoder_by_name("h264_nvenc");
            bool using_hw = true;
            if (!c) {
                std::cout << "[Encoder] h264_nvenc 未找到，回退到软件编码 libx264" << std::endl;
                c = avcodec_find_encoder(AV_CODEC_ID_H264);
                using_hw = false;
            } else {
                std::cout << "[Encoder] 成功加载 NVIDIA RTX 5070 Ti (NVENC) 硬件编码器！" << std::endl;
            }

            st = avformat_new_stream(fmt, c);
            ctx = avcodec_alloc_context3(c);
            ctx->width = WIDTH;
            ctx->height = HEIGHT;
            ctx->time_base = {1, FPS};

            // NVENC 原生最匹配的格式是 NV12
            ctx->pix_fmt = using_hw ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
            ctx->gop_size = 30;
            ctx->max_b_frames = 0;

            if (using_hw) {
                // --- 【修改 2】：NVENC 专属高质量/高速度预设 ---
                // p4 是平衡点，p6/p7 画质最好但稍慢，hq 代表高质量调优
                av_opt_set(ctx->priv_data, "preset", "p4", 0);
                av_opt_set(ctx->priv_data, "tune", "hq", 0);
                // 恒定质量模式 (CQ)，18是非常高的质量
                av_opt_set_int(ctx->priv_data, "cq", 18, 0);
                av_opt_set(ctx->priv_data, "rc", "vbr", 0);
            } else {
                av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
            }

            avcodec_open2(ctx, c, nullptr);
            avcodec_parameters_from_context(st->codecpar, ctx);
            avio_open(&fmt->pb, path.c_str(), AVIO_FLAG_WRITE);
            avformat_write_header(fmt, nullptr);

            sws = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGBA, WIDTH, HEIGHT, ctx->pix_fmt, SWS_FAST_BILINEAR, 0, 0, 0);
            f = av_frame_alloc();
            f->format = ctx->pix_fmt;
            f->width = WIDTH;
            f->height = HEIGHT;
            av_frame_get_buffer(f, 32);
        }

        ~VideoEncoder() {
            av_write_trailer(fmt); avio_closep(&fmt->pb);
            avcodec_free_context(&ctx); av_frame_free(&f); avformat_free_context(fmt);
        }

        void encode(const std::vector<uint8_t>& rgba, int pts) {
            const uint8_t* src[] = {rgba.data()}; int stride[] = {WIDTH*4};
            sws_scale(sws, src, stride, 0, HEIGHT, f->data, f->linesize);
            f->pts = pts; avcodec_send_frame(ctx, f);
            AVPacket* p = av_packet_alloc();
            while(avcodec_receive_packet(ctx, p) == 0) {
                av_packet_rescale_ts(p, ctx->time_base, st->time_base);
                av_interleaved_write_frame(fmt, p); av_packet_unref(p);
            }
            av_packet_free(&p);
        }

        static std::vector<float> extract_audio(const std::string& path, int sr) {
            std::string tmp = "temp_ext.raw";
            std::stringstream ss;
            ss << "ffmpeg -y -v error -i \"" << path << "\" -f f32le -ac 2 -ar " << sr << " " << tmp;
            system(ss.str().c_str());
            std::ifstream is(tmp, std::ios::binary | std::ios::ate);
            if(!is.is_open()) return {};
            std::vector<float> pcm(is.tellg()/4); is.seekg(0); is.read((char*)pcm.data(), pcm.size()*4);
            is.close(); std::remove(tmp.c_str());
            return pcm;
        }

        static void finalize(const std::vector<float>& audio_buf, const std::string& tmp_v, const std::string& out_v) {
            std::ofstream os("f_mix.raw", std::ios::binary);
            os.write((char*)audio_buf.data(), audio_buf.size()*4); os.close();

            // --- 【修改 3】：最后合成文字时，也使用 NVENC 硬件压制 ---
            std::stringstream ss;
            ss << "ffmpeg -y -v warning -i " << tmp_v << " -f f32le -ar 44100 -ac 2 -i f_mix.raw "
               << "-filter_complex \"[0:v]drawtext=fontfile='" << FONT_FILE << "':text='每碰撞一次，边数和大小都会增加！':x=(w-text_w)/2:y=200:fontsize=65:fontcolor=white:shadowcolor=black:shadowx=3:shadowy=3,"
               << "drawtext=fontfile='" << FONT_FILE << "':text='制作软件名\\:':x=(w-900)/2:y=h-250:fontsize=90:fontcolor=white,"
               << "drawtext=fontfile='" << FONT_FILE << "':text='StuCanvas':x=(w-900)/2+510:y=h-250:fontsize=90:fontcolor=0x00FFCC[v]\" "
               // -c:v h264_nvenc 调用显卡，-cq 18 保证视觉无损，-preset p4 速度与画质平衡
               << "-map \"[v]\" -map 1:a -c:v h264_nvenc -preset p4 -cq 18 -c:a aac -movflags +faststart -t " << DURATION << " " << out_v;

            std::cout << "[Mixer] 使用 RTX 5070 Ti 进行最终硬件成片..." << std::endl;
            system(ss.str().c_str());
            std::remove("f_mix.raw");
        }
    private:
        AVFormatContext* fmt; AVCodecContext* ctx; AVStream* st; SwsContext* sws; AVFrame* f;
    };
}