// stucanvas/canvas/vulkan/offscreen_target.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>
#include <array>
#include <iostream>

namespace StuCanvas::Vulkan {

/**
 * @brief 离屏渲染目标管理类 (RAII)
 * 负责离屏帧缓冲的分配，包括多重采样(MSAA)颜色、深度、单采样解析(Resolve) RGB 图像，
 * 已经用于格式转换的双缓冲 YUV (NV12) 物理图像。
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
        // 1. MSAA 颜色附件 (RGBA8)
        createImage(width, height, msaaSamples, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaColorImage_, msaaColorMemory_, physDev);

        msaaColorView_ = createImageView(msaaColorImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_ASPECT_COLOR_BIT,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);

        // 2. MSAA 深度附件 (D32)
        createImage(width, height, msaaSamples, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    msaaDepthImage_, msaaDepthMemory_, physDev);

        msaaDepthView_ = createImageView(msaaDepthImage_, VK_FORMAT_D32_SFLOAT,
                                         VK_IMAGE_ASPECT_DEPTH_BIT,
                                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

        // 3. Resolve RGB 图像 (RGBA8, 单重采样) - 供转码 Compute Shader 进行纹理采样
        createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    resolveRGBImage_, resolveRGBMemory_, physDev);

        resolveRGBView_ = createImageView(resolveRGBImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_ASPECT_COLOR_BIT,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        // 4. 双缓冲 YUV (NV12) 图像组 - 供转码转换
        for (int i = 0; i < 2; ++i) {
            VkImageUsageFlags yuvImageUsage = VK_IMAGE_USAGE_STORAGE_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            createImage(width, height, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                        VK_IMAGE_TILING_OPTIMAL, yuvImageUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        yuvImages_[i], yuvMemories_[i], physDev);

            // A. 整体多平面视图：移除其存储(STORAGE)权限
            VkImageUsageFlags wholeViewUsage = yuvImageUsage & ~VK_IMAGE_USAGE_STORAGE_BIT;
            yuvViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
                                           VK_IMAGE_ASPECT_COLOR_BIT, wholeViewUsage);

            // B. Y 平面子视图 (R8)：支持 STORAGE 写入
            yPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8_UNORM,
                                              VK_IMAGE_ASPECT_PLANE_0_BIT, yuvImageUsage);

            // C. UV 平面子视图 (R8G8)：支持 STORAGE 写入
            uvPlaneViews_[i] = createImageView(yuvImages_[i], VK_FORMAT_R8G8_UNORM,
                                               VK_IMAGE_ASPECT_PLANE_1_BIT, yuvImageUsage);
        }

        // 5. 创建离屏 Framebuffer
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
            cleanup();
            throw std::runtime_error("OffscreenTarget: Failed to create framebuffer.");
        }
    }

    ~OffscreenTarget() {
        cleanup();
    }

    // 禁止拷贝
    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    // 允许移动语义，保证资源流转安全
    OffscreenTarget(OffscreenTarget&& other) noexcept
        : device_(other.device_), width_(other.width_), height_(other.height_),
          msaaColorImage_(other.msaaColorImage_), msaaDepthImage_(other.msaaDepthImage_),
          resolveRGBImage_(other.resolveRGBImage_), msaaColorMemory_(other.msaaColorMemory_),
          msaaDepthMemory_(other.msaaDepthMemory_), resolveRGBMemory_(other.resolveRGBMemory_),
          msaaColorView_(other.msaaColorView_), msaaDepthView_(other.msaaDepthView_),
          resolveRGBView_(other.resolveRGBView_), framebuffer_(other.framebuffer_)
    {
        for (int i = 0; i < 2; ++i) {
            yuvImages_[i] = other.yuvImages_[i];
            yuvMemories_[i] = other.yuvMemories_[i];
            yuvViews_[i] = other.yuvViews_[i];
            yPlaneViews_[i] = other.yPlaneViews_[i];
            uvPlaneViews_[i] = other.uvPlaneViews_[i];

            other.yuvImages_[i] = VK_NULL_HANDLE;
            other.yuvMemories_[i] = VK_NULL_HANDLE;
            other.yuvViews_[i] = VK_NULL_HANDLE;
            other.yPlaneViews_[i] = VK_NULL_HANDLE;
            other.uvPlaneViews_[i] = VK_NULL_HANDLE;
        }

        other.msaaColorImage_ = VK_NULL_HANDLE;
        other.msaaDepthImage_ = VK_NULL_HANDLE;
        other.resolveRGBImage_ = VK_NULL_HANDLE;
        other.msaaColorMemory_ = VK_NULL_HANDLE;
        other.msaaDepthMemory_ = VK_NULL_HANDLE;
        other.resolveRGBMemory_ = VK_NULL_HANDLE;
        other.msaaColorView_ = VK_NULL_HANDLE;
        other.msaaDepthView_ = VK_NULL_HANDLE;
        other.resolveRGBView_ = VK_NULL_HANDLE;
        other.framebuffer_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    OffscreenTarget& operator=(OffscreenTarget&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            width_ = other.width_;
            height_ = other.height_;
            msaaColorImage_ = other.msaaColorImage_;
            msaaDepthImage_ = other.msaaDepthImage_;
            resolveRGBImage_ = other.resolveRGBImage_;
            msaaColorMemory_ = other.msaaColorMemory_;
            msaaDepthMemory_ = other.msaaDepthMemory_;
            resolveRGBMemory_ = other.resolveRGBMemory_;
            msaaColorView_ = other.msaaColorView_;
            msaaDepthView_ = other.msaaDepthView_;
            resolveRGBView_ = other.resolveRGBView_;
            framebuffer_ = other.framebuffer_;

            for (int i = 0; i < 2; ++i) {
                yuvImages_[i] = other.yuvImages_[i];
                yuvMemories_[i] = other.yuvMemories_[i];
                yuvViews_[i] = other.yuvViews_[i];
                yPlaneViews_[i] = other.yPlaneViews_[i];
                uvPlaneViews_[i] = other.uvPlaneViews_[i];

                other.yuvImages_[i] = VK_NULL_HANDLE;
                other.yuvMemories_[i] = VK_NULL_HANDLE;
                other.yuvViews_[i] = VK_NULL_HANDLE;
                other.yPlaneViews_[i] = VK_NULL_HANDLE;
                other.uvPlaneViews_[i] = VK_NULL_HANDLE;
            }

            other.msaaColorImage_ = VK_NULL_HANDLE;
            other.msaaDepthImage_ = VK_NULL_HANDLE;
            other.resolveRGBImage_ = VK_NULL_HANDLE;
            other.msaaColorMemory_ = VK_NULL_HANDLE;
            other.msaaDepthMemory_ = VK_NULL_HANDLE;
            other.resolveRGBMemory_ = VK_NULL_HANDLE;
            other.msaaColorView_ = VK_NULL_HANDLE;
            other.msaaDepthView_ = VK_NULL_HANDLE;
            other.resolveRGBView_ = VK_NULL_HANDLE;
            other.framebuffer_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 获取接口
    [[nodiscard]] VkFramebuffer getFramebuffer() const { return framebuffer_; }
    [[nodiscard]] VkImage       getResolveRGBImage() const { return resolveRGBImage_; }
    [[nodiscard]] VkImageView   getResolveRGBView() const { return resolveRGBView_; }
    [[nodiscard]] VkImage       getYuvImage(uint32_t idx) const { return yuvImages_[idx % 2]; }
    [[nodiscard]] VkImageView   getYuvView(uint32_t idx) const { return yuvViews_[idx % 2]; }
    [[nodiscard]] VkImageView   getYPlaneView(uint32_t idx) const { return yPlaneViews_[idx % 2]; }
    [[nodiscard]] VkImageView   getUVPlaneView(uint32_t idx) const { return uvPlaneViews_[idx % 2]; }

private:
    void createImage(uint32_t w, uint32_t h, VkSampleCountFlagBits samples, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                     VkImage& image, VkDeviceMemory& memory, VkPhysicalDevice physDev)
    {
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
        if (memoryTypeIndex == 0xFFFFFFFF) throw std::runtime_error("OffscreenTarget: Failed to find memory type.");

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            image = VK_NULL_HANDLE;
            throw std::runtime_error("OffscreenTarget: Failed to allocate memory.");
        }

        vkBindImageMemory(device_, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageUsageFlags usage) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { aspectFlags, 0, 1, 0, 1 };

        VkImageViewUsageCreateInfo usageInfo{};
        usageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        usageInfo.usage = usage;
        viewInfo.pNext = &usageInfo;

        VkImageView imageView;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("OffscreenTarget: Failed to create VkImageView.");
        }
        return imageView;
    }

    void cleanup() {
        if (device_ == VK_NULL_HANDLE) return;

        if (framebuffer_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
        }

        for (int i = 0; i < 2; ++i) {
            if (yuvViews_[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, yuvViews_[i], nullptr);
                yuvViews_[i] = VK_NULL_HANDLE;
            }
            if (yPlaneViews_[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, yPlaneViews_[i], nullptr);
                yPlaneViews_[i] = VK_NULL_HANDLE;
            }
            if (uvPlaneViews_[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, uvPlaneViews_[i], nullptr);
                uvPlaneViews_[i] = VK_NULL_HANDLE;
            }
            if (yuvImages_[i] != VK_NULL_HANDLE) {
                vkDestroyImage(device_, yuvImages_[i], nullptr);
                yuvImages_[i] = VK_NULL_HANDLE;
            }
            if (yuvMemories_[i] != VK_NULL_HANDLE) {
                vkFreeMemory(device_, yuvMemories_[i], nullptr);
                yuvMemories_[i] = VK_NULL_HANDLE;
            }
        }

        if (resolveRGBView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, resolveRGBView_, nullptr);
            resolveRGBView_ = VK_NULL_HANDLE;
        }
        if (resolveRGBImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, resolveRGBImage_, nullptr);
            resolveRGBImage_ = VK_NULL_HANDLE;
        }
        if (resolveRGBMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, resolveRGBMemory_, nullptr);
            resolveRGBMemory_ = VK_NULL_HANDLE;
        }

        if (msaaDepthView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, msaaDepthView_, nullptr);
            msaaDepthView_ = VK_NULL_HANDLE;
        }
        if (msaaDepthImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, msaaDepthImage_, nullptr);
            msaaDepthImage_ = VK_NULL_HANDLE;
        }
        if (msaaDepthMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, msaaDepthMemory_, nullptr);
            msaaDepthMemory_ = VK_NULL_HANDLE;
        }

        if (msaaColorView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, msaaColorView_, nullptr);
            msaaColorView_ = VK_NULL_HANDLE;
        }
        if (msaaColorImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, msaaColorImage_, nullptr);
            msaaColorImage_ = VK_NULL_HANDLE;
        }
        if (msaaColorMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, msaaColorMemory_, nullptr);
            msaaColorMemory_ = VK_NULL_HANDLE;
        }

        device_ = VK_NULL_HANDLE;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;
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