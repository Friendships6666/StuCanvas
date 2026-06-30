// stucanvas/canvas/vulkan/rt_present.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace StuCanvas::Vulkan {

/**
 * @brief 图像布局转换辅助函数
 */
inline void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    } else {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

/**
 * @brief 光追专属轻量级呈现器 (RTPresenter)
 */
class RTPresenter {
public:
    RTPresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
                uint32_t graphicsFamily, uint32_t presentFamily,
                VkQueue graphicsQueue, VkQueue presentQueue, SDL_Window* window)
        : physicalDevice_(physicalDevice), device_(device), surface_(surface),
          graphicsFamily_(graphicsFamily), presentFamily_(presentFamily),
          graphicsQueue_(graphicsQueue), presentQueue_(presentQueue), window_(window)
    {
        createSwapChain();
        createCommandPool();
        createSyncObjects();
        createCommandBuffers();
    }

    ~RTPresenter() {
        cleanup();
    }

    RTPresenter(const RTPresenter&) = delete;
    RTPresenter& operator=(const RTPresenter&) = delete;

    VkCommandBuffer beginFrame(uint32_t& imageIndex) {
        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(
            device_, swapChain_, UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return VK_NULL_HANDLE;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("RTPresenter: Failed to acquire swap chain image.");
        }

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to begin command buffer.");
        }

        return cmd;
    }

    void endFrame(VkCommandBuffer cmd, uint32_t imageIndex, VkImage srcStorageImage, VkExtent2D srcExtent) {
        // 1. 离屏 Storage 转换为 Transfer Source
        transitionImageLayout(cmd, srcStorageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // 2. 交换链接收 Image 转换为 Transfer Destination
        transitionImageLayout(cmd, swapChainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // 3. 执行 Blit 复制拉伸
        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = 0;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { static_cast<int32_t>(extent_.width), static_cast<int32_t>(extent_.height), 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = 0;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd, srcStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapChainImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        // 4. 恢复布局
        transitionImageLayout(cmd, swapChainImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        transitionImageLayout(cmd, srcStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to record command buffer.");
        }

        // 5. 提交执行与呈现
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphores_[currentFrame_] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        // ─────────────────────────────────────────────────────────────
        // 修复：提交的信号量索引与当前分配给它的 Swapchain Image 完全一致，彻底杜绝重用错误
        // ─────────────────────────────────────────────────────────────
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores_[imageIndex] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to submit command buffer.");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { swapChain_ };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized_) {
            resized_ = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to present swap chain image.");
        }

        currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void recreateSwapChain() {
        vkDeviceWaitIdle(device_);
        cleanupSwapChainOnly();
        createSwapChain();

        // 重新分配以匹配可能发生变化的交换链图片数
        recreateSyncObjects();
    }

    void markResized() { resized_ = true; }
    [[nodiscard]] VkExtent2D getExtent() const { return extent_; }

private:
    VkPhysicalDevice physicalDevice_;
    VkDevice         device_;
    VkSurfaceKHR     surface_;
    uint32_t         graphicsFamily_;
    uint32_t         presentFamily_;
    VkQueue          graphicsQueue_;
    VkQueue          presentQueue_;
    SDL_Window*      window_;

    VkSwapchainKHR           swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage>     swapChainImages_;
    VkFormat                 imageFormat_{};
    VkExtent2D               extent_{};

    const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkFence>     inFlightFences_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;

    VkCommandPool                commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    size_t currentFrame_ = 0;
    bool resized_ = false;

    void createSwapChain() {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = fmt;
                break;
            }
        }

        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent_ = capabilities.currentExtent;
        } else {
            int w = 0, h = 0;
            SDL_GetWindowSize(window_, &w, &h);
            extent_ = {
                std::clamp(static_cast<uint32_t>(w), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(static_cast<uint32_t>(h), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
            };
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        uint32_t queueFamilyIndices[] = { graphicsFamily_, presentFamily_ };
        if (graphicsFamily_ != presentFamily_) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to create swap chain.");
        }

        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
        swapChainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());

        imageFormat_ = surfaceFormat.format;
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily_;
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
            throw std::runtime_error("RTPresenter: Failed to create command pool.");
        }
    }

    void createSyncObjects() {
        uint32_t imageCount = static_cast<uint32_t>(swapChainImages_.size());

        imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

        // ─────────────────────────────────────────────────────────────
        // 修复：物理同步信号量严格与交换链的物理 Image 数量挂钩，彻底排除资源竞争
        // ─────────────────────────────────────────────────────────────
        renderFinishedSemaphores_.resize(imageCount);

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]);
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]);
        }

        for (uint32_t i = 0; i < imageCount; ++i) {
            vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]);
        }
    }

    void recreateSyncObjects() {
        for (auto& sem : renderFinishedSemaphores_) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
        }
        renderFinishedSemaphores_.clear();

        uint32_t imageCount = static_cast<uint32_t>(swapChainImages_.size());
        renderFinishedSemaphores_.resize(imageCount);

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (uint32_t i = 0; i < imageCount; ++i) {
            vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]);
        }
    }

    void createCommandBuffers() {
        commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data());
    }

    void cleanupSwapChainOnly() {
        if (swapChain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapChain_, nullptr);
            swapChain_ = VK_NULL_HANDLE;
        }
    }

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            cleanupSwapChainOnly();

            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
                vkDestroyFence(device_, inFlightFences_[i], nullptr);
            }
            for (auto& sem : renderFinishedSemaphores_) {
                if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
            }
            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            }
            device_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan