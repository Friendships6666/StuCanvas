// stucanvas/canvas/vulkan/video_encoder_h265.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_h265std.h>
#include <vk_video/vulkan_video_codec_h265std_encode.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include "buffer.hpp"
#include "video_encoder_av1.hpp"

namespace StuCanvas::Vulkan {

    class VideoEncoderH265 {
    public:
        static constexpr uint32_t MAX_ENCODE_QUERIES = 4;

        VideoEncoderH265(VkInstance instance, VkDevice device,
                         VkPhysicalDevice physicalDevice,
                         uint32_t queueFamilyIndex,
                         const ExportSettings& settings)
            : device_(device), settings_(settings)
        {
            loadVideoFunctions(instance, device);
            initProfileChain();

            std::cout << "[VideoEncoderH265] Initializing HEVC Encoder..." << std::endl;

            // 1. 查询能力
            VkVideoEncodeH265CapabilitiesKHR h265Caps{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR };
            VkVideoEncodeCapabilitiesKHR encodeCaps{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR, &h265Caps };
            VkVideoCapabilitiesKHR videoCaps{ VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR, &encodeCaps };

            pfnVkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &profileChain_.videoProfile, &videoCaps);

            // 2. 设置反馈标志
            if (encodeCaps.supportedEncodeFeedbackFlags & VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR) {
                profileChain_.feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;
            } else {
                profileChain_.feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR;
            }

            // 3. 创建 Session
            VkVideoSessionCreateInfoKHR sessionInfo{ VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
            sessionInfo.pVideoProfile = &profileChain_.videoProfile;
            sessionInfo.queueFamilyIndex = queueFamilyIndex;
            sessionInfo.maxCodedExtent = { settings.width, settings.height };
            sessionInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
            sessionInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR;
            sessionInfo.maxDpbSlots = 4;
            sessionInfo.maxActiveReferencePictures = 1;
            sessionInfo.pStdHeaderVersion = &videoCaps.stdHeaderVersion;

            if (pfnVkCreateVideoSessionKHR(device_, &sessionInfo, nullptr, &videoSession_) != VK_SUCCESS) {
                throw std::runtime_error("Vulkan: Failed to create HEVC Video Session.");
            }

            setupSessionMemory(physicalDevice);
            createSessionParameters();
            createQueryPool();

            VkDeviceSize bufferSize = (VkDeviceSize)MAX_ENCODE_QUERIES * 32 * 1024 * 1024;
            bitstreamBuffer_ = Buffer::Create(device_, physicalDevice, bufferSize,
                                              VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              &profileChain_.profileList);
        }

