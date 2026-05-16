// stucanvas/canvas/vulkan/swap_chains.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>          // 用于 SDL_GetWindowSize
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <limits>

namespace StuCanvas::Vulkan {

/**
 * @brief 封装整个 Vulkan 交换链（SwapChain），包括图像视图、帧缓冲。
 *
 * 特点：
 * - 自适应窗口大小（通过 SDL_Window 查询当前帧缓冲区尺寸）
 * - 正确处理图形/呈现队列族索引，自动选择独占或并发模式
 * - 提供 recreate() 方法以响应窗口大小改变
 */
class SwapChain {
public:
    /**
     * @brief 构造并创建交换链及相关资源。
     * @param physicalDevice 物理设备句柄
     * @param device         逻辑设备句柄
     * @param surface        窗口表面
     * @param renderPass     渲染通道（用于帧缓冲）
     * @param graphicsFamily 图形队列族索引
     * @param presentFamily  呈现队列族索引
     * @param window         SDL 窗口，用于获取当前尺寸（自适应 extent）
     * @param oldSwapChain   可选，旧的交换链（重建时传入）
     */
    SwapChain(VkPhysicalDevice physicalDevice,
              VkDevice device,
              VkSurfaceKHR surface,
              VkRenderPass renderPass,
              uint32_t graphicsFamily,
              uint32_t presentFamily,
              SDL_Window* window,
              VkSampleCountFlagBits msaaSamples,
              VkSwapchainKHR oldSwapChain = VK_NULL_HANDLE)
        : physicalDevice_(physicalDevice), device_(device),
          surface_(surface), renderPass_(renderPass),
          graphicsFamily_(graphicsFamily), presentFamily_(presentFamily),msaaSamples_(msaaSamples),
          window_(window)
    {
        createSwapChain(oldSwapChain);
        createImageViews();
        createColorResources(); // 生成 colorImageView_ (Attachment 0)
        createDepthResources();
        if (renderPass_ != VK_NULL_HANDLE) {
            createFramebuffers();
        }
    }

    ~SwapChain() {
        cleanup();
    }

    // 禁止拷贝
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    // 支持移动
    SwapChain(SwapChain&& other) noexcept
        : physicalDevice_(other.physicalDevice_), device_(other.device_),
          surface_(other.surface_), renderPass_(other.renderPass_),
          graphicsFamily_(other.graphicsFamily_), presentFamily_(other.presentFamily_),
          window_(other.window_),
          swapChain_(other.swapChain_),
          images_(std::move(other.images_)),
          imageFormat_(other.imageFormat_),
          extent_(other.extent_),
          imageViews_(std::move(other.imageViews_)),
          framebuffers_(std::move(other.framebuffers_)),
    msaaSamples_(other.msaaSamples_)
    {
        other.swapChain_ = VK_NULL_HANDLE;
    }

    SwapChain& operator=(SwapChain&& other) noexcept {
        if (this != &other) {
            cleanup();
            physicalDevice_ = other.physicalDevice_;
            device_ = other.device_;
            surface_ = other.surface_;
            renderPass_ = other.renderPass_;
            graphicsFamily_ = other.graphicsFamily_;
            presentFamily_ = other.presentFamily_;
            window_ = other.window_;
            swapChain_ = other.swapChain_;
            images_ = std::move(other.images_);
            imageFormat_ = other.imageFormat_;
            extent_ = other.extent_;
            imageViews_ = std::move(other.imageViews_);
            framebuffers_ = std::move(other.framebuffers_);
            other.swapChain_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 访问器
    VkSwapchainKHR getSwapChain()   const { return swapChain_; }
    VkFormat       getImageFormat() const { return imageFormat_; }
    VkExtent2D     getExtent()      const { return extent_; }
    size_t         getImageCount()  const { return imageViews_.size(); }
    VkImageView    getImageView(size_t i)  const { return imageViews_[i]; }
    VkFramebuffer  getFramebuffer(size_t i) const { return framebuffers_[i]; }

    /**
     * @brief 重建交换链（例如窗口大小改变后调用）。
     *        自动查询当前窗口尺寸，销毁旧资源并构建新链。
     */
    void recreate() {



        vkDeviceWaitIdle(device_);

        cleanup();
        createSwapChain(VK_NULL_HANDLE);
        createImageViews();
        createColorResources();
        createDepthResources();

        if (renderPass_ != VK_NULL_HANDLE) {
            createFramebuffers();
        }
    }

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice         device_;
    VkSurfaceKHR     surface_;
    VkRenderPass     renderPass_;
    uint32_t         graphicsFamily_{};
    uint32_t         presentFamily_{};
    SDL_Window*      window_;
    VkSampleCountFlagBits msaaSamples_{};


    VkImage        depthImage_       = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView    depthImageView_   = VK_NULL_HANDLE;


    // MSAA 颜色缓冲区资源（新增）
    VkImage        colorImage_       = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory_ = VK_NULL_HANDLE;
    VkImageView    colorImageView_   = VK_NULL_HANDLE;



    VkSwapchainKHR   swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    VkFormat         imageFormat_{};
    VkExtent2D       extent_{};

    std::vector<VkImageView>   imageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    // ---------- 内部辅助函数 ----------

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport() const {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, details.presentModes.data());
        }
        return details;
    }


    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type!");
    }
    void createImage(uint32_t width, uint32_t height, VkSampleCountFlagBits numSamples,
                 VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = numSamples; // 关键参数
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(device_, image, imageMemory, 0);
    }

    void createColorResources() {
        VkFormat colorFormat = imageFormat_; // 与交换链格式一致

        createImage(extent_.width, extent_.height, msaaSamples_, colorFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    // 注意：由于这张图只在 subpass 内部使用，标记为 TRANSIENT 可在移动端等设备大幅省电
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage_, colorImageMemory_);

        // 创建 View
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device_, &viewInfo, nullptr, &colorImageView_);
    }

void createDepthResources() {
        // 深度格式通常使用 D32_SFLOAT，确保 RenderPass 中也是这个格式
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        // 1. 创建支持 MSAA 的深度 Image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent_.width;
        imageInfo.extent.height = extent_.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;


        imageInfo.samples = msaaSamples_;

        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth image!");
        }

        // 2. 为深度图分配并绑定显存
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_, depthImage_, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;

        // 深度图通常存储在 DEVICE_LOCAL（显存）中以获得最高读写性能
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &depthImageMemory_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate depth image memory!");
        }

        vkBindImageMemory(device_, depthImage_, depthImageMemory_, 0);

        // 3. 创建深度图像视图 (ImageView)
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;

        // 设置 AspectMask 为 DEPTH 位
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth image view!");
        }
    }




    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const {
        for (const auto& fmt : available) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return fmt;
            }
        }
        return available[0];
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& available) const {
        for (const auto& mode : available) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            // 从 SDL 窗口获取当前帧缓冲区大小
            int width = 0, height = 0;
            if (window_) {
                SDL_GetWindowSize(window_, &width, &height);  // 实际应使用 SDL_GetWindowSizeInPixels 以获得像素尺寸
                // SDL3 API：SDL_GetWindowSizeInPixels(window, &width, &height);
            }
            if (width <= 0) width = 800;
            if (height <= 0) height = 600;

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };
            actualExtent.width = std::clamp(actualExtent.width,
                                            capabilities.minImageExtent.width,
                                            capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height,
                                             capabilities.minImageExtent.height,
                                             capabilities.maxImageExtent.height);
            return actualExtent;
        }
    }

    void createSwapChain(VkSwapchainKHR oldSwapChain) {
        auto support = querySwapChainSupport();

        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        VkPresentModeKHR   presentMode   = choosePresentMode(support.presentModes);
        VkExtent2D          extent        = chooseExtent(support.capabilities);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = surface_;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat.format;
        createInfo.imageColorSpace  = surfaceFormat.colorSpace;
        createInfo.imageExtent      = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // 队列族配置
        uint32_t queueFamilyIndices[] = { graphicsFamily_, presentFamily_ };
        if (graphicsFamily_ != presentFamily_) {
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices   = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices   = nullptr;
        }

        createInfo.preTransform   = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    = presentMode;
        createInfo.clipped        = VK_TRUE;
        createInfo.oldSwapchain   = oldSwapChain;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain");
        }

        // 获取交换链图像
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
        images_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, images_.data());

        imageFormat_ = surfaceFormat.format;
        extent_      = extent;
    }

    void createImageViews() {
        imageViews_.resize(images_.size());
        for (size_t i = 0; i < images_.size(); ++i) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image                           = images_[i];
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = imageFormat_;
            createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(device_, &createInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    void createFramebuffers() {
        framebuffers_.resize(imageViews_.size());
        for (size_t i = 0; i < imageViews_.size(); ++i) {
            // 顺序必须与 RenderPass 的 Attachment 对应：
            // 0: MSAA Color (目标)
            // 1: MSAA Depth (测试)
            // 2: Swapchain Image (Resolve 目标)
            std::array<VkImageView, 3> attachments = {
                colorImageView_,
                depthImageView_,
                imageViews_[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size()); // 3
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent_.width;
            framebufferInfo.height = extent_.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }

    void cleanup() {
        if (device_ == VK_NULL_HANDLE) return;

        // 1. 销毁帧缓冲
        for (auto fb : framebuffers_) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        framebuffers_.clear();

        // 2. 销毁 MSAA 颜色资源
        if (colorImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, colorImageView_, nullptr);
            colorImageView_ = VK_NULL_HANDLE;
        }
        if (colorImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, colorImage_, nullptr);
            colorImage_ = VK_NULL_HANDLE;
        }
        if (colorImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, colorImageMemory_, nullptr);
            colorImageMemory_ = VK_NULL_HANDLE;
        }

        // 3. 销毁 MSAA 深度资源
        if (depthImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, depthImageView_, nullptr);
            depthImageView_ = VK_NULL_HANDLE;
        }
        if (depthImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, depthImage_, nullptr);
            depthImage_ = VK_NULL_HANDLE;
        }
        if (depthImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, depthImageMemory_, nullptr);
            depthImageMemory_ = VK_NULL_HANDLE;
        }

        // 4. 销毁交换链图像视图
        for (auto iv : imageViews_) {
            vkDestroyImageView(device_, iv, nullptr);
        }
        imageViews_.clear();

        // 5. 销毁交换链本身
        if (swapChain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapChain_, nullptr);
            swapChain_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan