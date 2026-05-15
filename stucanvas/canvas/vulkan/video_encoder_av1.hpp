// stucanvas/canvas/vulkan/video_encoder_av1.hpp
#pragma once

#include <vulkan/vulkan.h>
// 显式包含 KHR 视频编码头文件
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vk_video/vulkan_video_codec_av1std_encode.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include "buffer.hpp"

namespace StuCanvas::Vulkan {
    enum class VideoCodec
    {
        AV1, // 推荐用于 8K，极致压缩
        HEVC, // 即 H.265，平衡兼容性与画质
        H264 // 即 AVC，最大兼容性（通常限制在 4K）
    };

    struct ExportSettings
    {
        // --- 基础配置 ---
        VideoCodec codec = VideoCodec::AV1;
        uint32_t width = 7680; // 默认 8K
        uint32_t height = 4320;
        std::string outputPath = "output.ivf"; // 原生编码通常输出 IVF 容器

        // --- 编码质量配置 ---
        uint32_t bitRateKbps = 50000; // 50 Mbps，对于 8K AV1 较合适
        uint32_t maxBitRateKbps = 80000;
        uint32_t gopSize = 60; // 关键帧间隔（通常设为与 FPS 一致）

        // 编码速度/质量平衡 (1-7): 1 最快, 7 质量最好 (对应 NVENC 预设)
        uint32_t tuningPreset = 7;
    };
/**
 * @brief 基于 KHR 标准的 AV1 硬件编码器封装
 * 适配 RTX 40/50 系列显卡，使用最新的 StdVideoAV1 结构
 */
class VideoEncoderAV1 {
public:
    VideoEncoderAV1(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    uint32_t queueFamilyIndex,
                    const ExportSettings& settings)
        : device_(device), settings_(settings)
    {
        std::cout << "[VideoEncoder] Initializing AV1 KHR Encoder..." << std::endl;

        // 1. 配置 AV1 Profile (使用你提供的 STD_VIDEO_AV1_PROFILE_MAIN)
        VkVideoEncodeAV1ProfileInfoKHR av1Profile{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR };
        av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

        VkVideoProfileInfoKHR videoProfile{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        videoProfile.pNext = &av1Profile;
        videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
        videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
        videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

        // 2. 创建 Video Session
        VkVideoSessionCreateInfoKHR sessionCreateInfo{ VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
        sessionCreateInfo.pVideoProfile = &videoProfile;
        sessionCreateInfo.queueFamilyIndex = queueFamilyIndex;
        sessionCreateInfo.maxCodedExtent = { settings.width, settings.height };

        // 渲染 8K 常用格式：G8_B8_R8_3PLANE_420 (YUV420)
        sessionCreateInfo.pictureFormat = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR;
        sessionCreateInfo.referencePictureFormat = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR;

        // 根据 KHR 规范更新的字段名
        sessionCreateInfo.maxDpbSlots = 8;
        sessionCreateInfo.maxActiveReferencePictures = 1;

        if (vkCreateVideoSessionKHR(device_, &sessionCreateInfo, nullptr, &videoSession_) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Failed to create AV1 Video Session. Ensure your GPU supports AV1 Encode KHR.");
        }

        // 3. 分配并绑定 Session 专用内存
        setupSessionMemory(physicalDevice);



        // 5. 准备输出 Bitstream Buffer (8K 建议 32MB)
        bitstreamBuffer_ = Buffer::Create(device_, physicalDevice, 32 * 1024 * 1024,
            VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);


        // 1. 定义我们想要哪些反馈数据
        VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackCreateInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR };
        // 我们只需要获取这一帧实际写入 Buffer 的字节数
        feedbackCreateInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

        // 2. 将反馈配置链接到 QueryPool 创建信息中
        VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        queryPoolInfo.pNext = &feedbackCreateInfo; // 关键：链接 pNext
        queryPoolInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
        queryPoolInfo.queryCount = 1;

        if (vkCreateQueryPool(device_, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Video Encode Feedback Query Pool");
        }
    }

    ~VideoEncoderAV1() {
        if (queryPool_ != VK_NULL_HANDLE) vkDestroyQueryPool(device_, queryPool_, nullptr);
        for (auto mem : sessionMemories_) vkFreeMemory(device_, mem, nullptr);
        if (videoSession_ != VK_NULL_HANDLE) vkDestroyVideoSessionKHR(device_, videoSession_, nullptr);
    }

    /**
         * @brief 录制一帧的编码指令
         */
    void EncodeFrame(VkCommandBuffer cmd, VkImageView srcView, uint32_t frameIdx) {
        bool isKeyFrame = (frameIdx % settings_.gopSize == 0);

        // 1. 准备 AV1 标准协议相关的图片信息 (这是最底层的参数)
        // 注意：这个结构体通常在 vulkan_video_codec_av1std_encode.h 中定义
        StdVideoEncodeAV1PictureInfo stdPicInfo{};
        // 使用你提供的头文件中的枚举值
        stdPicInfo.frame_type = isKeyFrame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;

        // 补充 AV1 必需的序号逻辑（否则播放器可能无法识别帧顺序）
        stdPicInfo.order_hint = (uint8_t)(frameIdx % 256);

        // 2. 准备 Vulkan 级别的编码图片信息
        VkVideoEncodeAV1PictureInfoKHR av1PicInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR };

        // --- 核心修复：将标准协议结构体链接到 Vulkan 结构体上 ---
        av1PicInfo.pStdPictureInfo = &stdPicInfo;

        // 设置预测模式：KeyFrame 使用 Intra，InterFrame 使用单参考或复合参考
        av1PicInfo.predictionMode = isKeyFrame ?
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR :
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;

        // 3. 配置通用的编码信息
        VkVideoEncodeInfoKHR encodeInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR };
        encodeInfo.pNext = &av1PicInfo; // 链接 AV1 特有参数
        encodeInfo.dstBuffer = bitstreamBuffer_.getBuffer();
        encodeInfo.dstBufferRange = 32 * 1024 * 1024;
        encodeInfo.srcPictureResource.imageViewBinding = srcView;
        encodeInfo.srcPictureResource.baseArrayLayer = 0;

        // 4. 执行编码录制
        VkVideoBeginCodingInfoKHR beginInfo{ VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
        beginInfo.videoSession = videoSession_;
        vkCmdBeginVideoCodingKHR(cmd, &beginInfo);

        vkCmdResetQueryPool(cmd, queryPool_, 0, 1);
        vkCmdBeginQuery(cmd, queryPool_, 0, 0);

        vkCmdEncodeVideoKHR(cmd, &encodeInfo);

        vkCmdEndQuery(cmd, queryPool_, 0);
        vkCmdEndVideoCodingKHR(cmd, nullptr);
    }

    /**
     * @brief 获取产生的压缩数据大小
     */
    /**
     * @brief 获取编码后的比特流数据大小 (直接读取查询池)
     */
    uint64_t GetEncodedSize() {
        // 因为我们在创建池时只设置了 BYTES_WRITTEN 标志，
        // 所以结果数组里只有 1 个 uint64_t
        uint64_t bitstreamBytesWritten = 0;

        // 从 QueryPool 中提取结果
        VkResult res = vkGetQueryPoolResults(
            device_,
            queryPool_,
            0,              // firstQuery
            1,              // queryCount
            sizeof(uint64_t),
            &bitstreamBytesWritten,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if (res != VK_SUCCESS) {
            std::cerr << "[VideoEncoder] Failed to get query results: " << res << std::endl;
            return 0;
        }

        return bitstreamBytesWritten;
    }

    VkDeviceMemory GetBitstreamMemory() { return bitstreamBuffer_.getMemory(); }

private:
    void setupSessionMemory(VkPhysicalDevice physDev) {
        uint32_t memReqCount = 0;
        vkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &memReqCount, nullptr);
        std::vector<VkVideoSessionMemoryRequirementsKHR> memReqs(memReqCount);
        for(auto& m : memReqs) m.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        vkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &memReqCount, memReqs.data());

        std::vector<VkBindVideoSessionMemoryInfoKHR> binds(memReqCount);
        sessionMemories_.resize(memReqCount);

        for (uint32_t i = 0; i < memReqCount; ++i) {
            VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocInfo.allocationSize = memReqs[i].memoryRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(physDev, memReqs[i].memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device_, &allocInfo, nullptr, &sessionMemories_[i]);

            binds[i].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
            binds[i].memoryBindIndex = memReqs[i].memoryBindIndex;
            binds[i].memory = sessionMemories_[i];
            binds[i].memorySize = memReqs[i].memoryRequirements.size;
        }
        vkBindVideoSessionMemoryKHR(device_, videoSession_, (uint32_t)binds.size(), binds.data());
    }

    uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
        }
        return 0;
    }

    VkDevice                    device_;
    VkVideoSessionKHR           videoSession_ = VK_NULL_HANDLE;
    VkQueryPool                 queryPool_ = VK_NULL_HANDLE;
    std::vector<VkDeviceMemory> sessionMemories_;
    Buffer                      bitstreamBuffer_;
    ExportSettings              settings_{};
};

} // namespace StuCanvas::Vulkan