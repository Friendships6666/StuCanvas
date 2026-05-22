// stucanvas/canvas/vulkan/offscreen_target.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vk_video/vulkan_video_codec_av1std_encode.h>
#include <stdexcept>
#include <vector>
#include <array>

namespace StuCanvas::Vulkan {

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
        createImage(width, height, msaaSamples, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaColorImage_, msaaColorMemory_, physDev);
        msaaColorView_ = createImageView(msaaColorImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        createImage(width, height, msaaSamples, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaDepthImage_, msaaDepthMemory_, physDev);
        msaaDepthView_ = createImageView(msaaDepthImage_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

        createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    resolveRGBImage_, resolveRGBMemory_, physDev);
        resolveRGBView_ = createImageView(resolveRGBImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        // ---------------------------------------------------------
        // 核心修复：YUV 物理图像组 (双缓冲)
        // ---------------------------------------------------------
        for (int i = 0; i < 2; ++i) {
            createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                        VK_IMAGE_TILING_OPTIMAL,
                        // 必须包含所有被 View 用到的位
                        VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
                        VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
                        VK_IMAGE_USAGE_STORAGE_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        yuvImages_[i], yuvMemories_[i], physDev);

            // 整体视图 (供 Encoder 读取)
            yuvViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
            // 分平面视图 (供 Compute 写入)
            yPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT);
            uvPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_PLANE_1_BIT);
        }

        std::array<VkImageView, 3> attachments = { msaaColorView_, msaaDepthView_, resolveRGBView_ };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
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
        if (device_ == VK_NULL_HANDLE) return;
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        for(int i=0; i<2; ++i) {
            vkDestroyImageView(device_, yuvViews_[i], nullptr);
            vkDestroyImageView(device_, yPlaneViews_[i], nullptr);
            vkDestroyImageView(device_, uvPlaneViews_[i], nullptr);
            vkDestroyImage(device_, yuvImages_[i], nullptr);
            vkFreeMemory(device_, yuvMemories_[i], nullptr);
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

    VkFramebuffer getFramebuffer() const { return framebuffer_; }
    VkImage       getResolveRGBImage() const { return resolveRGBImage_; }
    VkImageView   getResolveRGBView() const { return resolveRGBView_; }

    // 双缓冲获取接口
    VkImage       getYuvImage(uint32_t idx) const { return yuvImages_[idx % 2]; }
    VkImageView   getYuvView(uint32_t idx) const { return yuvViews_[idx % 2]; }
    VkImageView   getYPlaneView(uint32_t idx) const { return yPlaneViews_[idx % 2]; }
    VkImageView   getUVPlaneView(uint32_t idx) const { return uvPlaneViews_[idx % 2]; }

private:
    void createImage(uint32_t w, uint32_t h, VkSampleCountFlagBits samples, VkFormat format,
                    VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                    VkImage& image, VkDeviceMemory& memory, VkPhysicalDevice physDev)
    {
        VkVideoEncodeAV1ProfileInfoKHR av1Profile{};
        av1Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR;
        av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;

        VkVideoProfileInfoKHR videoProfile{};
        videoProfile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
        videoProfile.pNext = &av1Profile;
        videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;
        videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
        videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

        VkVideoProfileListInfoKHR profileList{};
        profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
        profileList.profileCount = 1;
        profileList.pProfiles = &videoProfile;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
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
            imageInfo.pNext = &profileList;
        }

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("OffscreenTarget: Failed to create VkImage.");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device_, image, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;

        allocInfo.memoryTypeIndex = 0;
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((memReq.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        }

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("OffscreenTarget: Failed to allocate image memory.");
        vkBindImageMemory(device_, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageViewUsageCreateInfo usageInfo{};
        usageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        bool restrictUsage = false;

        // 核心修复：权限精准剥离
        if (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR) {
            usageInfo.usage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            restrictUsage = true;
        } else if (aspectFlags == VK_IMAGE_ASPECT_PLANE_0_BIT || aspectFlags == VK_IMAGE_ASPECT_PLANE_1_BIT) {
            usageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            restrictUsage = true;
        }

        viewInfo.pNext = restrictUsage ? &usageInfo : nullptr;

        VkImageView imageView;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
            throw std::runtime_error("OffscreenTarget: Failed to create VkImageView.");
        return imageView;
    }

    VkDevice device_;
    uint32_t width_, height_;
    VkImage msaaColorImage_, msaaDepthImage_, resolveRGBImage_;
    VkImage yuvImages_[2];
    VkDeviceMemory msaaColorMemory_, msaaDepthMemory_, resolveRGBMemory_;
    VkDeviceMemory yuvMemories_[2];
    VkImageView msaaColorView_, msaaDepthView_, resolveRGBView_;
    VkImageView yuvViews_[2], yPlaneViews_[2], uvPlaneViews_[2];
    VkFramebuffer framebuffer_;
};

} // namespace StuCanvas::Vulkan