// stucanvas/utils/ivf_writer.hpp
#pragma once

#include <fstream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// 1. 包含您项目现存的外部 minimp4 头文件
#define MINIMP4_IMPLEMENTATION
#include "minimp4/minimp4.h"

namespace StuCanvas {

    // ========================================================================
    // 2. 内部自包含的 Annex-B 起始码定位解析算法 (彻底解决 find_nal_unit 符号未解析问题)
    // ========================================================================
    static inline const uint8_t *find_start_code(const uint8_t *h264_data, int h264_data_bytes, int *zcount) {
        const uint8_t *eof = h264_data + h264_data_bytes;
        const uint8_t *p = h264_data;
        do {
            int zero_cnt = 1;
            const uint8_t* found = (const uint8_t*)std::memchr(p, 0, eof - p);
            p = found ? found : eof;
            while (p + zero_cnt < eof && !p[zero_cnt]) zero_cnt++;
            if (zero_cnt >= 2 && p[zero_cnt] == 1) {
                *zcount = zero_cnt + 1;
                return p + zero_cnt + 1;
            }
            p += zero_cnt;
        } while (p < eof);
        *zcount = 0;
        return eof;
    }

    static inline const uint8_t *find_nal_unit_local(const uint8_t *h264_data, int h264_data_bytes, int *pnal_unit_bytes) {
        const uint8_t *eof = h264_data + h264_data_bytes;
        int zcount = 0;
        const uint8_t *start = find_start_code(h264_data, h264_data_bytes, &zcount);
        const uint8_t *stop = start;
        if (start < eof) {
            stop = find_start_code(start, (int)(eof - start), &zcount);
            while (stop > start && !stop[-1]) {
                stop--;
            }
        }
        *pnal_unit_bytes = (int)(stop - start - zcount);
        return start;
    }

    // ========================================================================
    // 3. 原生 IVFWriter 类 (保留以兼容画布底层的原生原始流写入)
    // ========================================================================
    class IVFWriter {
    public:

        struct IVFHeader {
            char     signature[4];
            uint16_t version;
            uint16_t headerSize;
            char     fourcc[4];
            uint16_t width;
            uint16_t height;
            uint32_t frNum;
            uint32_t frDen;
            uint32_t numFrames;
            uint32_t reserved;
        };


        static void WriteHeader(std::ofstream& file, uint32_t width, uint32_t height, uint32_t fps, uint32_t totalFrames) {
            IVFHeader h{};
            std::memcpy(h.signature, "DKIF", 4);
            h.version = 0;
            h.headerSize = 32;
            std::memcpy(h.fourcc, "AV01", 4);
            h.width = static_cast<uint16_t>(width);
            h.height = static_cast<uint16_t>(height);
            h.frNum = fps;
            h.frDen = 1;
            h.numFrames = totalFrames;
            h.reserved = 0;
            file.write(reinterpret_cast<const char*>(&h), sizeof(IVFHeader));
        }

        static void WriteFrame(std::ofstream& file, const void* data, size_t size, uint64_t timestamp) {
            if (!data || size == 0) return;
            uint32_t s = static_cast<uint32_t>(size);
            file.write(reinterpret_cast<const char*>(&s), 4);
            file.write(reinterpret_cast<const char*>(&timestamp), 8);
            file.write(reinterpret_cast<const char*>(data), size);
        }
    };

    // ========================================================================
    // 4. 跨平台一键 MP4 封装接口（H.264/H.265 一步到 MP4，不含任何 AV1 逻辑）
    // ========================================================================

    static inline int minimp4_file_write_callback(int64_t offset, const void* buffer, size_t size, void* token) {
        auto* out = static_cast<std::ofstream*>(token);
        out->seekp(offset);
        out->write(static_cast<const char*>(buffer), size);
        return out->fail() ? 1 : 0;
    }

    static inline bool ConvertH26xToMp4Internal(const std::string& inputPath, const std::string& outputPath, uint32_t width, uint32_t height, uint32_t fps, bool isHevc) {
        std::ifstream inFile(inputPath, std::ios::binary);
        if (!inFile.is_open()) return false;

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) return false;

        MP4E_mux_t* mux = MP4E_open(0, 0, &outFile, minimp4_file_write_callback);
        if (!mux) return false;

        mp4_h26x_writer_t writer{};
        mp4_h26x_write_init(&writer, mux, width, height, isHevc ? 1 : 0);

        inFile.seekg(0, std::ios::end);
        size_t size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        inFile.read(reinterpret_cast<char*>(buffer.data()), size);
        inFile.close();

        uint32_t frame_duration = 90000 / fps;
        const uint8_t* p = buffer.data();
        size_t bytes_left = size;
        int nal_size = 0;

        while (bytes_left > 0) {
            const uint8_t* nal = find_nal_unit_local(p, bytes_left, &nal_size);
            if (!nal || nal_size <= 0) break;

            size_t consumed = (nal - p) + nal_size;
            p += consumed;
            bytes_left -= consumed;

            std::vector<uint8_t> annexb_packet(4 + nal_size);
            annexb_packet[0] = 0; annexb_packet[1] = 0; annexb_packet[2] = 0; annexb_packet[3] = 1;
            std::memcpy(annexb_packet.data() + 4, nal, nal_size);

            mp4_h26x_write_nal(&writer, annexb_packet.data(), annexb_packet.size(), frame_duration);
        }

        mp4_h26x_write_close(&writer);
        MP4E_close(mux);
        outFile.close();
        return true;
    }

    /**
     * @brief 一键将 H.264 (AVC) 裸流包装为标准的 MP4 视频
     */
    static inline bool ConvertH264ToMp4(const std::string& inputPath, const std::string& outputPath, uint32_t width, uint32_t height, uint32_t fps) {
        return ConvertH26xToMp4Internal(inputPath, outputPath, width, height, fps, false);
    }

    /**
     * @brief 一键将 H.265 (HEVC) 裸流包装为标准的 MP4 视频
     */
    static inline bool ConvertH265ToMp4(const std::string& inputPath, const std::string& outputPath, uint32_t width, uint32_t height, uint32_t fps) {
        return ConvertH26xToMp4Internal(inputPath, outputPath, width, height, fps, true);
    }

} // namespace StuCanvas