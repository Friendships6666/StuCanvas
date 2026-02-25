#pragma once
#include "config.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class VideoEncoder {
public:
    VideoEncoder(const std::string& filename) {
        avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.c_str());
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        stream = avformat_new_stream(fmt_ctx, codec);
        codec_ctx = avcodec_alloc_context3(codec);

        codec_ctx->width = WIDTH;
        codec_ctx->height = HEIGHT;
        codec_ctx->time_base = {1, FPS};
        codec_ctx->framerate = {FPS, 1};
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->gop_size = 30;
        codec_ctx->max_b_frames = 0;

        av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
        avcodec_open2(codec_ctx, codec, nullptr);
        avcodec_parameters_from_context(stream->codecpar, codec_ctx);
        stream->time_base = {1, FPS};

        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
        }
        avformat_write_header(fmt_ctx, nullptr);

        sws_ctx = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGBA, WIDTH, HEIGHT, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        frame = av_frame_alloc();
        frame->format = codec_ctx->pix_fmt;
        frame->width = WIDTH;
        frame->height = HEIGHT;
        av_frame_get_buffer(frame, 32);
        pkt = av_packet_alloc();
    }

    ~VideoEncoder() {
        if (fmt_ctx) {
            avcodec_send_frame(codec_ctx, nullptr);
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_write_trailer(fmt_ctx);
            avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&codec_ctx);
            av_frame_free(&frame);
            av_packet_free(&pkt);
            sws_freeContext(sws_ctx);
            avformat_free_context(fmt_ctx);
        }
    }

    void encode(const std::vector<uint8_t>& buffer, int pts) {
        const uint8_t* src[] = { buffer.data() };
        int stride[] = { WIDTH * 4 };
        sws_scale(sws_ctx, src, stride, 0, HEIGHT, frame->data, frame->linesize);

        frame->pts = pts;
        if (avcodec_send_frame(codec_ctx, frame) >= 0) {
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
        }
    }

    static void mix_audio(std::vector<ImpactEvent>& impacts, const ScenarioConfig& cfg) {
        std::cout << "[Mixer-" << cfg.id << "] Mixing audio and normalizing..." << std::endl;

        const int sample_rate = 44100;
        const int channels = 2;

        // 1. 转换源音效
        std::string temp_raw_source = "temp_source_" + std::to_string(cfg.id) + ".raw";
        std::stringstream conv_ss;
        conv_ss << "ffmpeg -y -v error -i \"" << AUDIO_SOURCE << "\" -f f32le -ac " << channels << " -ar " << sample_rate << " " << temp_raw_source;
        system(conv_ss.str().c_str());

        // 2. 加载音效
        std::ifstream is(temp_raw_source, std::ios::binary | std::ios::ate);
        if (!is.is_open()) return;
        std::streamsize source_size = is.tellg();
        is.seekg(0, std::ios::beg);
        std::vector<float> source_pcm(source_size / sizeof(float));
        is.read((char*)source_pcm.data(), source_size);
        is.close();
        remove(temp_raw_source.c_str());

        // 3. 初始混合
        size_t total_samples = (size_t)sample_rate * channels * DURATION;
        std::vector<float> main_buffer(total_samples, 0.0f);

        for (const auto& ev : impacts) {
            size_t start_idx = (size_t)(ev.time * sample_rate) * channels;
            for (size_t i = 0; i < source_pcm.size(); ++i) {
                if (start_idx + i < main_buffer.size()) {
                    // 初始叠加
                    main_buffer[start_idx + i] += source_pcm[i] * ev.volume * 1.5f;
                }
            }
        }

        // --- 5. 关键改进：限制最大音量（自动增益控制） ---
        float max_amp = 0.0f;
        for (float s : main_buffer) {
            float abs_s = std::abs(s);
            if (abs_s > max_amp) max_amp = abs_s;
        }

        // 如果峰值超过 0.95，则等比例整体缩小，防止破音
        // 这样既保留了碰撞的动态感，又确保了输出音频的安全性
        if (max_amp > 0.7f) {
            float scale = 0.7f / max_amp;
            std::cout << "[Mixer-" << cfg.id << "] Peak " << max_amp << " detected. Scaling by " << scale << std::endl;
            for (float& s : main_buffer) s *= scale;
        }

        // 6. 写入混合文件
        std::string mixed_raw_path = "mixed_" + std::to_string(cfg.id) + ".raw";
        std::ofstream os(mixed_raw_path, std::ios::binary);
        os.write((char*)main_buffer.data(), main_buffer.size() * sizeof(float));
        os.close();



        // --- 7. 修正后的文字滤镜：双色拼接 ---
        // 增加总宽度预估值，因为文字变长了
        float total_width = 960.0f;      // 整个“制作软件名: StuCanvas”的预估总宽
        float first_part_width = 450.0f; // “制作软件名:” 这六个字符（含冒号）的宽度偏移

        std::stringstream filter;
        // 顶部标题（使用随机选取的 cfg.video_title）
        filter << "drawtext=fontfile='" << FONT_PATH << "':text='全部小球多久能逃离?':"
               << "x=(w-text_w)/2:y=200:fontsize=72:fontcolor=0xFFFFFF:shadowcolor=0x000000:shadowx=3:shadowy=3,"

               // 底部第一部分：'制作软件名:' -> 白色 (注意冒号 \\: 包含在这里)
               << "drawtext=fontfile='" << FONT_PATH << "':text='制作软件\\:':"
               << "x=(w-" << total_width << ")/2:y=h-250:fontsize=90:fontcolor=0xFFFFFF,"

               // 底部第二部分：'StuCanvas' -> 电光青色
               << "drawtext=fontfile='" << FONT_PATH << "':text='StuCanvas':"
               << "x=(w-" << total_width << ")/2 + " << first_part_width << ":y=h-250:fontsize=90:fontcolor=0x00FFCC";

        // 8. 最终合成
        std::stringstream final_cmd;
        final_cmd << "ffmpeg -y -v warning "
                  << "-i " << cfg.temp_video_name << " "
                  << "-f f32le -ar " << sample_rate << " -ac " << channels << " -i " << mixed_raw_path << " "
                  << "-filter_complex \"[0:v]" << filter.str() << "[outv]\" "
                  << "-map \"[outv]\" -map 1:a "
                  << "-c:v libx264 -preset fast -crf 18 "
                  << "-c:a aac -b:a 192k "
                  << "-movflags +faststart "
                  << "-t " << DURATION << " "
                  << cfg.final_video_name;

        system(final_cmd.str().c_str());
        remove(mixed_raw_path.c_str());
        std::cout << "[Mixer-" << cfg.id << "] Finalized scenario " << cfg.id << std::endl;
    }

private:
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVStream* stream = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
};