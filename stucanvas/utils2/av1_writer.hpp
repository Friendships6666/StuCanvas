// stucanvas/canvas/vulkan/av1_writer.hpp
#pragma once

#include <fstream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace StuCanvas {

    // 确保 AV1 全局媒体标识宏存在
    #ifndef MP4_OBJECT_TYPE_AV1
    #define MP4_OBJECT_TYPE_AV1 0x31
    #endif

    // ========================================================================
    // 1. 自动解码并解析 AV1 流的底层比特流解析工具 (LEB128 & BitReader)
    // ========================================================================

    // 自动解码 AV1 的 LEB128 可变长度无符号整数
    static inline uint64_t ReadLeb128(const uint8_t* p, size_t size, size_t& bytes_read) {
        uint64_t value = 0;
        bytes_read = 0;
        for (size_t i = 0; i < size; ++i) {
            uint8_t byte = p[i];
            value |= static_cast<uint64_t>(byte & 0x7F) << (7 * i);
            bytes_read++;
            if (!(byte & 0x80)) break;
        }
        return value;
    }

    /**
     * @brief 极轻量级的 C++ 比特流读取器（BitReader），专门用来在运行期无损解包 OBU 参数
     */
    class BitReader {
    public:
        BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

        uint32_t ReadBits(int n) {
            uint32_t val = 0;
            for (int i = 0; i < n; ++i) {
                if (bit_offset_ >= size_ * 8) return 0;
                uint8_t byte = data_[bit_offset_ / 8];
                int bit = (byte >> (7 - (bit_offset_ % 8))) & 1;
                val = (val << 1) | bit;
                bit_offset_++;
            }
            return val;
        }

        void SkipBits(int n) {
            bit_offset_ += n;
        }

    private:
        const uint8_t* data_;
        size_t size_;
        size_t bit_offset_ = 0;
    };

    // 从首帧中精准提取 OBU_SEQUENCE_HEADER (序列参数集，类型为 1)
    static inline std::vector<uint8_t> ExtractAV1SequenceHeader(const uint8_t* frame_data, size_t frame_size) {
        const uint8_t* p = frame_data;
        size_t left = frame_size;
        while (left > 0) {
            uint8_t header = p[0];
            uint8_t obu_type = (header >> 3) & 0xF;
            bool has_extension = (header >> 2) & 1;
            bool has_size = (header >> 1) & 1;
            size_t offset = 1;
            if (has_extension) offset += 1;
            uint64_t obu_size = 0;
            if (has_size) {
                size_t leb_bytes = 0;
                obu_size = ReadLeb128(p + offset, left - offset, leb_bytes);
                offset += leb_bytes;
            } else {
                if (obu_type == 2) {
                    obu_size = 1;
                } else {
                    obu_size = left - offset;
                }
            }
            if (obu_type == 1) {
                // 如果该 OBU 没有使能 size 标记，强制重构使其包含 size (VLC 强制要求)
                if (!has_size) {
                    std::vector<uint8_t> seq_obu;
                    uint8_t new_header = (header & 0xFD) | 0x02; // 设置 bit 1 为 1 (obu_has_size_field)
                    seq_obu.push_back(new_header);
                    if (has_extension) {
                        seq_obu.push_back(p[1]);
                    }
                    uint64_t s = obu_size;
                    while (s > 0x7F) {
                        seq_obu.push_back((s & 0x7F) | 0x80);
                        s >>= 7;
                    }
                    seq_obu.push_back(s & 0x7F);
                    seq_obu.insert(seq_obu.end(), p + offset, p + offset + obu_size);
                    return seq_obu;
                } else {
                    return std::vector<uint8_t>(p, p + offset + obu_size);
                }
            }
            size_t consumed = offset + obu_size;
            if (consumed >= left) break;
            p += consumed;
            left -= consumed;
        }
        return {};
    }

    // 核心标准过滤器：在写入数据区(mdat)前，过滤剥离掉不应存在于帧数据区的 Temporal Delimiter (2)、Padding (15) 等
    static inline std::vector<uint8_t> FilterAV1Sample(const uint8_t* frame_data, size_t frame_size) {
        std::vector<uint8_t> filtered_data;
        filtered_data.reserve(frame_size);
        const uint8_t* p = frame_data;
        size_t left = frame_size;
        while (left > 0) {
            uint8_t header = p[0];
            uint8_t obu_type = (header >> 3) & 0xF;
            bool has_extension = (header >> 2) & 1;
            bool has_size = (header >> 1) & 1;
            size_t offset = 1;
            if (has_extension) offset += 1;
            uint64_t obu_size = 0;
            if (has_size) {
                size_t leb_bytes = 0;
                obu_size = ReadLeb128(p + offset, left - offset, leb_bytes);
                offset += leb_bytes;
            } else {
                if (obu_type == 2) {
                    obu_size = 1;
                } else {
                    obu_size = left - offset;
                }
            }

            // 核心规范对齐：仅过滤掉 Temporal Delimiter (2)、Padding (15)、Redundant Frame (7) 和 Tile List (4)
            // 必须保留 Sequence Header (1)，使其作为视频数据载荷的一部分随关键帧一并加载
            if (obu_type != 2 && obu_type != 15 && obu_type != 7 && obu_type != 4) {
                if (!has_size) {
                    std::vector<uint8_t> temp_obu;
                    uint8_t new_header = (header & 0xFD) | 0x02; // 设置 obu_has_size_field 为 1
                    temp_obu.push_back(new_header);
                    if (has_extension) {
                        temp_obu.push_back(p[1]);
                    }
                    uint64_t s = obu_size;
                    while (s > 0x7F) {
                        temp_obu.push_back((s & 0x7F) | 0x80);
                        s >>= 7;
                    }
                    temp_obu.push_back(s & 0x7F);
                    temp_obu.insert(temp_obu.end(), p + offset, p + offset + obu_size);
                    filtered_data.insert(filtered_data.end(), temp_obu.begin(), temp_obu.end());
                } else {
                    filtered_data.insert(filtered_data.end(), p, p + offset + obu_size);
                }
            }
            size_t consumed = offset + obu_size;
            if (consumed >= left) break;
            p += consumed;
            left -= consumed;
        }
        return filtered_data;
    }

    // ========================================================================
    // 2. 专用于 AV1 零冲突、高保真封装的自主 C++ Muxer (Av1Mp4Writer)
    // ========================================================================
    class Av1Mp4Writer {
    public:
        struct FrameIndex {
            uint64_t offset;
            uint32_t size;
            uint32_t duration;
            bool is_keyframe;
        };

        Av1Mp4Writer(const std::string& path, uint32_t width, uint32_t height, uint32_t fps)
            : path_(path), width_(width), height_(height), fps_(fps) {}

        bool Open() {
            file_.open(path_, std::ios::binary | std::ios::trunc);
            if (!file_.is_open()) return false;

            BeginBox("ftyp");
            WriteFourCC("mp42");
            WriteUint32(0);
            WriteFourCC("mp42");
            WriteFourCC("isom");
            EndBox();

            mdat_offset_ = file_.tellp();
            WriteUint32(0);
            WriteFourCC("mdat");
            return true;
        }

        void WriteFrame(const void* data, size_t size, bool is_keyframe) {
            FrameIndex idx{};
            idx.offset = file_.tellp();
            idx.size = static_cast<uint32_t>(size);
            idx.duration = 90000 / fps_;
            idx.is_keyframe = is_keyframe;
            frames_.push_back(idx);
            file_.write(reinterpret_cast<const char*>(data), size);
        }

        void Close(const std::vector<uint8_t>& extra_data) {
            if (!file_.is_open()) return;
            uint64_t mdat_end = file_.tellp();
            uint64_t mdat_size = mdat_end - mdat_offset_;

            file_.seekp(mdat_offset_);
            WriteUint32(static_cast<uint32_t>(mdat_size));

            file_.seekp(mdat_end);
            WriteMoov(extra_data);
            file_.close();
        }

    private:
        struct BoxStack {
            std::string type;
            uint64_t offset;
        };

        std::string path_;
        uint32_t width_, height_, fps_;
        std::ofstream file_;
        uint64_t mdat_offset_ = 0;
        std::vector<FrameIndex> frames_;
        std::vector<BoxStack> box_stack_;

        void BeginBox(const char* type) {
            BoxStack b{ type, static_cast<uint64_t>(file_.tellp()) };
            box_stack_.push_back(b);
            WriteUint32(0);
            WriteFourCC(type);
        }

        void EndBox() {
            if (box_stack_.empty()) return;
            BoxStack b = box_stack_.back();
            box_stack_.pop_back();
            uint64_t curr = file_.tellp();
            uint32_t size = static_cast<uint32_t>(curr - b.offset);
            file_.seekp(b.offset);
            WriteUint32(size);
            file_.seekp(curr);
        }

        void WriteUint8(uint8_t v) { file_.write(reinterpret_cast<const char*>(&v), 1); }
        void WriteUint16(uint16_t v) { uint16_t be = (v >> 8) | (v << 8); file_.write(reinterpret_cast<const char*>(&be), 2); }
        void WriteUint24(uint32_t v) { uint8_t be[3] = { static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v) }; file_.write(reinterpret_cast<const char*>(be), 3); }
        void WriteUint32(uint32_t v) { uint32_t be = ((v >> 24) & 0xff) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | ((v << 24) & 0xff000000); file_.write(reinterpret_cast<const char*>(&be), 4); }
        void WriteFourCC(const char* fcc) { file_.write(fcc, 4); }
        void WriteString(const std::string& str) { file_.write(str.c_str(), str.size() + 1); }
        void WriteBytes(const std::vector<uint8_t>& bytes) { if (!bytes.empty()) file_.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }

        void WriteMoov(const std::vector<uint8_t>& extra_data) {
            uint32_t total_duration = 0;
            for (const auto& f : frames_) total_duration += f.duration;

            BeginBox("moov");
                BeginBox("mvhd");
                    WriteUint8(0); WriteUint24(0);
                    WriteUint32(0); WriteUint32(0);
                    WriteUint32(90000);
                    WriteUint32(total_duration);
                    WriteUint32(0x00010000); WriteUint16(0x0100); WriteUint16(0); WriteUint32(0); WriteUint32(0);
                    WriteUint32(0x00010000); WriteUint32(0); WriteUint32(0);
                    WriteUint32(0); WriteUint32(0x00010000); WriteUint32(0);
                    WriteUint32(0); WriteUint32(0); WriteUint32(0x40000000);
                    for (int i = 0; i < 6; ++i) WriteUint32(0);
                    WriteUint32(2);
                EndBox();

                BeginBox("trak");
                    BeginBox("tkhd");
                        WriteUint8(0); WriteUint24(7);
                        WriteUint32(0); WriteUint32(0);
                        WriteUint32(1);
                        WriteUint32(0); WriteUint32(total_duration);
                        WriteUint32(0); WriteUint32(0);
                        WriteUint16(0); WriteUint16(0); WriteUint16(0); WriteUint16(0);
                        WriteUint32(0x00010000); WriteUint32(0); WriteUint32(0);
                        WriteUint32(0); WriteUint32(0x00010000); WriteUint32(0);
                        WriteUint32(0); WriteUint32(0); WriteUint32(0x40000000);
                        WriteUint32(width_ << 16); WriteUint32(height_ << 16);
                    EndBox();

                    BeginBox("mdia");
                        BeginBox("mdhd");
                            WriteUint8(0); WriteUint24(0); WriteUint32(0); WriteUint32(0);
                            WriteUint32(90000); WriteUint32(total_duration);
                            WriteUint16(0x15c7); WriteUint16(0); // "und" language
                        EndBox();

                        BeginBox("hdlr");
                            WriteUint8(0); WriteUint24(0); WriteUint32(0); WriteFourCC("vide");
                            for (int i = 0; i < 3; ++i) WriteUint32(0);
                            WriteString("VideoHandler");
                        EndBox();

                        BeginBox("minf");
                            BeginBox("vmhd");
                                WriteUint8(0); WriteUint24(1); WriteUint16(0);
                                for (int i = 0; i < 3; ++i) WriteUint16(0);
                            EndBox();

                            BeginBox("dinf");
                                BeginBox("dref");
                                    WriteUint8(0); WriteUint24(0); WriteUint32(1);
                                    BeginBox("url "); WriteUint8(0); WriteUint24(1); EndBox();
                                EndBox();
                            EndBox();

                            BeginBox("stbl");
                                BeginBox("stsd");
                                    WriteUint8(0); WriteUint24(0); WriteUint32(1);
                                    BeginBox("av01");
                                        for (int i = 0; i < 6; ++i) WriteUint8(0);
                                        WriteUint16(1); WriteUint16(0); WriteUint16(0);
                                        for (int i = 0; i < 3; ++i) WriteUint32(0);
                                        WriteUint16(width_); WriteUint16(height_);
                                        WriteUint32(0x00480000); WriteUint32(0x00480000);
                                        WriteUint32(0); WriteUint16(1);

                                        // 规范 compressorname
                                        WriteUint8(10);
                                        WriteBytes(std::vector<uint8_t>{'A', 'O', 'M', ' ', 'C', 'o', 'd', 'i', 'n', 'g'});
                                        for (int k = 0; k < 21; ++k) WriteUint8(0);

                                        WriteUint16(24); WriteUint16(-1);

                                        // 写入标准的 AV1 格式配置信息 (av1C)
                                        BeginBox("av1C");
                                        WriteUint8(0x81); // marker=1, version=1

                                        // 动态从序列头字节流中解码出真实的 Profile 与 Level 数据
                                        uint8_t seq_profile = 0;
                                        uint8_t seq_level_idx_0 = 12; // 默认 Level 5.0
                                        uint8_t seq_tier_0 = 0;
                                        uint8_t high_bitdepth = 0;
                                        uint8_t twelve_bit = 0;
                                        uint8_t mono_chrome = 0;
                                        uint8_t chroma_subsampling_x = 1; // 默认 4:2:0 采样
                                        uint8_t chroma_subsampling_y = 1;
                                        uint8_t chroma_sample_position = 0;

                                        if (!extra_data.empty()) {
                                            uint8_t header = extra_data[0];
                                            bool has_ext = (header >> 2) & 1;
                                            bool has_sz = (header >> 1) & 1;
                                            size_t offset = 1;
                                            if (has_ext) offset += 1;
                                            if (has_sz) {
                                                size_t leb_bytes = 0;
                                                ReadLeb128(extra_data.data() + offset, extra_data.size() - offset, leb_bytes);
                                                offset += leb_bytes;
                                            }
                                            if (offset < extra_data.size()) {
                                                BitReader br(extra_data.data() + offset, extra_data.size() - offset);
                                                seq_profile = br.ReadBits(3);
                                                br.SkipBits(1); // still_picture
                                                uint8_t reduced_still_picture_header = br.ReadBits(1);
                                                if (reduced_still_picture_header) {
                                                    seq_level_idx_0 = br.ReadBits(5);
                                                } else {
                                                    uint8_t timing_info_present_flag = br.ReadBits(1);
                                                    if (timing_info_present_flag) {
                                                        br.SkipBits(32); br.SkipBits(32);
                                                        uint8_t equal_picture_interval = br.ReadBits(1);
                                                        if (equal_picture_interval) {
                                                            while (br.ReadBits(1) == 0) { br.SkipBits(1); }
                                                        }
                                                        uint8_t decoder_model_info_present = br.ReadBits(1);
                                                        if (decoder_model_info_present) {
                                                            br.SkipBits(5); br.SkipBits(32); br.SkipBits(5); br.SkipBits(5);
                                                        }
                                                    }
                                                    uint8_t initial_display_delay_present = br.ReadBits(1);
                                                    if (initial_display_delay_present) {
                                                        br.SkipBits(4);
                                                    }
                                                    uint8_t operating_points_cnt_minus_1 = br.ReadBits(5);
                                                    br.SkipBits(12); // operating_point_idc[0]
                                                    seq_level_idx_0 = br.ReadBits(5);
                                                    seq_tier_0 = br.ReadBits(1);
                                                }
                                                high_bitdepth = br.ReadBits(1);
                                                if (seq_profile == 2 && high_bitdepth == 1) {
                                                    twelve_bit = br.ReadBits(1);
                                                }
                                                mono_chrome = br.ReadBits(1);
                                                uint8_t color_desc_present = br.ReadBits(1);
                                                if (color_desc_present) {
                                                    br.SkipBits(8); br.SkipBits(8); br.SkipBits(8);
                                                }
                                                if (mono_chrome) {
                                                    chroma_subsampling_x = 1; chroma_subsampling_y = 1;
                                                } else {
                                                    br.SkipBits(1); // color_range
                                                    chroma_subsampling_x = br.ReadBits(1);
                                                    chroma_subsampling_y = br.ReadBits(1);
                                                }
                                            }
                                        }

                                        uint8_t byte1 = (seq_profile << 5) | (seq_level_idx_0 & 0x1F);
                                        WriteUint8(byte1);

                                        // 完美写入 4:2:0 采样的 0x0c 属性，确保参数集相互匹配，支持常规硬解
                                        uint8_t byte2 = (seq_tier_0 << 7) | (high_bitdepth << 6) | (twelve_bit << 5) | (mono_chrome << 4) | (chroma_subsampling_x << 3) | (chroma_subsampling_y << 2) | (chroma_sample_position & 3);
                                        WriteUint8(byte2);

                                        WriteUint8(0x00); // reserved
                                        WriteBytes(extra_data);
                                        EndBox();
                                    EndBox();
                                EndBox();

                                // Time to Sample Box
                                BeginBox("stts");
                                    WriteUint8(0); WriteUint24(0); WriteUint32(1);
                                    WriteUint32(static_cast<uint32_t>(frames_.size()));
                                    WriteUint32(90000 / fps_);
                                EndBox();

                                // Sample Size Box
                                BeginBox("stsz");
                                    WriteUint8(0); WriteUint24(0); WriteUint32(0);
                                    WriteUint32(static_cast<uint32_t>(frames_.size()));
                                    for (const auto& f : frames_) WriteUint32(f.size);
                                EndBox();

                                // Sample to Chunk Box
                                BeginBox("stsc");
                                    WriteUint8(0); WriteUint24(0); WriteUint32(1);
                                    WriteUint32(1); WriteUint32(1); WriteUint32(1);
                                EndBox();

                                // Chunk Offset Box
                                BeginBox("stco");
                                    WriteUint8(0); WriteUint24(0);
                                    WriteUint32(static_cast<uint32_t>(frames_.size()));
                                    for (const auto& f : frames_) WriteUint32(static_cast<uint32_t>(f.offset));
                                EndBox();

                                // Sync Sample Box (stss)
                                std::vector<uint32_t> keyframes;
                                for (size_t i = 0; i < frames_.size(); ++i) {
                                    if (frames_[i].is_keyframe) keyframes.push_back(static_cast<uint32_t>(i + 1));
                                }
                                if (keyframes.size() < frames_.size()) {
                                    BeginBox("stss");
                                        WriteUint8(0); WriteUint24(0);
                                        WriteUint32(static_cast<uint32_t>(keyframes.size()));
                                        for (auto k : keyframes) WriteUint32(k);
                                    EndBox();
                                }
                            EndBox();
                        EndBox();
                    EndBox();
                EndBox();
            EndBox();
        }
    };

    // ========================================================================
    // 3. 跨平台一键 AV1 MP4 打包/封装静态接口（完全自包含，100% 播放兼容）
    // ========================================================================

    /**
     * @brief 一键将原始的 .ivf 格式 AV1 视频无损打包并封装为标准可播放的 MP4 格式视频
     */
    static inline bool ConvertAv1ToMp4(const std::string& inputPath, const std::string& outputPath, uint32_t width, uint32_t height, uint32_t fps) {
        std::ifstream inFile(inputPath, std::ios::binary);
        if (!inFile.is_open()) return false;

        // 1. 验证并读取 IVF 头部属性
        char signature[4];
        inFile.read(signature, 4);
        if (std::memcmp(signature, "DKIF", 4) != 0) return false;

        inFile.seekg(12, std::ios::beg);
        uint16_t ivf_width = 0, ivf_height = 0;
        inFile.read(reinterpret_cast<char*>(&ivf_width), 2);
        inFile.read(reinterpret_cast<char*>(&ivf_height), 2);

        if (width == 0) width = ivf_width;
        if (height == 0) height = ivf_height;

        // 2. 提取首帧中必须放置于 av1C 的序列参数集 (Sequence Header)
        inFile.seekg(32, std::ios::beg);
        uint32_t first_frame_size = 0;
        uint64_t first_timestamp = 0;
        inFile.read(reinterpret_cast<char*>(&first_frame_size), 4);
        inFile.read(reinterpret_cast<char*>(&first_timestamp), 8);

        std::vector<uint8_t> first_frame_data(first_frame_size);
        inFile.read(reinterpret_cast<char*>(first_frame_data.data()), first_frame_size);

        std::vector<uint8_t> seq_obu = ExtractAV1SequenceHeader(first_frame_data.data(), first_frame_size);

        // 3. 使用自建 of Av1Mp4Writer 开启写入流 (杜绝了与外部旧版 minimp4.h 无法写入 av1C 的硬件级冲突)
        Av1Mp4Writer writer(outputPath, width, height, fps);
        if (!writer.Open()) return false;

        // 恢复文件指针至首帧头部，开启循环帧提取
        inFile.seekg(32, std::ios::beg);
        uint32_t frame_idx = 0;

        while (true) {
            uint32_t frame_size = 0;
            uint64_t timestamp = 0;

            inFile.read(reinterpret_cast<char*>(&frame_size), 4);
            if (inFile.gcount() != 4) break; // 读取完毕

            inFile.read(reinterpret_cast<char*>(&timestamp), 8);

            std::vector<uint8_t> frame_data(frame_size);
            inFile.read(reinterpret_cast<char*>(frame_data.data()), frame_size);

            bool is_key = false;
            if (frame_idx == 0) {
                is_key = true;
            } else {
                uint8_t header = frame_data[0];
                uint8_t obu_type = (header >> 3) & 0xF;
                if (obu_type == 6 || obu_type == 1) {
                    is_key = true;
                }
            }

            // 核心安全过滤：将帧数据中不符合规范的 Temporal Delimiter (2) 等进行剥离，保留 Sequence Header
            std::vector<uint8_t> filtered_data = FilterAV1Sample(frame_data.data(), frame_size);
            if (!filtered_data.empty()) {
                // 核心修复：AV1 在 MP4 中没有 4 字节的 NAL 长度前缀！直接写入纯净的 OBU 序列数据！
                writer.WriteFrame(filtered_data.data(), filtered_data.size(), is_key);
            }
            frame_idx++;
        }

        writer.Close(seq_obu);
        inFile.close();
        return true;
    }

} // namespace StuCanvas