// stucanvas/utils/ivf_writer.hpp
#pragma once
#include <fstream>
#include <cstdint>
#include <cstring>

namespace StuCanvas {

    class IVFWriter {
    public:
        // 使用 pack(1) 确保结构体在内存中紧凑排列，无任何 Padding
#pragma pack(push, 1)
        struct IVFHeader {
            char     signature[4]; // 'DKIF'
            uint16_t version;      // 0
            uint16_t headerSize;   // 32
            char     fourcc[4];    // 'AV01'
            uint16_t width;
            uint16_t height;
            uint32_t frNum;
            uint32_t frDen;
            uint32_t numFrames;
            uint32_t reserved;
        };
#pragma pack(pop)

        static void WriteHeader(std::ofstream& file, uint32_t width, uint32_t height, uint32_t fps, uint32_t totalFrames) {
            IVFHeader h{};

            std::memcpy(h.signature, "DKIF", 4);
            h.version = 0;
            h.headerSize = 32;
            std::memcpy(h.fourcc, "H265", 4); // AV1 的 FourCC

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

            // IVF 帧头严格 12 字节
            uint32_t s = static_cast<uint32_t>(size);

            // 1. 写入 4 字节帧大小 (小端)
            file.write(reinterpret_cast<const char*>(&s), 4);

            // 2. 写入 8 字节时间戳 (小端)
            // 注意：Vulkan 传回的 pts 通常是从 0 开始的帧序号
            file.write(reinterpret_cast<const char*>(&timestamp), 8);

            // 3. 写入原始比特流数据
            file.write(reinterpret_cast<const char*>(data), size);
        }
    };

} // namespace StuCanvas