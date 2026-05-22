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
        uint32_t width = 2560; // 默认 8K
        uint32_t height = 1600;
        std::string outputPath = "output.ivf"; // 原生编码通常输出 IVF 容器

        // --- 编码质量配置 ---
        uint32_t bitRateKbps = 50000; // 50 Mbps，对于 8K AV1 较合适
        uint32_t maxBitRateKbps = 80000;
        uint32_t gopSize = 1; // 关键帧间隔（通常设为与 FPS 一致）

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
    loadVideoFunctions(instance, device);
    physicalDevice_ = physicalDevice;

    // 第一步：初始化唯一的类成员 Profile 链
    initProfileChain();

    // 第二步：查询能力（必须使用类成员作为查询参数）
    VkVideoEncodeAV1CapabilitiesKHR av1EncodeCaps{VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR};
    VkVideoEncodeCapabilitiesKHR encodeCaps{VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR};
    encodeCaps.pNext = &av1EncodeCaps;
    VkVideoCapabilitiesKHR videoCaps{VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR};
    videoCaps.pNext = &encodeCaps;

    // 使用 profileChain_ 里的 videoProfile
    pfnVkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &profileChain_.videoProfile, &videoCaps);

    // 第三步：创建 Session (务必使用 profileChain_.videoProfile)
    VkVideoSessionCreateInfoKHR sessionCreateInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    sessionCreateInfo.pVideoProfile = &profileChain_.videoProfile; // 关键：使用类成员
    sessionCreateInfo.queueFamilyIndex = queueFamilyIndex;
    sessionCreateInfo.maxCodedExtent = {settings.width, settings.height};
    sessionCreateInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    sessionCreateInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
    sessionCreateInfo.maxDpbSlots = 8;
    sessionCreateInfo.maxActiveReferencePictures = 1;
    sessionCreateInfo.pStdHeaderVersion = &videoCaps.stdHeaderVersion;

    if (pfnVkCreateVideoSessionKHR(device_, &sessionCreateInfo, nullptr, &videoSession_) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan: Failed to create AV1 Video Session.");
    }

    setupSessionMemory(physicalDevice);
    createSessionParameters();


    profileChain_.feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

    // 第五步：创建 QueryPool
    createQueryPool();

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
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&profileChain_.profileList);

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
                if (pfnDestroyParameters)
                {
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
                if (mem != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device_, mem, nullptr);
                }
            }
            sessionMemories_.clear();

            // 6. bitstreamBuffer_ 作为 RAII 包装对象（Buffer 类），
            // 将在此析构函数结束时自动触发其内部的 vkDestroyBuffer 和 vkFreeMemory。
        }

        // stucanvas/canvas/vulkan/video_encoder_av1.hpp 内部增加

        /**
         * @brief 提取 AV1 序列参数集 (OBU)
         * 用于写入 IVF 文件头，解决播放器报 "No sequence header available" 的问题
         */
        std::vector<uint8_t> getCodecHeader() {
            if (!pfnVkGetEncodedVideoSessionParametersKHR || videoSessionParameters_ == VK_NULL_HANDLE) {
                return {};
            }

            VkVideoEncodeSessionParametersGetInfoKHR getInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR };
            getInfo.videoSessionParameters = videoSessionParameters_;

            // 在 2026 正式版中，Feedback 结构通常用于获取参数更新状态，这里必须初始化
            VkVideoEncodeSessionParametersFeedbackInfoKHR feedback{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR };

            size_t dataSize = 0;
            // 第一次调用：查询所需的 Buffer 长度
            VkResult res = pfnVkGetEncodedVideoSessionParametersKHR(device_, &getInfo, &feedback, &dataSize, nullptr);

            if (res != VK_SUCCESS || dataSize == 0) return {};

            std::vector<uint8_t> headerData(dataSize);
            // 第二次调用：填充真正的 Sequence Header OBU 数据
            res = pfnVkGetEncodedVideoSessionParametersKHR(device_, &getInfo, &feedback, &dataSize, headerData.data());

            if (res != VK_SUCCESS) return {};

            std::cout << "[VideoEncoder] Successfully extracted AV1 Sequence Header (" << dataSize << " bytes)." << std::endl;
            return headerData;
        }


        void EncodeFrame(VkCommandBuffer cmd, VkImageView currentSrcView, VkImageView previousSrcView, uint32_t frameIdx)
        {
            // 1. 基础参数计算
            const bool isKeyFrame = (frameIdx % settings_.gopSize == 0);
            const uint32_t querySlot = frameIdx % MAX_ENCODE_QUERIES;

            // DPB 槽位分配逻辑 (Ping-Pong 双缓冲)：
            // 当前帧编码结果写到槽位 currIdx，如果是 P 帧，参考来自槽位 prevIdx
            const uint32_t currIdx = frameIdx % 2;
            const uint32_t prevIdx = (frameIdx + 1) % 2;

            // ---------------------------------------------------------
            // 2. 物理资源描述
            // ---------------------------------------------------------
            // 当前帧资源 (写入目标)
            VkVideoPictureResourceInfoKHR currResource = { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
            currResource.imageViewBinding = currentSrcView;
            currResource.codedExtent = { settings_.width, settings_.height };
            currResource.baseArrayLayer = 0;

            // 参考帧资源 (读取源)
            VkVideoPictureResourceInfoKHR prevResource = { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
            prevResource.imageViewBinding = previousSrcView;
            prevResource.codedExtent = { settings_.width, settings_.height };
            prevResource.baseArrayLayer = 0;

            // ---------------------------------------------------------
            // 3. 配置 DPB 槽位绑定
            // ---------------------------------------------------------
            StdVideoEncodeAV1ReferenceInfo stdRefCurr{};
            stdRefCurr.frame_type = isKeyFrame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;

            VkVideoEncodeAV1DpbSlotInfoKHR av1DpbSetup = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR };
            av1DpbSetup.pStdReferenceInfo = &stdRefCurr;

            // Setup Slot: 告诉 EncodeInfo 把编码后的重建数据写到哪个槽位
            VkVideoReferenceSlotInfoKHR setupSlot = { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
            setupSlot.pNext = &av1DpbSetup;
            setupSlot.slotIndex = (int32_t)currIdx;
            setupSlot.pPictureResource = &currResource;

            // Bind Setup Slot: 核心修复！在 BeginCoding 时，目标槽位还未写入数据，
            // 属于“未激活(Inactive)”状态，必须填 -1 告诉驱动仅作资源绑定。
            VkVideoReferenceSlotInfoKHR bindSetupSlot = setupSlot;
            bindSetupSlot.slotIndex = -1;

            // Reference Slot: P 帧用来读取前一帧数据的槽位 (已激活)
            VkVideoReferenceSlotInfoKHR refSlot = { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
            refSlot.pNext = &av1DpbSetup; // 复用结构体
            refSlot.slotIndex = (int32_t)prevIdx;
            refSlot.pPictureResource = &prevResource;

            // ---------------------------------------------------------
            // 4. 配置 AV1 图像语法参数
            // ---------------------------------------------------------
            StdVideoEncodeAV1PictureInfo stdPicInfo{};
            stdPicInfo.frame_type = stdRefCurr.frame_type;
            stdPicInfo.order_hint = (uint8_t)(frameIdx % 128);

            // 关键修复 1：I 帧刷新整个 DPB (0xFF)，P 帧仅更新它刚写出的那个槽位
            stdPicInfo.refresh_frame_flags = isKeyFrame ? 0xFF : (1 << currIdx);

            // 关键修复 2：I 帧必须显式声明无参考帧 (7 对应 STD_VIDEO_AV1_PRIMARY_REF_NONE)
            stdPicInfo.primary_ref_frame = isKeyFrame ? STD_VIDEO_AV1_PRIMARY_REF_NONE : 0;

            VkVideoEncodeAV1PictureInfoKHR av1PicInfo = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR };
            av1PicInfo.pStdPictureInfo = &stdPicInfo;
            av1PicInfo.predictionMode = isKeyFrame
                                            ? VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR
                                            : VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;

            // 建立 AV1 标准参考名映射 (LAST_FRAME -> prevIdx)
            for (uint32_t i = 0; i < 7; ++i) av1PicInfo.referenceNameSlotIndices[i] = -1;
            if (!isKeyFrame)
            {
                av1PicInfo.referenceNameSlotIndices[0] = (int32_t)prevIdx;
            }

            // ---------------------------------------------------------
            // 5. 组装通用编码指令信息
            // ---------------------------------------------------------
            VkVideoEncodeInfoKHR encodeInfo = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR };
            encodeInfo.pNext = &av1PicInfo;
            encodeInfo.srcPictureResource = currResource;
            encodeInfo.dstBuffer = bitstreamBuffer_.getBuffer();
            encodeInfo.dstBufferOffset = (VkDeviceSize)querySlot * (32 * 1024 * 1024);
            encodeInfo.dstBufferRange = 32 * 1024 * 1024;

            // 真正 Encode 时使用实际的槽位索引 (currIdx)
            encodeInfo.pSetupReferenceSlot = &setupSlot;

            if (!isKeyFrame)
            {
                encodeInfo.referenceSlotCount = 1;
                encodeInfo.pReferenceSlots = &refSlot;
            }

            // ---------------------------------------------------------
            // 6. 指令录制阶段
            // ---------------------------------------------------------

            // A. Query 重置 (必须在 Coding Block 之外)
            vkCmdResetQueryPool(cmd, queryPool_, querySlot, 1);

            // B. 开始视频编码作用域
            // BeginCoding 必须声明涉及的所有物理资源
            VkVideoReferenceSlotInfoKHR activeSlots[2];
            uint32_t activeCount = 0;
            activeSlots[activeCount++] = bindSetupSlot; // 绑定当前图，slotIndex = -1
            if (!isKeyFrame) {
                activeSlots[activeCount++] = refSlot;   // 绑定参考图，slotIndex = prevIdx
            }

            VkVideoBeginCodingInfoKHR beginInfo = { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
            beginInfo.videoSession = videoSession_;
            beginInfo.videoSessionParameters = videoSessionParameters_;
            beginInfo.referenceSlotCount = activeCount;
            beginInfo.pReferenceSlots = activeSlots;

            pfnVkCmdBeginVideoCodingKHR(cmd, &beginInfo);

            // C. 状态机初始化 (解决 VUID-07012)
            if (frameIdx == 0 || isKeyFrame)
            {
                VkVideoCodingControlInfoKHR ctrl = { VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
                ctrl.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
                pfnVkCmdControlVideoCodingKHR(cmd, &ctrl);
            }

            // D. 发射编码任务
            vkCmdBeginQuery(cmd, queryPool_, querySlot, 0);
            pfnVkCmdEncodeVideoKHR(cmd, &encodeInfo);
            vkCmdEndQuery(cmd, queryPool_, querySlot);

            // E. 结束视频编码作用域 (修复 NULL 报错)
            VkVideoEndCodingInfoKHR endInfo = { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
            pfnVkCmdEndVideoCodingKHR(cmd, &endInfo);
        }

        /**
                 * @brief 获取指定 Query 槽位的编码大小
                 * @param slot 对应 frameIdx % MAX_ENCODE_QUERIES
                 */
        uint64_t GetEncodedSize(uint32_t slot)
        {
            uint64_t bitstreamBytesWritten = 0;

            // 核心修复：将 hardcode 的 0 替换为传入的 slot
            VkResult result = vkGetQueryPoolResults(
                device_,
                queryPool_,
                slot,            // firstQuery: 使用指定的槽位
                1,               // queryCount: 查 1 个
                sizeof(uint64_t),
                &bitstreamBytesWritten,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );

            if (result != VK_SUCCESS) {
                return 0;
            }

            return bitstreamBytesWritten;
        }

        VkDeviceMemory GetBitstreamMemory() { return bitstreamBuffer_.getMemory(); }

    private:
        VkVideoSessionParametersKHR videoSessionParameters_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

        // 无头模式下的并发深度建议为 2 或 4
        // 因为 8K 帧的编码耗时较长，保持 2-4 个 Query 槽位可防止指令流阻塞
        static constexpr uint32_t MAX_ENCODE_QUERIES = 4;
        StdVideoAV1SequenceHeader stdSequenceHeader_ = {};
        StdVideoAV1TimingInfo stdTimingInfo_ = {};
        StdVideoAV1ColorConfig stdColorConfig_ = {};
        PFN_vkCreateVideoSessionKHR pfnVkCreateVideoSessionKHR = nullptr;
        PFN_vkDestroyVideoSessionKHR pfnVkDestroyVideoSessionKHR = nullptr;
        PFN_vkGetVideoSessionMemoryRequirementsKHR pfnVkGetVideoSessionMemoryRequirementsKHR = nullptr;
        PFN_vkBindVideoSessionMemoryKHR pfnVkBindVideoSessionMemoryKHR = nullptr;
        PFN_vkCmdBeginVideoCodingKHR pfnVkCmdBeginVideoCodingKHR = nullptr;
        PFN_vkCmdEndVideoCodingKHR pfnVkCmdEndVideoCodingKHR = nullptr;
        PFN_vkCmdEncodeVideoKHR pfnVkCmdEncodeVideoKHR = nullptr;
        PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR pfnVkGetPhysicalDeviceVideoCapabilitiesKHR = nullptr;
        PFN_vkCmdControlVideoCodingKHR pfnVkCmdControlVideoCodingKHR = nullptr;
        PFN_vkGetEncodedVideoSessionParametersKHR pfnVkGetEncodedVideoSessionParametersKHR = nullptr;

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
            pfnVkGetEncodedVideoSessionParametersKHR = (PFN_vkGetEncodedVideoSessionParametersKHR)load("vkGetEncodedVideoSessionParametersKHR");
        }

        struct ProfilePnextChain
        {
            VkVideoEncodeAV1ProfileInfoKHR av1Profile;
            VkVideoProfileInfoKHR videoProfile;
            VkVideoProfileListInfoKHR profileList;
            // 关键：反馈信息也必须是成员变量，保证生命周期
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo;
        } profileChain_{};

        void initProfileChain() {
            std::memset(&profileChain_, 0, sizeof(ProfilePnextChain));

            profileChain_.av1Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR;
            profileChain_.av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

            profileChain_.videoProfile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
            profileChain_.videoProfile.pNext = &profileChain_.av1Profile; // 连向 AV1
            profileChain_.videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
            profileChain_.videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profileChain_.videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profileChain_.videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

            profileChain_.feedbackInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR;
            profileChain_.feedbackInfo.pNext = &profileChain_.videoProfile; // 连向 VideoProfile

            profileChain_.profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
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
                throw std::runtime_error(
                    "Vulkan: Failed to create AV1 Session Parameters. Code: " + std::to_string(result));
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

                VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
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
                    throw std::runtime_error(
                        "Vulkan: Failed to allocate video session memory at index " + std::to_string(i));
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
            VkResult result = pfnVkBindVideoSessionMemoryKHR(device_, videoSession_, (uint32_t)binds.size(),
                                                             binds.data());
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(
                    "Vulkan: Failed to bind memory to Video Session. Result: " + std::to_string(result));
            }

            std::cout << "[VideoEncoder] Successfully allocated and bound " << memReqCount <<
                " memory regions for AV1 Session." << std::endl;
        }



        // 修改后的 createQueryPool 逻辑：
        void createQueryPool()
        {
            // 1. 准备反馈扩展结构
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{
                VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR
            };

            // 2. 关键点：将反馈结构插入到已有的 Profile 链条中，或者让 Profile 指向它
            // 规范要求 QueryPool -> FeedbackCreateInfo -> VideoProfile
            feedbackInfo.pNext = &profileChain_.videoProfile;

            VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            queryPoolInfo.pNext = &feedbackInfo; // 挂载整个链条
            queryPoolInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
            queryPoolInfo.queryCount = MAX_ENCODE_QUERIES;

            if (vkCreateQueryPool(device_, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create matching Query Pool");
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