        ~VideoEncoderH265() {
            if (device_ == VK_NULL_HANDLE) return;
            vkDeviceWaitIdle(device_);
            if (videoSessionParameters_ != VK_NULL_HANDLE) pfnVkDestroyVideoSessionParametersKHR(device_, videoSessionParameters_, nullptr);
            if (videoSession_ != VK_NULL_HANDLE) pfnVkDestroyVideoSessionKHR(device_, videoSession_, nullptr);
            if (queryPool_ != VK_NULL_HANDLE) vkDestroyQueryPool(device_, queryPool_, nullptr);
            for (auto m : sessionMemories_) vkFreeMemory(device_, m, nullptr);
        }

void EncodeFrame(VkCommandBuffer cmd, VkImageView currView, VkImageView prevView, uint32_t frameIdx) {
    const bool isKey = (frameIdx % settings_.gopSize == 0);
    const uint32_t slot = frameIdx % MAX_ENCODE_QUERIES;
    const uint32_t currDpbIdx = frameIdx % 2;
    const uint32_t prevDpbIdx = (frameIdx + 1) % 2;

    // 1. 物理资源
    VkVideoPictureResourceInfoKHR currResource{ VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR, nullptr, {0,0}, {settings_.width, settings_.height}, 0, currView };
    VkVideoPictureResourceInfoKHR prevResource = currResource; prevResource.imageViewBinding = prevView;

    // 2. 当前帧 DPB 设置
    StdVideoEncodeH265ReferenceInfo stdRefCurr{};
    stdRefCurr.pic_type = isKey ? STD_VIDEO_H265_PICTURE_TYPE_IDR : STD_VIDEO_H265_PICTURE_TYPE_P;
    stdRefCurr.PicOrderCntVal = (int32_t)frameIdx;

    VkVideoEncodeH265DpbSlotInfoKHR h265DpbSetup{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR, nullptr, &stdRefCurr };
    VkVideoReferenceSlotInfoKHR setupSlot{ VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &h265DpbSetup, (int32_t)currDpbIdx, &currResource };

    // 3. 参考帧 DPB 设置 (必须准确描述旧图的信息)
    StdVideoEncodeH265ReferenceInfo stdRefPrev{};
    stdRefPrev.pic_type = STD_VIDEO_H265_PICTURE_TYPE_I; // 只要是合法的旧类型即可
    stdRefPrev.PicOrderCntVal = (int32_t)(frameIdx - 1); // 必须是上一帧的 POC

    VkVideoEncodeH265DpbSlotInfoKHR h265DpbRef{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR, nullptr, &stdRefPrev };
    VkVideoReferenceSlotInfoKHR refSlot{ VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, &h265DpbRef, (int32_t)prevDpbIdx, &prevResource };

    // 4. 构建 H.265 P帧强制要求的 RPS (参考图像集)
    StdVideoH265ShortTermRefPicSet stRps{};
    StdVideoEncodeH265ReferenceListsInfo refListInfo{};

    if (!isKey) {
        // 声明与上一帧的距离为 1
        stRps.num_negative_pics = 1;
        stRps.num_positive_pics = 0;
        stRps.delta_poc_s0_minus1[0] = 0; // (frameIdx) - (frameIdx - 1) - 1 = 0
        stRps.used_by_curr_pic_s0_flag = 1; // 激活此参考

        // 绑定参考列表到具体的 DPB 槽位
        refListInfo.num_ref_idx_l0_active_minus1 = 0; // 1个参考帧 (0+1)
        refListInfo.RefPicList0[0] = (uint8_t)prevDpbIdx; // 对应 prevDpbIdx
    }

    // 5. 图像切片信息
    StdVideoEncodeH265SliceSegmentHeader sliceHdr{};
    sliceHdr.slice_type = isKey ? STD_VIDEO_H265_SLICE_TYPE_I : STD_VIDEO_H265_SLICE_TYPE_P;
    VkVideoEncodeH265NaluSliceSegmentInfoKHR sliceInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR, nullptr, 0, &sliceHdr };

    StdVideoEncodeH265PictureInfo stdPic{};
    stdPic.pic_type = stdRefCurr.pic_type;
    stdPic.PicOrderCntVal = stdRefCurr.PicOrderCntVal;

    // 【核心修复】告诉驱动这张图是用来做参考的，且是关键帧
    stdPic.flags.is_reference = 1;
    if (isKey) {
        stdPic.flags.IrapPicFlag = 1; // IDR 帧必须具有此标志 (Intra Random Access Point)
    } else {
        // P 帧才需要加载 RPS
        stdPic.pShortTermRefPicSet = &stRps;
        stdPic.pRefLists = &refListInfo;
    }


    VkVideoEncodeH265PictureInfoKHR h265Pic{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR };
    h265Pic.pStdPictureInfo = &stdPic;
    h265Pic.naluSliceSegmentEntryCount = 1;
    h265Pic.pNaluSliceSegmentEntries = &sliceInfo;

    // 6. 指令组装
    VkVideoEncodeInfoKHR encInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR, &h265Pic };
    encInfo.srcPictureResource = currResource;
    encInfo.dstBuffer = bitstreamBuffer_.getBuffer();
    encInfo.dstBufferOffset = (VkDeviceSize)slot * 32 * 1024 * 1024;
    encInfo.dstBufferRange = 32 * 1024 * 1024;
    encInfo.pSetupReferenceSlot = &setupSlot;
    if (!isKey) {
        encInfo.referenceSlotCount = 1;
        encInfo.pReferenceSlots = &refSlot;
    }

    // 7. 录制
    vkCmdResetQueryPool(cmd, queryPool_, slot, 1);
    VkVideoReferenceSlotInfoKHR activeSlots[2] = { setupSlot, refSlot };
    activeSlots[0].slotIndex = -1; // 当前帧尚未写入，标记为未激活

