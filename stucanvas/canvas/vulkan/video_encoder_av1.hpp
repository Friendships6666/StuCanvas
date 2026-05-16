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
         * @brief 录制单帧 AV1 编码指令
         * @param cmd 视频编码队列的 Command Buffer
         * @param srcView 必须是 NV12 (2PLANE) 格式且带有 VIDEO_ENCODE_SRC 标志的 ImageView
         * @param frameIdx 全局绝对帧序号
         */
        void EncodeFrame(VkCommandBuffer cmd, VkImageView srcView, uint32_t frameIdx)
        {
            bool isKeyFrame = (frameIdx % settings_.gopSize == 0);
            uint32_t querySlot = frameIdx % MAX_ENCODE_QUERIES;

            // 8K 场景下，为了支持流水线并行，建议 dstBuffer 偏移量随 querySlot 移动
            // 防止当前帧还在编码时，下一帧的录制覆盖了 Buffer 空间
            VkDeviceSize bitstreamOffset = querySlot * (32 * 1024 * 1024);

            // ---------------------------------------------------------
            // 1. 配置 AV1 标准参考帧管理 (DPB Logic)
            // ---------------------------------------------------------
            StdVideoEncodeAV1PictureInfo stdPicInfo{};
            stdPicInfo.frame_type = isKeyFrame ? STD_VIDEO_AV1_FRAME_TYPE_KEY : STD_VIDEO_AV1_FRAME_TYPE_INTER;
            stdPicInfo.order_hint = (uint8_t)(frameIdx % 256);

            // 配置哪些 DPB 槽位需要被当前编码后的图像刷新 (对于 KeyFrame 通常全刷新)
            stdPicInfo.refresh_frame_flags = isKeyFrame ? 0xFF : 0x01;

            // 对于 AV1，即使是 P 帧也需要定义参考索引映射
            if (!isKeyFrame)
            {
                stdPicInfo.primary_ref_frame = 0; // 选 Slot 0 作为主参考
                for (int i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; ++i)
                {
                    stdPicInfo.ref_frame_idx[i] = 0; // 简单起见，全部指向最近的一个 Slot
                }
            }

            // ---------------------------------------------------------
            // 2. 配置 AV1 Vulkan 扩展信息
            // ---------------------------------------------------------
            VkVideoEncodeAV1PictureInfoKHR av1PicInfo{VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR};
            av1PicInfo.pStdPictureInfo = &stdPicInfo;

            // 预测模式设置
            av1PicInfo.predictionMode = isKeyFrame
                                            ? VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR
                                            : VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;

            // 关键修复：填入参考帧映射（Validation Error 提到的 7 个 Slot 指向）
            for (uint32_t i = 0; i < 7; ++i)
            {
                av1PicInfo.referenceNameSlotIndices[i] = -1; // 默认无参考
            }
            if (!isKeyFrame)
            {
                av1PicInfo.referenceNameSlotIndices[STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - 1] = 0; // LAST 指向 Slot 0
            }

            // ---------------------------------------------------------
            // 3. 配置参考槽位 (Reference Slots) - 解决 pSetupReferenceSlot 为空的问题
            // ---------------------------------------------------------
            // 定义当前帧编码后应该存入哪个槽位（Setup Slot）
            VkVideoReferenceSlotInfoKHR setupSlot{VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR};
            setupSlot.slotIndex = 0; // 我们使用 Slot 0 作为滚动参考槽
            setupSlot.pPictureResource = nullptr; // 在编码时通常由驱动内部管理，设为 null

            // 定义当前帧需要参考哪些槽位（Reference Slots）
            VkVideoReferenceSlotInfoKHR referenceSlot{VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR};
            referenceSlot.slotIndex = 0;
            referenceSlot.pPictureResource = nullptr;

            // ---------------------------------------------------------
            // 4. 填充通用编码指令结构
            // ---------------------------------------------------------
            VkVideoEncodeInfoKHR encodeInfo{VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR};
            encodeInfo.pNext = &av1PicInfo;

            // 关键修复：补全 srcPictureResource 内部结构
            encodeInfo.srcPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            encodeInfo.srcPictureResource.imageViewBinding = srcView;
            encodeInfo.srcPictureResource.codedExtent = {settings_.width, settings_.height};
            encodeInfo.srcPictureResource.codedOffset = {0, 0};

            // 比特流目标缓冲
            encodeInfo.dstBuffer = bitstreamBuffer_.getBuffer();
            encodeInfo.dstBufferOffset = bitstreamOffset;
            encodeInfo.dstBufferRange = 32 * 1024 * 1024;

            // 绑定参考帧逻辑
            encodeInfo.pSetupReferenceSlot = &setupSlot;
            if (!isKeyFrame)
            {
                encodeInfo.referenceSlotCount = 1;
                encodeInfo.pReferenceSlots = &referenceSlot;
            }
            else
            {
                encodeInfo.referenceSlotCount = 0;
                encodeInfo.pReferenceSlots = nullptr;
            }

            // ---------------------------------------------------------
            // 5. 录制指令流
            // ---------------------------------------------------------

            // 关键修复：Query 重置必须在 Coding Scope 之外
            vkCmdResetQueryPool(cmd, queryPool_, querySlot, 1);

            VkVideoBeginCodingInfoKHR beginInfo{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
            beginInfo.videoSession = videoSession_;
            // 关键修复：必须传入 SessionParameters，否则驱动无法获取 Sequence Header
            beginInfo.videoSessionParameters = videoSessionParameters_;

            // 如果有参考帧，BeginCoding 需要知道涉及哪些槽位（本示例为 Slot 0）
            beginInfo.referenceSlotCount = encodeInfo.referenceSlotCount + 1;
            beginInfo.pReferenceSlots = &setupSlot;

            pfnVkCmdBeginVideoCodingKHR(cmd, &beginInfo);

            // 开启长度反馈查询
            vkCmdBeginQuery(cmd, queryPool_, querySlot, 0);

            // 发射真正的硬件编码任务
            pfnVkCmdEncodeVideoKHR(cmd, &encodeInfo);

            vkCmdEndQuery(cmd, queryPool_, querySlot);

            // 结束作用域
            pfnVkCmdEndVideoCodingKHR(cmd, nullptr);
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



        }
        struct ProfilePnextChain {
            VkVideoEncodeAV1ProfileInfoKHR av1Profile;
            VkVideoProfileInfoKHR videoProfile;
            VkVideoProfileListInfoKHR profileList;
        } profileChain_;
        void initProfileChain() {
            profileChain_.av1Profile = { VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR };
            profileChain_.av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

            profileChain_.videoProfile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
            profileChain_.videoProfile.pNext = &profileChain_.av1Profile;
            profileChain_.videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
            profileChain_.videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profileChain_.videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profileChain_.videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

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

        void createQueryPool()
        {
            // 1. 必须配置与 Session 一致的 Profile 链
            VkVideoEncodeAV1ProfileInfoKHR av1Profile{VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR};
            av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

            VkVideoProfileInfoKHR videoProfile{VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR};
            videoProfile.pNext = &profileChain_.profileList;;
            videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
            videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

            // 2. 配置反馈类型 (Feedback Flags)
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{
                VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR
            };
            // 关键：获取写入比特流的准确长度
            feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

            // 3. 创建 Query Pool
            VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            // 核心：pNext 必须挂载 Profile 信息和 Feedback 信息
            queryPoolInfo.pNext = &videoProfile;
            videoProfile.pNext = &feedbackInfo; // 嵌套链

            queryPoolInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
            queryPoolInfo.queryCount = MAX_ENCODE_QUERIES;

            if (vkCreateQueryPool(device_, &queryPoolInfo, nullptr, &queryPool_) != VK_SUCCESS)
            {
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
