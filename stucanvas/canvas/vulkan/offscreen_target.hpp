#pragma once

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vk_video/vulkan_video_codec_av1std_encode.h>
#include <stdexcept>
#include <vector>
#include <array>
#include <iostream>

namespace StuCanvas::Vulkan {

/**
 * @brief 离屏渲染目标封装
 * 管理 8K 渲染及视频编码所需的物理图像资源
 */
class OffscreenTarget {
public:
    OffscreenTarget(VkDevice device,
                   VkPhysicalDevice physDev,
                   uint32_t width,
                   uint32_t height,
                   VkSampleCountFlagBits msaaSamples,
                   VkRenderPass renderPass)
        : device_(device), width_(width), height_(height)
    {
        // 1. 创建 MSAA 颜色缓冲 (8K 多重采样)
        // 仅在子通道内部使用，标记为 TRANSIENT
        createImage(width, height, msaaSamples, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaColorImage_, msaaColorMemory_, physDev);
        msaaColorView_ = createImageView(msaaColorImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        // 2. 创建 MSAA 深度缓冲 (8K 多重采样)
        createImage(width, height, msaaSamples, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaDepthImage_, msaaDepthMemory_, physDev);
        msaaDepthView_ = createImageView(msaaDepthImage_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 3. 创建 Resolve 目标 (8K 单采样 RGB)
        // 注意：必须包含 SAMPLED_BIT 供计算着色器读取，且作为渲染后的 Resolve 目标
        createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    resolveRGBImage_, resolveRGBMemory_, physDev);
        resolveRGBView_ = createImageView(resolveRGBImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        // 4. 创建最终编码源 (8K NV12 2-Plane)
        // 核心修复：必须开启 MUTABLE 以创建 R8/R8G8 平面视图，且绑定视频 Profile
        createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    yuvImage_, yuvMemory_, physDev);
        // 此 View 用于编码器读取（全格式视图）
        yuvView_ = createImageView(yuvImage_, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

        // 5. 创建离屏帧缓冲
        // 附件顺序对应 RenderPass: 0=MSAA Color, 1=MSAA Depth, 2=Resolve Target
        std::array<VkImageView, 3> attachments = {
            msaaColorView_,
            msaaDepthView_,
            resolveRGBView_
        };

        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create offscreen framebuffer");
        }
    }

    ~OffscreenTarget() {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);

        vkDestroyImageView(device_, yuvView_, nullptr);
        vkDestroyImage(device_, yuvImage_, nullptr);
        vkFreeMemory(device_, yuvMemory_, nullptr);

        vkDestroyImageView(device_, resolveRGBView_, nullptr);
        vkDestroyImage(device_, resolveRGBImage_, nullptr);
        vkFreeMemory(device_, resolveRGBMemory_, nullptr);

        vkDestroyImageView(device_, msaaDepthView_, nullptr);
        vkDestroyImage(device_, msaaDepthImage_, nullptr);
        vkFreeMemory(device_, msaaDepthMemory_, nullptr);

        vkDestroyImageView(device_, msaaColorView_, nullptr);
        vkDestroyImage(device_, msaaColorImage_, nullptr);
        vkFreeMemory(device_, msaaColorMemory_, nullptr);
    }

    // --- Getter 修复 ---
    VkFramebuffer getFramebuffer()   const { return framebuffer_; }
    VkImage       getResolveRGBImage() const { return resolveRGBImage_; }
    VkImage       getYuvImage()        const { return yuvImage_; }
    VkImageView   getYuvView()         const { return yuvView_; }

    /**
     * @brief 返回用于计算着色器输入的单采样视图 (1x)
     * 修复了之前返回 msaaColorView_ (8x) 的逻辑错误
     */
    VkImageView   getResolveRGBView()  const { return resolveRGBView_; }

private:

    void createImage(uint32_t w, uint32_t h, VkSampleCountFlagBits samples, VkFormat format,
                    VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                    VkImage& image, VkDeviceMemory& memory, VkPhysicalDevice physDev)
    {
        // 准备视频 Profile (用于显存布局对齐)
        VkVideoEncodeAV1ProfileInfoKHR av1Profile{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR };
        av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

        VkVideoProfileInfoKHR videoProfile{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        videoProfile.pNext = &av1Profile;
        videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
        videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
        videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

        VkVideoProfileListInfoKHR profileList{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        profileList.profileCount = 1;
        profileList.pProfiles = &videoProfile;

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { w, h, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = samples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // 核心修复：多平面图像必须设置 MUTABLE 以便在 Compute 中以 R8/R8G8 访问
        imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;

        // 核心修复：如果是视频资源，必须挂载 Profile 链
        if (usage & (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR)) {
            imageInfo.pNext = &profileList;
        }

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to create VkImage.");
        }

        // 获取内存需求
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, image, &memReq);

        VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize = memReq.size;

        // 核心修复：必须使用驱动返回的 memoryTypeBits 进行过滤
        allocInfo.memoryTypeIndex = findMemoryType(physDev, memReq.memoryTypeBits, props);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to allocate image memory.");
        }
        vkBindImageMemory(device_, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {

        VkVideoEncodeAV1ProfileInfoKHR av1Profile{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR };
        av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

        VkVideoProfileInfoKHR videoProfile{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        videoProfile.pNext = &av1Profile;
        videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
        videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
        videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

        VkVideoProfileListInfoKHR profileList{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        profileList.profileCount = 1;
        profileList.pProfiles = &videoProfile;


        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        // 核心修复：正式版规范中，ImageViewCreateInfo 的 pNext 严禁挂载视频 Profile
        // 视频特性是自动从底层的 VkImage 继承的
        viewInfo.pNext = nullptr;

        VkImageView imageView;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to create VkImageView.");
        }
        return imageView;
    }

    uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

        // 严谨逻辑：先找满足 typeFilter 且包含 props 的
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        // 如果找不到 DEVICE_LOCAL，回退到第一个符合 filter 的类型（视频 Session 的某些 Buffer 可能要求 index 3）
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if (typeFilter & (1 << i)) return i;
        }
        return 0;
    }

    VkDevice device_;
    uint32_t width_, height_;

    VkImage msaaColorImage_, msaaDepthImage_, resolveRGBImage_, yuvImage_;
    VkDeviceMemory msaaColorMemory_, msaaDepthMemory_, resolveRGBMemory_, yuvMemory_;
    VkImageView msaaColorView_, msaaDepthView_, resolveRGBView_, yuvView_;
    VkFramebuffer framebuffer_;
};

} // namespace StuCanvas::Vulkan