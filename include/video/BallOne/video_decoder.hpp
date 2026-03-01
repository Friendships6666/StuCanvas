#pragma once
#include <vector>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace BallOne {
    struct VideoFrame {
        std::vector<uint8_t> data;
        int width, height;
    };

    inline std::vector<VideoFrame> load_overlay_video(const std::string& path, int target_w) {
        std::vector<VideoFrame> frames;
        AVFormatContext* fmt = nullptr;
        avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
        avformat_find_stream_info(fmt, nullptr);

        int stream_idx = -1;
        for (int i = 0; i < fmt->nb_streams; i++) 
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) stream_idx = i;

        AVCodecParameters* params = fmt->streams[stream_idx]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, params);
        avcodec_open2(ctx, codec, nullptr);

        // 计算等比例缩放的高度
        int target_h = (int)(target_w * (float)params->height / params->width);

        SwsContext* sws = sws_getContext(params->width, params->height, ctx->pix_fmt,
                                         target_w, target_h, AV_PIX_FMT_RGBA, SWS_BILINEAR, 0, 0, 0);

        AVFrame* frame = av_frame_alloc();
        AVFrame* rgba_frame = av_frame_alloc();
        int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, target_w, target_h, 1);
        
        AVPacket* pkt = av_packet_alloc();
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == stream_idx) {
                if (avcodec_send_packet(ctx, pkt) >= 0) {
                    while (avcodec_receive_frame(ctx, frame) == 0) {
                        VideoFrame vf;
                        vf.data.resize(buffer_size);
                        vf.width = target_w; vf.height = target_h;
                        uint8_t* dest[] = { vf.data.data() };
                        int dest_stride[] = { target_w * 4 };
                        sws_scale(sws, frame->data, frame->linesize, 0, params->height, dest, dest_stride);
                        frames.push_back(std::move(vf));
                    }
                }
            }
            av_packet_unref(pkt);
        }

        av_frame_free(&frame); av_frame_free(&rgba_frame);
        avcodec_free_context(&ctx); avformat_close_input(&fmt);
        return frames;
    }
}