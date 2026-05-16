// stucanvas/utils/ivf_writer.hpp
#pragma once
#include <fstream>
#include <cstdint>

namespace StuCanvas {

    /**
     * @brief IVF (Indeo Video Format) 容器写入器
     * 专门用于封装 AV1 原始比特流，使其能被主流播放器识别。
     */
    class IVFWriter {
    public:
        static void WriteHeader(std::ofstream& file, uint32_t width, uint32_t height, uint32_t fps, uint32_t totalFrames) {
            struct Header {
                char     signature[4]; // 'DKIF'
                uint16_t version;
                uint16_t headerSize;
                char     fourcc[4];    // 'AV01'
                uint16_t width;
                uint16_t height;
                uint32_t frNum;
                uint32_t frDen;
                uint32_t numFrames;
                uint32_t reserved;
            } h;

            h.signature[0] = 'D'; h.signature[1] = 'K'; h.signature[2] = 'I'; h.signature[3] = 'F';
            h.version = 0;
            h.headerSize = 32;
            h.fourcc[0] = 'A'; h.fourcc[1] = 'V'; h.fourcc[2] = '0'; h.fourcc[3] = '1';
            h.width = (uint16_t)width;
            h.height = (uint16_t)height;
            h.frNum = fps;
            h.frDen = 1;
            h.numFrames = totalFrames;
            h.reserved = 0;

            file.write(reinterpret_cast<char*>(&h), sizeof(h));
        }

        static void WriteFrame(std::ofstream& file, const void* data, size_t size, uint64_t timestamp) {
            // IVF 帧头 (12字节)
            // 4 字节：帧大小
            // 8 字节：时间戳
            uint32_t s = (uint32_t)size;
            file.write(reinterpret_cast<const char*>(&s), 4);
            file.write(reinterpret_cast<const char*>(&timestamp), 8);
            file.write(reinterpret_cast<const char*>(data), size);
        }
    };

} // namespace StuCanvas