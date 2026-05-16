// stucanvas/canvas/vulkan/video_encoder_av1.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vk_video/vulkan_video_codec_av1std_encode.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include "buffer.hpp"

namespace StuCanvas::Vulkan
{
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

    class VideoEncoderAV1
    {
    public:
        VideoEncoderAV1(VkInstance instance, VkDevice device,
                        VkPhysicalDevice physicalDevice,
                        uint32_t queueFamilyIndex,
                        const ExportSettings& settings)
            : device_(device), settings_(settings)
        {
            // 1. 手动加载函数指针
            loadVideoFunctions(instance, device);
            initProfileChain();
            std::cout << "[VideoEncoder] Initializing AV1 KHR Encoder (2026 Official Spec)..." << std::endl;

            // ---------------------------------------------------------
            // 2. 定义 AV1 Video Profile 链
            // ---------------------------------------------------------
            VkVideoEncodeAV1ProfileInfoKHR av1Profile{VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR};
            av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

            VkVideoProfileInfoKHR videoProfile{VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
            videoProfile.pNext = &av1Profile;
            videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
            videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

            // 为后续资源创建准备 Profile 列表
            VkVideoProfileListInfoKHR profileList{VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR};
            profileList.profileCount = 1;
            profileList.pProfiles = &videoProfile;

            // ---------------------------------------------------------
            // 3. 查询硬件能力 (必须挂载 AV1 Caps 以获取 stdHeaderVersion)
            // ---------------------------------------------------------
            VkVideoEncodeAV1CapabilitiesKHR av1EncodeCaps{VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR};
            VkVideoEncodeCapabilitiesKHR encodeCaps{VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR};
            encodeCaps.pNext = &av1EncodeCaps;

            VkVideoCapabilitiesKHR videoCaps{VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR};
            videoCaps.pNext = &encodeCaps;

            if (pfnVkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &videoProfile, &videoCaps) != VK_SUCCESS)
            {
                throw std::runtime_error("Vulkan: GPU does not support requested AV1 Encode capabilities.");
            }

            // ---------------------------------------------------------
            // 4. 创建 Video Session
            // ---------------------------------------------------------
            VkVideoSessionCreateInfoKHR sessionCreateInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
            sessionCreateInfo.pVideoProfile = &videoProfile;
            sessionCreateInfo.queueFamilyIndex = queueFamilyIndex;
            sessionCreateInfo.maxCodedExtent = {settings.width, settings.height};

            // 强制使用硬件兼容性最好的 NV12 (2-PLANE)
            sessionCreateInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
            sessionCreateInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;

            sessionCreateInfo.maxDpbSlots = 8;
            sessionCreateInfo.maxActiveReferencePictures = 1; // 简单线性参考模型
            sessionCreateInfo.pStdHeaderVersion = &videoCaps.stdHeaderVersion;

            if (pfnVkCreateVideoSessionKHR(device_, &sessionCreateInfo, nullptr, &videoSession_) != VK_SUCCESS)
            {
                throw std::runtime_error("Vulkan: Failed to create AV1 Video Session.");
            }

            // ---------------------------------------------------------
            // 5. 分配并绑定会话显存
            // ---------------------------------------------------------
            setupSessionMemory(physicalDevice);

            // ---------------------------------------------------------
            // 6. 创建 Session Parameters (SPS/Operating Points) - 解决崩溃核心
            // ---------------------------------------------------------
            createSessionParameters();

            // ---------------------------------------------------------
            // 7. 创建 Query Pool (Feedback)
            // ---------------------------------------------------------
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{
                VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR
            };
            feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

            VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            // 嵌套链: queryPoolInfo -> videoProfile -> feedbackInfo
            queryPoolInfo.pNext = &videoProfile;
            videoProfile.pNext = &feedbackInfo;

            queryPoolInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
            queryPoolInfo.queryCount = MAX_ENCODE_QUERIES;

            if (vkCreateQueryPool(device_, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS)
            {
                throw std::runtime_error("Vulkan: Failed to create Video Encode Query Pool.");
            }

            // ---------------------------------------------------------
            // 8. 分配 Bitstream Buffer (必须包含 ProfileList 以消除 Validation 警告)
            // ---------------------------------------------------------
            VkDeviceSize singleFrameSize = 32 * 1024 * 1024; // 8K 建议 32MB
            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.pNext = &profileChain_.profileList; // 关键：声明此 Buffer 用于该编码配置
            bufferInfo.size = singleFrameSize * MAX_ENCODE_QUERIES;
            bufferInfo.usage = VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            // 使用你已有的 Buffer 封装或原生创建
            // 注意：这里需要确保物理设备内存类型正确（HOST_VISIBLE | HOST_COHERENT）
            bitstreamBuffer_ = Buffer::Create(device_, physicalDevice, bufferInfo.size,
                                              VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            std::cout << "[VideoEncoder] Initialized successfully for 8K AV1 Export." << std::endl;
        }

        ~VideoEncoderAV1()
        {
            if (device_ == VK_NULL_HANDLE) return;

            // 1. 核心安全步骤：确保 GPU 已经完成所有编码任务
            // 在无头模式导出 8K 时，这一步至关重要，防止销毁仍在被硬件引用的位流缓冲或参考帧
            vkDeviceWaitIdle(device_);

            // 2. 销毁 Session Parameters (依赖于 Video Session)
            // 必须首先销毁参数对象，因为它持有对 Session 的引用
            if (videoSessionParameters_ != VK_NULL_HANDLE)
            {
                auto pfnDestroyParameters = (PFN_vkDestroyVideoSessionParametersKHR)
                    vkGetDeviceProcAddr(device_, "vkDestroyVideoSessionParametersKHR");
                if (pfnDestroyParameters) {
                    pfnDestroyParameters(device_, videoSessionParameters_, nullptr);
                }
                videoSessionParameters_ = VK_NULL_HANDLE;
            }

            // 3. 销毁 Video Session
            if (videoSession_ != VK_NULL_HANDLE)
            {
                pfnVkDestroyVideoSessionKHR(device_, videoSession_, nullptr);
                videoSession_ = VK_NULL_HANDLE;
            }

            // 4. 销毁反馈查询池 (Query Pool)
            if (queryPool_ != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device_, queryPool_, nullptr);
                queryPool_ = VK_NULL_HANDLE;
            }

            // 5. 释放会话绑定内存
            for (auto mem : sessionMemories_)
            {
                if (mem != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, mem, nullptr);
                }
            }
            sessionMemories_.clear();

            // 6. bitstreamBuffer_ 作为 RAII 包装对象（Buffer 类），
            // 将在此析构函数结束时自动触发其内部的 vkDestroyBuffer 和 vkFreeMemory。
        }

/**
 * @brief 录制单帧 AV1 编码指令 (2026 正式版严谨实现)
 * @param cmd 视频编码队列的 Command Buffer
 * @param srcView 带有 VIDEO_ENCODE_SRC 标志的 NV12 视图
 * @param frameIdx 全局帧索引
 */
void EncodeFrame(VkCommandBuffer cmd, VkImageView srcView, uint32_t frameIdx)
{
    const bool isKeyFrame = (frameIdx % settings_.gopSize == 0);
    const uint32_t querySlot = frameIdx % MAX_ENCODE_QUERIES;
    const VkDeviceSize bitstreamOffset = (VkDeviceSize)querySlot * (32 * 1024 * 1024);

    // ---------------------------------------------------------
    // 1. 状态机 Reset (解决 VUID-07012: Session Uninitialized)
    // ---------------------------------------------------------
    if (frameIdx == 0 || isKeyFrame)
    {
        VkVideoCodingControlInfoKHR controlInfo{ VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
        controlInfo.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
        pfnVkCmdControlVideoCodingKHR(cmd, &controlInfo);
    }

    // ---------------------------------------------------------
    // 2. 定义当前帧的物理资源 (用于输入和重建像素存储)
    // ---------------------------------------------------------
    VkVideoPictureResourceInfoKHR pictureResource{ VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
    pictureResource.imageViewBinding = srcView;
    pictureResource.codedExtent = { settings_.width, settings_.height };
    pictureResource.codedOffset = { 0, 0 };
    pictureResource.baseArrayLayer = 0;

    // ---------------------------------------------------------
    // 3. 配置 DPB 槽位元数据 (解决 VUID-10318)
    // ---------------------------------------------------------
    // 当前帧作为参考帧时的标准元数据
    StdVideoEncodeAV1ReferenceInfo stdRefInfo{};
    stdRefInfo.frame_type = isKeyFrame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;

    VkVideoEncodeAV1DpbSlotInfoKHR av1DpbSlotInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR };
    av1DpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR;
    av1DpbSlotInfo.pStdReferenceInfo = &stdRefInfo;

    // 定义 Setup Slot: 告诉硬件当前帧编码后的重建像素存入 Slot 0
    VkVideoReferenceSlotInfoKHR setupSlot{ VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
    setupSlot.pNext = &av1DpbSlotInfo;
    setupSlot.slotIndex = 0;
    setupSlot.pPictureResource = &pictureResource; // 修复：必须指向物理资源

    // 定义 Reference Slot: 如果是 P 帧，参考 Slot 0 中的前一帧像素
    VkVideoReferenceSlotInfoKHR referenceSlot{ VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
    referenceSlot.pNext = &av1DpbSlotInfo;
    referenceSlot.slotIndex = 0;
    referenceSlot.pPictureResource = &pictureResource;

    // ---------------------------------------------------------
    // 4. 配置 AV1 图像参数 (解决 VUID-10291)
    // ---------------------------------------------------------
    StdVideoEncodeAV1PictureInfo stdPicInfo{};
    stdPicInfo.frame_type = isKeyFrame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;
    stdPicInfo.order_hint = (uint8_t)(frameIdx % 128);
    // KeyFrame 刷新全部 8 个虚拟槽位，P 帧仅刷新 Slot 0
    stdPicInfo.refresh_frame_flags = isKeyFrame ? 0xFF : 0x01;

    VkVideoEncodeAV1PictureInfoKHR av1PicInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR };
    av1PicInfo.pStdPictureInfo = &stdPicInfo;
    av1PicInfo.predictionMode = isKeyFrame
                                    ? VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR
                                    : VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;

    // 核心修复：建立参考名到 Slot 的映射。由于 primary_ref_frame = 0，映射表首项必须合法
    for (uint32_t i = 0; i < 7; ++i) av1PicInfo.referenceNameSlotIndices[i] = -1;
    if (!isKeyFrame)
    {
        stdPicInfo.primary_ref_frame = 0;
        av1PicInfo.referenceNameSlotIndices[0] = 0; // 名 0 指向 Slot 0
    }

    // ---------------------------------------------------------
    // 5. 组装通用编码指令
    // ---------------------------------------------------------
    VkVideoEncodeInfoKHR encodeInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR };
    encodeInfo.pNext = &av1PicInfo;
    encodeInfo.srcPictureResource = pictureResource;
    encodeInfo.dstBuffer = bitstreamBuffer_.getBuffer();
    encodeInfo.dstBufferOffset = bitstreamOffset;
    encodeInfo.dstBufferRange = 32 * 1024 * 1024;

    // 每一帧都必须有 SetupSlot (重建输出)
    encodeInfo.pSetupReferenceSlot = &setupSlot;

    if (!isKeyFrame)
    {
        encodeInfo.referenceSlotCount = 1;
        encodeInfo.pReferenceSlots = &referenceSlot;
    }

    // ---------------------------------------------------------
    // 6. 录制到指令缓冲
    // ---------------------------------------------------------
    // 关键：Query 重置必须在编码作用域外
    vkCmdResetQueryPool(cmd, queryPool_, querySlot, 1);

    VkVideoBeginCodingInfoKHR beginInfo{ VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
    beginInfo.videoSession = videoSession_;
    beginInfo.videoSessionParameters = videoSessionParameters_;

    // BeginCoding 的参考列表必须包含编码过程中涉及的所有槽位 (Setup + Reference)
    beginInfo.referenceSlotCount = 1;
    beginInfo.pReferenceSlots = &setupSlot;

    pfnVkCmdBeginVideoCodingKHR(cmd, &beginInfo);

    vkCmdBeginQuery(cmd, queryPool_, querySlot, 0);

    pfnVkCmdEncodeVideoKHR(cmd, &encodeInfo);

    vkCmdEndQuery(cmd, queryPool_, querySlot);

    // 关键修复：正式版严禁传 nullptr
    VkVideoEndCodingInfoKHR endInfo{ VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
    pfnVkCmdEndVideoCodingKHR(cmd, &endInfo);
}

        uint64_t GetEncodedSize()
        {
            uint64_t bitstreamBytesWritten = 0;
            vkGetQueryPoolResults(device_, queryPool_, 0, 1, sizeof(uint64_t), &bitstreamBytesWritten, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            return bitstreamBytesWritten;
        }

        VkDeviceMemory GetBitstreamMemory() { return bitstreamBuffer_.getMemory(); }

    private:
        VkVideoSessionParametersKHR videoSessionParameters_ = VK_NULL_HANDLE;

        // 无头模式下的并发深度建议为 2 或 4
        // 因为 8K 帧的编码耗时较长，保持 2-4 个 Query 槽位可防止指令流阻塞
        static constexpr uint32_t MAX_ENCODE_QUERIES = 4;
        // AV1 标准结构体（必须持久化，防止指针失效）
        StdVideoAV1SequenceHeader stdSequenceHeader_ = {};
        StdVideoAV1TimingInfo stdTimingInfo_ = {};
        StdVideoAV1ColorConfig stdColorConfig_ = {};
        // --- 函数指针变量声明 ---
        PFN_vkCreateVideoSessionKHR pfnVkCreateVideoSessionKHR = nullptr;
        PFN_vkDestroyVideoSessionKHR pfnVkDestroyVideoSessionKHR = nullptr;
        PFN_vkGetVideoSessionMemoryRequirementsKHR pfnVkGetVideoSessionMemoryRequirementsKHR = nullptr;
        PFN_vkBindVideoSessionMemoryKHR pfnVkBindVideoSessionMemoryKHR = nullptr;
        PFN_vkCmdBeginVideoCodingKHR pfnVkCmdBeginVideoCodingKHR = nullptr;
        PFN_vkCmdEndVideoCodingKHR pfnVkCmdEndVideoCodingKHR = nullptr;
        PFN_vkCmdEncodeVideoKHR pfnVkCmdEncodeVideoKHR = nullptr;
        PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR pfnVkGetPhysicalDeviceVideoCapabilitiesKHR = nullptr;
        PFN_vkCmdControlVideoCodingKHR pfnVkCmdControlVideoCodingKHR = nullptr;
        // --- 【实现 loadVideoFunctions】 ---

        void loadVideoFunctions(VkInstance instance, VkDevice device)
        {
            pfnVkGetPhysicalDeviceVideoCapabilitiesKHR = (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceVideoCapabilitiesKHR");
            auto load = [&](const char* name) -> PFN_vkVoidFunction
            {
                auto addr = vkGetDeviceProcAddr(device, name);
                if (!addr)
                {
                    throw std::runtime_error("Vulkan: Could not load video function: " + std::string(name));
                }
                return addr;
            };
            pfnVkCreateVideoSessionKHR = (PFN_vkCreateVideoSessionKHR)load("vkCreateVideoSessionKHR");
            pfnVkDestroyVideoSessionKHR = (PFN_vkDestroyVideoSessionKHR)load("vkDestroyVideoSessionKHR");
            pfnVkGetVideoSessionMemoryRequirementsKHR = (PFN_vkGetVideoSessionMemoryRequirementsKHR)load(
                "vkGetVideoSessionMemoryRequirementsKHR");
            pfnVkBindVideoSessionMemoryKHR = (PFN_vkBindVideoSessionMemoryKHR)load("vkBindVideoSessionMemoryKHR");
            pfnVkCmdBeginVideoCodingKHR = (PFN_vkCmdBeginVideoCodingKHR)load("vkCmdBeginVideoCodingKHR");
            pfnVkCmdEndVideoCodingKHR = (PFN_vkCmdEndVideoCodingKHR)load("vkCmdEndVideoCodingKHR");
            pfnVkCmdEncodeVideoKHR = (PFN_vkCmdEncodeVideoKHR)load("vkCmdEncodeVideoKHR");
            pfnVkCmdControlVideoCodingKHR = (PFN_vkCmdControlVideoCodingKHR)load("vkCmdControlVideoCodingKHR");



        }
        struct ProfilePnextChain {
            VkVideoEncodeAV1ProfileInfoKHR av1Profile;
            VkVideoProfileInfoKHR videoProfile;
            VkVideoProfileListInfoKHR profileList;
        } profileChain_;
        void initProfileChain() {
            // 1. AV1 核心标志 (叶子)
            profileChain_.av1Profile = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR };
            profileChain_.av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

            // 2. 视频 Profile (中间) - 必须指向 AV1Info
            profileChain_.videoProfile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
            profileChain_.videoProfile.pNext = &profileChain_.av1Profile; // 指向叶子
            profileChain_.videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
            profileChain_.videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profileChain_.videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profileChain_.videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

            // 3. Profile List (根) - 用于 Buffer/Image 创建
            profileChain_.profileList = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
            profileChain_.profileList.profileCount = 1;
            profileChain_.profileList.pProfiles = &profileChain_.videoProfile;
        }
void createSessionParameters()
{
    // 1. 颜色配置：2026 正式版强制要求明确采样比例
    stdColorConfig_ = {};
    stdColorConfig_.BitDepth = 8;
    stdColorConfig_.subsampling_x = 1; // 4:2:0
    stdColorConfig_.subsampling_y = 1;
    stdColorConfig_.flags.color_description_present_flag = 1;
    stdColorConfig_.flags.color_range = 0;
    stdColorConfig_.color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_BT_709;
    stdColorConfig_.transfer_characteristics = STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_BT_709;
    stdColorConfig_.matrix_coefficients = STD_VIDEO_AV1_MATRIX_COEFFICIENTS_BT_709;

    // 2. 定时信息：强制要求 equal_picture_interval 与 time_scale 匹配
    stdTimingInfo_ = {};
    stdTimingInfo_.flags.equal_picture_interval = 1;
    stdTimingInfo_.num_units_in_display_tick = 1;
    stdTimingInfo_.time_scale = settings_.gopSize * 60; // 这里的 time_scale 必须非零

    // 3. 序列头：修复 8K 关键约束
    stdSequenceHeader_ = {};
    stdSequenceHeader_.seq_profile = STD_VIDEO_AV1_PROFILE_MAIN;
    stdSequenceHeader_.frame_width_bits_minus_1 = 15;
    stdSequenceHeader_.frame_height_bits_minus_1 = 15;
    stdSequenceHeader_.max_frame_width_minus_1 = (uint16_t)settings_.width - 1;
    stdSequenceHeader_.max_frame_height_minus_1 = (uint16_t)settings_.height - 1;

    // 强制设置 Order Hint，这是 -1000299000 报错的重灾区
    stdSequenceHeader_.order_hint_bits_minus_1 = 7; // 对应 AV1 规范最大值
    stdSequenceHeader_.flags.enable_order_hint = 1;
    stdSequenceHeader_.flags.frame_id_numbers_present_flag = 1;
    stdSequenceHeader_.delta_frame_id_length_minus_2 = 10;
    stdSequenceHeader_.additional_frame_id_length_minus_1 = 1;

    // 强制指定工具集状态为 "自动/选择"
    stdSequenceHeader_.seq_force_integer_mv = 2; // SELECT
    stdSequenceHeader_.seq_force_screen_content_tools = 2; // SELECT

    stdSequenceHeader_.pColorConfig = &stdColorConfig_;
    stdSequenceHeader_.pTimingInfo = &stdTimingInfo_;

    // 4. 操作点：2026 版必须包含有效的 operating_point_idc
    StdVideoEncodeAV1OperatingPointInfo opInfo = {};
    opInfo.seq_level_idx = STD_VIDEO_AV1_LEVEL_6_0; // 8K 强制 Level
    opInfo.seq_tier = 0;
    opInfo.operating_point_idc = 0; // 0 表示适用于所有层

    // 5. 组装参数
    VkVideoEncodeAV1SessionParametersCreateInfoKHR av1SpecificInfo{
        VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR
    };
    av1SpecificInfo.pStdSequenceHeader = &stdSequenceHeader_;
    av1SpecificInfo.stdOperatingPointCount = 1;
    av1SpecificInfo.pStdOperatingPoints = &opInfo;

    VkVideoSessionParametersCreateInfoKHR paramsCreateInfo{
        VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR
    };
    paramsCreateInfo.pNext = &av1SpecificInfo;
    paramsCreateInfo.videoSession = videoSession_;

    auto pfnCreateParameters = (PFN_vkCreateVideoSessionParametersKHR)
        vkGetDeviceProcAddr(device_, "vkCreateVideoSessionParametersKHR");

    VkResult result = pfnCreateParameters(device_, &paramsCreateInfo, nullptr, &videoSessionParameters_);
    if (result != VK_SUCCESS)
    {
        // 如果依然报错，请检查物理设备是否真的支持 8K (maxCodedExtent)
        throw std::runtime_error("Vulkan: Failed to create AV1 Session Parameters. Code: " + std::to_string(result));
    }
}

void setupSessionMemory(VkPhysicalDevice physDev)
{
    // 1. 获取会话所需的内存需求数量
    uint32_t memReqCount = 0;
    pfnVkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &memReqCount, nullptr);

    // 2. 准备需求数组
    // 注意：2026 正式版要求 sType 必须正确初始化
    std::vector<VkVideoSessionMemoryRequirementsKHR> memReqs(memReqCount, {
        VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR
    });
    pfnVkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &memReqCount, memReqs.data());

    // 3. 准备绑定信息数组和句柄容器
    std::vector<VkBindVideoSessionMemoryInfoKHR> binds(memReqCount);
    sessionMemories_.resize(memReqCount);

    for (uint32_t i = 0; i < memReqCount; ++i)
    {
        // 核心修复点：获取当前槽位的特定内存需求
        const VkMemoryRequirements& req = memReqs[i].memoryRequirements;

        VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize = req.size;

        // ---------------------------------------------------------
        // 关键逻辑：findMemoryType 的第二个参数必须传入 req.memoryTypeBits
        // 这解决了 Validation Error: "memoryTypeBits (0x8) ... are not compatible"
        // ---------------------------------------------------------
        allocInfo.memoryTypeIndex = findMemoryType(
            physDev,
            memReqs[i].memoryRequirements.memoryTypeBits, // 必须传入这个 bits 掩码
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &sessionMemories_[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Vulkan: Failed to allocate video session memory at index " + std::to_string(i));
        }

        // 4. 填充绑定信息
        binds[i].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
        binds[i].pNext = nullptr;
        binds[i].memoryBindIndex = memReqs[i].memoryBindIndex; // 必须匹配驱动返回的 bindIndex
        binds[i].memory = sessionMemories_[i];
        binds[i].memoryOffset = 0;
        binds[i].memorySize = req.size;
    }

    // 5. 执行原子绑定
    VkResult result = pfnVkBindVideoSessionMemoryKHR(device_, videoSession_, (uint32_t)binds.size(), binds.data());
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Vulkan: Failed to bind memory to Video Session. Result: " + std::to_string(result));
    }

    std::cout << "[VideoEncoder] Successfully allocated and bound " << memReqCount << " memory regions for AV1 Session." << std::endl;
}

        void createQueryPool() {
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{
                VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR
            };
            feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

            VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            // 关键：pNext 必须是 Profile -> Feedback 的链条
            queryPoolInfo.pNext = &profileChain_.videoProfile;
            feedbackInfo.pNext = queryPoolInfo.pNext;
            queryPoolInfo.pNext = &feedbackInfo;

            queryPoolInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
            queryPoolInfo.queryCount = MAX_ENCODE_QUERIES;

            if (vkCreateQueryPool(device_, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS) {
                throw std::runtime_error("Vulkan: Failed to create Video Encode Query Pool.");
            }
        }


        uint64_t getEncodedSize(uint32_t querySlot)
        {
            // 对应 2026 正式版，结果通常以 64 位无符号整数返回
            uint64_t bitstreamBytesWritten = 0;

            // querySlot = frameIdx % MAX_ENCODE_QUERIES
            VkResult result = vkGetQueryPoolResults(
                device_,
                queryPool_,
                querySlot,
                1,
                sizeof(uint64_t),
                &bitstreamBytesWritten,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );

            if (result != VK_SUCCESS)
            {
                // 如果返回 VK_NOT_READY 且未使用 WAIT_BIT，说明 GPU 还没算完
                return 0;
            }
            return bitstreamBytesWritten;
        }


        uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props)
        {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

            // 第一轮：严格寻找满足 typeFilter 且包含 props 的类型
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            {
                if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
                    return i;
            }

            // 第二轮：如果找不到 DEVICE_LOCAL，必须遵守 typeFilter 寻找任何可用类型
            // 因为驱动对 Video Session 的某些层（如 Context）可能强制要求非本地内存
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            {
                if (typeFilter & (1 << i)) return i;
            }

            throw std::runtime_error("Vulkan: No compatible memory type found for Video Session.");
        }

        VkDevice device_;
        VkVideoSessionKHR videoSession_ = VK_NULL_HANDLE;
        VkQueryPool queryPool_ = VK_NULL_HANDLE;
        std::vector<VkDeviceMemory> sessionMemories_;
        Buffer bitstreamBuffer_;
        ExportSettings settings_;
    };
} // namespace StuCanvas::Vulkan