    VkVideoBeginCodingInfoKHR beginInfo{ VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
    beginInfo.videoSession = videoSession_;
    beginInfo.videoSessionParameters = videoSessionParameters_;
    beginInfo.referenceSlotCount = isKey ? 1 : 2;
    beginInfo.pReferenceSlots = activeSlots;

    pfnVkCmdBeginVideoCodingKHR(cmd, &beginInfo);

    if (isKey) {
        VkVideoCodingControlInfoKHR ctrl{ VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR, nullptr, VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR };
        pfnVkCmdControlVideoCodingKHR(cmd, &ctrl);
    }

    vkCmdBeginQuery(cmd, queryPool_, slot, 0);
    pfnVkCmdEncodeVideoKHR(cmd, &encInfo);
    vkCmdEndQuery(cmd, queryPool_, slot);

    VkVideoEndCodingInfoKHR endInfo{ VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
    pfnVkCmdEndVideoCodingKHR(cmd, &endInfo);
}

        uint64_t GetEncodedSize(uint32_t slot) {
            uint64_t bytes = 0;
            VkResult res = vkGetQueryPoolResults(device_, queryPool_, slot, 1, sizeof(uint64_t), &bytes, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            return (res == VK_SUCCESS) ? bytes : 0;
        }

        std::vector<uint8_t> getCodecHeader() {
            // 【核心修复】H265 专属的获取信息结构
            VkVideoEncodeH265SessionParametersGetInfoKHR h265GetInfo{
                VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR
            };
            h265GetInfo.writeStdVPS = VK_TRUE;
            h265GetInfo.writeStdSPS = VK_TRUE;
            h265GetInfo.writeStdPPS = VK_TRUE;

            VkVideoEncodeSessionParametersGetInfoKHR get{
                VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
                &h265GetInfo, // 指向 H265 专用结构
                videoSessionParameters_
            };

            VkVideoEncodeSessionParametersFeedbackInfoKHR fb{
                VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR
            };

            size_t size = 0;
            pfnVkGetEncodedVideoSessionParametersKHR(device_, &get, &fb, &size, nullptr);
            if (size == 0) return {};

            std::vector<uint8_t> data(size);
            pfnVkGetEncodedVideoSessionParametersKHR(device_, &get, &fb, &size, data.data());
            return data;
        }

        VkDeviceMemory GetBitstreamMemory() { return bitstreamBuffer_.getMemory(); }

    private:
        struct ProfilePnextChain {
            VkVideoEncodeH265ProfileInfoKHR h265Profile;
            VkVideoProfileInfoKHR videoProfile;
            VkVideoProfileListInfoKHR profileList;
            VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo;
        } profileChain_{};

        void initProfileChain() {
            std::memset(&profileChain_, 0, sizeof(ProfilePnextChain));
            profileChain_.h265Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR;
            // 修正：正式版成员名为 stdProfileIdc
            profileChain_.h265Profile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;

            profileChain_.videoProfile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
            profileChain_.videoProfile.pNext = &profileChain_.h265Profile;
            profileChain_.videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
            profileChain_.videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profileChain_.videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profileChain_.videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

            profileChain_.profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
            profileChain_.profileList.profileCount = 1;
            profileChain_.profileList.pProfiles = &profileChain_.videoProfile;

            profileChain_.feedbackInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR;
            profileChain_.feedbackInfo.pNext = &profileChain_.videoProfile;
        }

void createSessionParameters() {
    // 1. 档次与级别 (8K 需要 Level 6.0 或 6.1)
    static StdVideoH265ProfileTierLevel ptl{};
    ptl.general_profile_idc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
    ptl.general_level_idc = STD_VIDEO_H265_LEVEL_IDC_6_1;

    // 2. 解码图片缓冲管理 (H.265 强制要求)
    static StdVideoH265DecPicBufMgr dpb{};
    dpb.max_dec_pic_buffering_minus1[0] = 3;
    dpb.max_num_reorder_pics[0] = 0;
    dpb.max_latency_increase_plus1[0] = 0;

    // 3. VPS 配置
    static StdVideoH265VideoParameterSet vps{};
    vps.vps_video_parameter_set_id = 0;
    vps.vps_max_sub_layers_minus1 = 0;
    vps.pProfileTierLevel = &ptl; // 【核心修复】绝不能为 null
    vps.pDecPicBufMgr = &dpb;     // 【核心修复】绝不能为 null

    // 4. SPS 配置
    static StdVideoH265SequenceParameterSet sps{};
    sps.pic_width_in_luma_samples = settings_.width;
    sps.pic_height_in_luma_samples = settings_.height;
    sps.chroma_format_idc = STD_VIDEO_H265_CHROMA_FORMAT_IDC_420;
    sps.log2_max_pic_order_cnt_lsb_minus4 = 4;
    sps.pProfileTierLevel = &ptl; // 【核心修复】
    sps.pDecPicBufMgr = &dpb;     // 【核心修复】

    // 5. PPS 配置
    static StdVideoH265PictureParameterSet pps{};
    pps.pps_pic_parameter_set_id = 0;
    pps.pps_seq_parameter_set_id = 0;

    VkVideoEncodeH265SessionParametersAddInfoKHR h265Add{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR };
    h265Add.stdVPSCount = 1; h265Add.pStdVPSs = &vps;
    h265Add.stdSPSCount = 1; h265Add.pStdSPSs = &sps;
    h265Add.stdPPSCount = 1; h265Add.pStdPPSs = &pps;

    VkVideoEncodeH265SessionParametersCreateInfoKHR h265CreateInfo{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR };
    h265CreateInfo.maxStdVPSCount = 1;
    h265CreateInfo.maxStdSPSCount = 1;
    h265CreateInfo.maxStdPPSCount = 1;
    h265CreateInfo.pParametersAddInfo = &h265Add;

    VkVideoSessionParametersCreateInfoKHR paramsInfo{ VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR };
    paramsInfo.pNext = &h265CreateInfo;
    paramsInfo.videoSession = videoSession_;

    VkResult res = pfnVkCreateVideoSessionParametersKHR(device_, &paramsInfo, nullptr, &videoSessionParameters_);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Vulkan: Failed to create H265 Session Parameters.");
    }
}

        void createQueryPool() {
            VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, &profileChain_.feedbackInfo };
            info.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
            info.queryCount = MAX_ENCODE_QUERIES;
            vkCreateQueryPool(device_, &info, nullptr, &queryPool_);
        }

