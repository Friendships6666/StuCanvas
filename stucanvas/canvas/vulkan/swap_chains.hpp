// stucanvas/canvas/vulkan/swap_chains.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <limits>

namespace StuCanvas::Vulkan {

/**
 * @brief 封装 Vulkan 交换链（SwapChain）及关联的屏幕呈现资源。
 * 负责管理 MSAA 颜色附件、深度附件、交换链图像视图以及最终的帧缓冲（Framebuffer）。
 */
class SwapChain {
public:
    /**
     * @brief 构造并创建交换链。
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
          graphicsFamily_(graphicsFamily), presentFamily_(presentFamily), msaaSamples_(msaaSamples),
          window_(window)
    {
        createSwapChain(oldSwapChain);
        createImageViews();
        createColorResources();
        createDepthResources();
        if (renderPass_ != VK_NULL_HANDLE) {
            createFramebuffers();
        }
    }

    ~SwapChain() {
        cleanup();
    }

    // 禁止拷贝，保证句柄生命周期安全
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    // 支持移动语义
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
          msaaSamples_(other.msaaSamples_),
          depthImage_(other.depthImage_),
          depthImageMemory_(other.depthImageMemory_),
          depthImageView_(other.depthImageView_),
          colorImage_(other.colorImage_),
          colorImageMemory_(other.colorImageMemory_),
          colorImageView_(other.colorImageView_)
    {
        other.swapChain_ = VK_NULL_HANDLE;
        other.depthImage_ = VK_NULL_HANDLE;
        other.depthImageMemory_ = VK_NULL_HANDLE;
        other.depthImageView_ = VK_NULL_HANDLE;
        other.colorImage_ = VK_NULL_HANDLE;
        other.colorImageMemory_ = VK_NULL_HANDLE;
        other.colorImageView_ = VK_NULL_HANDLE;
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
            msaaSamples_ = other.msaaSamples_;
            depthImage_ = other.depthImage_;
            depthImageMemory_ = other.depthImageMemory_;
            depthImageView_ = other.depthImageView_;
            colorImage_ = other.colorImage_;
            colorImageMemory_ = other.colorImageMemory_;
            colorImageView_ = other.colorImageView_;

            other.swapChain_ = VK_NULL_HANDLE;
            other.depthImage_ = VK_NULL_HANDLE;
            other.depthImageMemory_ = VK_NULL_HANDLE;
            other.depthImageView_ = VK_NULL_HANDLE;
            other.colorImage_ = VK_NULL_HANDLE;
            other.colorImageMemory_ = VK_NULL_HANDLE;
            other.colorImageView_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 访问器
    [[nodiscard]] VkSwapchainKHR getSwapChain()   const { return swapChain_; }
    [[nodiscard]] VkFormat       getImageFormat() const { return imageFormat_; }
    [[nodiscard]] VkExtent2D     getExtent()      const { return extent_; }
    [[nodiscard]] size_t         getImageCount()  const { return imageViews_.size(); }
    [[nodiscard]] VkImageView    getImageView(size_t i)  const { return imageViews_[i]; }
    [[nodiscard]] VkFramebuffer  getFramebuffer(size_t i) const { return framebuffers_[i]; }

    /**
     * @brief 响应窗口尺寸变化，重建交换链及相关呈现资源
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
    uint32_t         graphicsFamily_ = 0xFFFFFFFF;
    uint32_t         presentFamily_ = 0xFFFFFFFF;
    SDL_Window*      window_;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    // MSAA 深度资源
    VkImage        depthImage_       = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView    depthImageView_   = VK_NULL_HANDLE;

    // MSAA 颜色资源
    VkImage        colorImage_       = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory_ = VK_NULL_HANDLE;
    VkImageView    colorImageView_   = VK_NULL_HANDLE;

    // 交换链核心对象
    VkSwapchainKHR             swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage>       images_;
    VkFormat                   imageFormat_{};
    VkExtent2D                 extent_{};
    std::vector<VkImageView>   imageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport() const {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount = 0;
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
        throw std::runtime_error("SwapChain: Failed to find suitable memory type.");
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
        imageInfo.samples = numSamples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("SwapChain: Failed to create VkImage.");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("SwapChain: Failed to allocate image memory.");
        }

        vkBindImageMemory(device_, image, imageMemory, 0);
    }

    void createColorResources() {
        VkFormat colorFormat = imageFormat_;

        // 颜色多重采样附件仅作为 subpass 的过渡目标，使用 TRANSIENT 可获得显存带宽优化
        createImage(extent_.width, extent_.height, msaaSamples_, colorFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage_, colorImageMemory_);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &colorImageView_) != VK_SUCCESS) {
            throw std::runtime_error("SwapChain: Failed to create color MSAA image view.");
        }
    }

    void createDepthResources() {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        createImage(extent_.width, extent_.height, msaaSamples_, depthFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage_, depthImageMemory_);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
            throw std::runtime_error("SwapChain: Failed to create depth MSAA image view.");
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
            int width = 0, height = 0;
            if (window_) {
                SDL_GetWindowSize(window_, &width, &height);
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
            throw std::runtime_error("SwapChain: Failed to create VkSwapchainKHR.");
        }

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
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(device_, &createInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
                throw std::runtime_error("SwapChain: Failed to create Swapchain image view.");
            }
        }
    }

    void createFramebuffers() {
        framebuffers_.resize(imageViews_.size());
        for (size_t i = 0; i < imageViews_.size(); ++i) {
            std::array<VkImageView, 3> attachments = {
                colorImageView_,
                depthImageView_,
                imageViews_[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent_.width;
            framebufferInfo.height = extent_.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
                throw std::runtime_error("SwapChain: Failed to create Framebuffer.");
            }
        }
    }

    void cleanup() {
        if (device_ == VK_NULL_HANDLE) return;

        for (auto fb : framebuffers_) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        framebuffers_.clear();

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

        for (auto iv : imageViews_) {
            vkDestroyImageView(device_, iv, nullptr);
        }
        imageViews_.clear();

        if (swapChain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapChain_, nullptr);
            swapChain_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan