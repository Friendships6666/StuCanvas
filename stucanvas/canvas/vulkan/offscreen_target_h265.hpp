// stucanvas/canvas/vulkan/offscreen_target.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_h265std.h>
#include <vk_video/vulkan_video_codec_h265std_encode.h>
#include <stdexcept>
#include <vector>
#include <array>
#include <iostream>

namespace StuCanvas::Vulkan {

/**
 * @brief 离屏渲染目标管理类
 *
 * 修正说明：
 * 1. 移除了 YUV 图像中不必要的 SAMPLED_BIT，避免强制要求 YcbcrConversion。
 * 2. 严格按 Plane 拆分权限，确保整体视图不包含 STORAGE 权限。
 */
class OffscreenTarget {
public:
    OffscreenTarget(VkDevice device,
                   VkPhysicalDevice physDev,
                   uint32_t width,
                   uint32_t height,
                   VkSampleCountFlagBits msaaSamples,
                   VkRenderPass renderPass,
                   bool enableVideoProfile = false)
        : device_(device), width_(width), height_(height)
    {
        // ---------------------------------------------------------
        // 1. MSAA 颜色附件 (RGBA8)
        // ---------------------------------------------------------
        createImage(width, height, msaaSamples, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaColorImage_, msaaColorMemory_, physDev, nullptr);

        msaaColorView_ = createImageView(msaaColorImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_ASPECT_COLOR_BIT,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);

        // ---------------------------------------------------------
        // 2. MSAA 深度附件 (D32)
        // ---------------------------------------------------------
        createImage(width, height, msaaSamples, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaDepthImage_, msaaDepthMemory_, physDev, nullptr);

        msaaDepthView_ = createImageView(msaaDepthImage_, VK_FORMAT_D32_SFLOAT,
                                        VK_IMAGE_ASPECT_DEPTH_BIT,
                                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

        // ---------------------------------------------------------
        // 3. Resolve RGB 图像 (RGBA8, 1 Sample) - 供 Compute Shader 采样
        // ---------------------------------------------------------
        createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    resolveRGBImage_, resolveRGBMemory_, physDev, nullptr);

        resolveRGBView_ = createImageView(resolveRGBImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_ASPECT_COLOR_BIT,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        // ---------------------------------------------------------
        // 4. YUV (NV12) 图像组 - 用于转码导出
        // ---------------------------------------------------------
        for (int i = 0; i < 2; ++i) {
            // 【核心修正】: 移除 VK_IMAGE_USAGE_SAMPLED_BIT
            // YUV 图像在导出流程中只需要被 Compute 写入 (STORAGE) 和被 Transfer 读取 (SRC)
            VkImageUsageFlags yuvImageUsage = VK_IMAGE_USAGE_STORAGE_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            if (enableVideoProfile) {
                yuvImageUsage |= (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);
            }

            // 构造 H.265 Profile 链（可选）
            VkVideoEncodeH265ProfileInfoKHR h265Profile{ VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR };
            VkVideoProfileInfoKHR videoProfile{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
            VkVideoProfileListInfoKHR profileList{ VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
            void* pNextChain = nullptr;

            if (enableVideoProfile) {
                h265Profile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
                videoProfile.pNext = &h265Profile;
                videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
                videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
                videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
                videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
                profileList.profileCount = 1;
                profileList.pProfiles = &videoProfile;
                pNextChain = &profileList;
            }

            createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                        VK_IMAGE_TILING_OPTIMAL, yuvImageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        yuvImages_[i], yuvMemories_[i], physDev, pNextChain);

            // A. 整体视图 (NV12 格式)：移除 STORAGE 权限，且不带 SAMPLED 权限
            VkImageUsageFlags wholeViewUsage = yuvImageUsage & ~VK_IMAGE_USAGE_STORAGE_BIT;
            yuvViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                                          VK_IMAGE_ASPECT_COLOR_BIT, wholeViewUsage);

            // B. Y 平面视图 (R8 格式)：支持 STORAGE 权限
            yPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8_UNORM,
                                             VK_IMAGE_ASPECT_PLANE_0_BIT, yuvImageUsage);

            // C. UV 平面视图 (R8G8 格式)：支持 STORAGE 权限
            uvPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8G8_UNORM,
                                              VK_IMAGE_ASPECT_PLANE_1_BIT, yuvImageUsage);
        }

        // ---------------------------------------------------------
        // 5. 离屏 Framebuffer
        // ---------------------------------------------------------
        std::array<VkImageView, 3> attachments = { msaaColorView_, msaaDepthView_, resolveRGBView_ };
        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to create framebuffer.");
        }
    }

    ~OffscreenTarget() {
        if (device_ == VK_NULL_HANDLE) return;
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        for (int i = 0; i < 2; ++i) {
            if (yuvViews_[i]) vkDestroyImageView(device_, yuvViews_[i], nullptr);
            if (yPlaneViews_[i]) vkDestroyImageView(device_, yPlaneViews_[i], nullptr);
            if (uvPlaneViews_[i]) vkDestroyImageView(device_, uvPlaneViews_[i], nullptr);
            if (yuvImages_[i]) vkDestroyImage(device_, yuvImages_[i], nullptr);
            if (yuvMemories_[i]) vkFreeMemory(device_, yuvMemories_[i], nullptr);
        }
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

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    VkFramebuffer getFramebuffer() const { return framebuffer_; }
    VkImage       getResolveRGBImage() const { return resolveRGBImage_; }
    VkImageView   getResolveRGBView() const { return resolveRGBView_; }
    VkImage       getYuvImage(uint32_t idx) const { return yuvImages_[idx % 2]; }
    VkImageView   getYuvView(uint32_t idx) const { return yuvViews_[idx % 2]; }
    VkImageView   getYPlaneView(uint32_t idx) const { return yPlaneViews_[idx % 2]; }
    VkImageView   getUVPlaneView(uint32_t idx) const { return uvPlaneViews_[idx % 2]; }

private:
    void createImage(uint32_t w, uint32_t h, VkSampleCountFlagBits samples, VkFormat format,
                    VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                    VkImage& image, VkDeviceMemory& memory, VkPhysicalDevice physDev, void* pNext)
    {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.pNext = pNext;
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

        if (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) {
            imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        }

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("OffscreenTarget: Failed to create VkImage.");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, image, &memReq);
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

        uint32_t memoryTypeIndex = 0xFFFFFFFF;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                memoryTypeIndex = i;
                break;
            }
        }
        if (memoryTypeIndex == 0xFFFFFFFF) throw std::runtime_error("Failed to find memory type");

        VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("OffscreenTarget: Failed to allocate memory.");

        vkBindImageMemory(device_, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageUsageFlags usage) {
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { aspectFlags, 0, 1, 0, 1 };

        VkImageViewUsageCreateInfo usageInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
        usageInfo.usage = usage;
        viewInfo.pNext = &usageInfo;

        VkImageView imageView;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to create VkImageView.");
        }
        return imageView;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_, height_;
    VkImage msaaColorImage_ = VK_NULL_HANDLE;
    VkImage msaaDepthImage_ = VK_NULL_HANDLE;
    VkImage resolveRGBImage_ = VK_NULL_HANDLE;
    VkImage yuvImages_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory msaaColorMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaDepthMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory resolveRGBMemory_ = VK_NULL_HANDLE;
    VkDeviceMemory yuvMemories_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    VkImageView msaaDepthView_ = VK_NULL_HANDLE;
    VkImageView resolveRGBView_ = VK_NULL_HANDLE;
    VkImageView yuvViews_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView yPlaneViews_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView uvPlaneViews_[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

} // namespace StuCanvas::Vulkan