        void setupSessionMemory(VkPhysicalDevice physDev) {
            uint32_t count = 0;
            pfnVkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &count, nullptr);

            // 注意：sType 必须正确初始化
            std::vector<VkVideoSessionMemoryRequirementsKHR> memReqs(count,
                { VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR });
            pfnVkGetVideoSessionMemoryRequirementsKHR(device_, videoSession_, &count, memReqs.data());

            std::vector<VkBindVideoSessionMemoryInfoKHR> binds;
            sessionMemories_.resize(count);

            for (uint32_t i = 0; i < count; ++i) {
                const VkMemoryRequirements& req = memReqs[i].memoryRequirements;

                VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                ai.allocationSize = req.size;

                // 【核心修复】传入特定的 memoryTypeBits，而不是 0xFFFFFFFF
                ai.memoryTypeIndex = findMemoryType(physDev, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                if (vkAllocateMemory(device_, &ai, nullptr, &sessionMemories_[i]) != VK_SUCCESS) {
                    throw std::runtime_error("Vulkan: Failed to allocate session memory for index " + std::to_string(i));
                }

                VkBindVideoSessionMemoryInfoKHR bind{ VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR };
                bind.memoryBindIndex = memReqs[i].memoryBindIndex;
                bind.memory = sessionMemories_[i];
                bind.memorySize = req.size;
                binds.push_back(bind);
            }

            pfnVkBindVideoSessionMemoryKHR(device_, videoSession_, (uint32_t)binds.size(), binds.data());
        }

        uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                // typeFilter 就是显卡要求的 Bits 掩码
                if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                    return i;
                }
            }
            // 备选方案：如果找不到 DEVICE_LOCAL，至少满足 typeFilter 要求
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                if (typeFilter & (1 << i)) return i;
            }
            return 0;
        }

        void loadVideoFunctions(VkInstance inst, VkDevice dev) {
            auto loadInst = [&](const char* n) { return vkGetInstanceProcAddr(inst, n); };
            auto loadDev = [&](const char* n) { return vkGetDeviceProcAddr(dev, n); };
            pfnVkGetPhysicalDeviceVideoCapabilitiesKHR = (PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)loadInst("vkGetPhysicalDeviceVideoCapabilitiesKHR");
            pfnVkCreateVideoSessionKHR = (PFN_vkCreateVideoSessionKHR)loadDev("vkCreateVideoSessionKHR");
            pfnVkDestroyVideoSessionKHR = (PFN_vkDestroyVideoSessionKHR)loadDev("vkDestroyVideoSessionKHR");
            pfnVkGetVideoSessionMemoryRequirementsKHR = (PFN_vkGetVideoSessionMemoryRequirementsKHR)loadDev("vkGetVideoSessionMemoryRequirementsKHR");
            pfnVkBindVideoSessionMemoryKHR = (PFN_vkBindVideoSessionMemoryKHR)loadDev("vkBindVideoSessionMemoryKHR");
            pfnVkCreateVideoSessionParametersKHR = (PFN_vkCreateVideoSessionParametersKHR)loadDev("vkCreateVideoSessionParametersKHR");
            pfnVkDestroyVideoSessionParametersKHR = (PFN_vkDestroyVideoSessionParametersKHR)loadDev("vkDestroyVideoSessionParametersKHR");
            pfnVkGetEncodedVideoSessionParametersKHR = (PFN_vkGetEncodedVideoSessionParametersKHR)loadDev("vkGetEncodedVideoSessionParametersKHR");
            pfnVkCmdBeginVideoCodingKHR = (PFN_vkCmdBeginVideoCodingKHR)loadDev("vkCmdBeginVideoCodingKHR");
            pfnVkCmdEndVideoCodingKHR = (PFN_vkCmdEndVideoCodingKHR)loadDev("vkCmdEndVideoCodingKHR");
            pfnVkCmdEncodeVideoKHR = (PFN_vkCmdEncodeVideoKHR)loadDev("vkCmdEncodeVideoKHR");
            pfnVkCmdControlVideoCodingKHR = (PFN_vkCmdControlVideoCodingKHR)loadDev("vkCmdControlVideoCodingKHR");
        }

        VkDevice device_;
        VkVideoSessionKHR videoSession_ = VK_NULL_HANDLE;
        VkVideoSessionParametersKHR videoSessionParameters_ = VK_NULL_HANDLE;
        VkQueryPool queryPool_ = VK_NULL_HANDLE;
        std::vector<VkDeviceMemory> sessionMemories_;
        Buffer bitstreamBuffer_;
        ExportSettings settings_;

        PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR pfnVkGetPhysicalDeviceVideoCapabilitiesKHR;
        PFN_vkCreateVideoSessionKHR pfnVkCreateVideoSessionKHR;
        PFN_vkDestroyVideoSessionKHR pfnVkDestroyVideoSessionKHR;
        PFN_vkGetVideoSessionMemoryRequirementsKHR pfnVkGetVideoSessionMemoryRequirementsKHR;
        PFN_vkBindVideoSessionMemoryKHR pfnVkBindVideoSessionMemoryKHR;
        PFN_vkCreateVideoSessionParametersKHR pfnVkCreateVideoSessionParametersKHR;
        PFN_vkDestroyVideoSessionParametersKHR pfnVkDestroyVideoSessionParametersKHR;
        PFN_vkGetEncodedVideoSessionParametersKHR pfnVkGetEncodedVideoSessionParametersKHR;
        PFN_vkCmdBeginVideoCodingKHR pfnVkCmdBeginVideoCodingKHR;
        PFN_vkCmdEndVideoCodingKHR pfnVkCmdEndVideoCodingKHR;
        PFN_vkCmdEncodeVideoKHR pfnVkCmdEncodeVideoKHR;
        PFN_vkCmdControlVideoCodingKHR pfnVkCmdControlVideoCodingKHR;
    };
}