// stucanvas/canvas/vulkan/present.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <memory>

#include "swap_chains.hpp"

namespace StuCanvas::Vulkan {

/**
 * @brief 呈现器：管理帧循环、命令缓冲区、同步原语，完成最终的图像呈现。
 *
 * 负责：
 * - 创建交换链（内部持有 SwapChain 对象）
 * - 分配命令池和每帧的命令缓冲区
 * - 创建同步对象（信号量、栅栏）
 * - 提供 beginFrame/endFrame 接口，简化渲染循环
 * - 自动检测窗口大小变化并重建交换链
 */
class Presenter {
public:
    /**
     * @brief 构造呈现器，同时创建交换链、同步对象和命令缓冲区。
     */
    Presenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
              uint32_t graphicsFamily, uint32_t presentFamily,
              VkQueue graphicsQueue, VkQueue presentQueue,
              VkRenderPass renderPass, SDL_Window* window, VkSampleCountFlagBits msaaSamples);

    ~Presenter();

    // 禁止拷贝
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    /**
     * @brief 开始一帧，获取可用的命令缓冲区和交换链图像索引。
     * @param imageIndex 输出参数：将渲染的交换链图像索引
     * @return VkCommandBuffer 已开始录制的命令缓冲区；若交换链已过期则返回 VK_NULL_HANDLE
     */
    VkCommandBuffer beginFrame(uint32_t& imageIndex);

    /**
     * @brief 结束一帧，提交命令缓冲区并呈现。
     * @param commandBuffer 由 beginFrame 返回的已录制命令缓冲区
     * @param imageIndex    由 beginFrame 返回的图像索引
     */
    void endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // 访问器
    [[nodiscard]] size_t        getImageCount()                    const;
    [[nodiscard]] VkFramebuffer getFramebuffer(size_t imageIndex)  const;
    [[nodiscard]] VkExtent2D    getExtent()                        const;
    [[nodiscard]] VkImageView   getImageView(size_t imageIndex)    const;

    /**
     * @brief 手动触发交换链重建（外部检测到窗口大小改变时调用）。
     */
    void markResized();

private:
    void createCommandPool();
    void createSyncObjects();
    void createCommandBuffers();
    void recreateSwapChain();
    void cleanupSwapChainResources();

    VkPhysicalDevice physicalDevice_;
    VkDevice         device_;
    VkSurfaceKHR     surface_;
    uint32_t         graphicsFamily_ = 0xFFFFFFFF;
    uint32_t         presentFamily_ = 0xFFFFFFFF;
    VkQueue          graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue          presentQueue_ = VK_NULL_HANDLE;
    VkRenderPass     renderPass_;
    SDL_Window*      window_;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    std::unique_ptr<SwapChain> swapChain_;

    const int MAX_FRAMES_IN_FLIGHT = 2;

    std::vector<VkFence>     inFlightFences_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;

    VkCommandPool                commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    size_t currentFrame_ = 0;
};

// ─────────────────────────────────────────────────────────────
// 内联实现
// ─────────────────────────────────────────────────────────────

inline Presenter::Presenter(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkSurfaceKHR surface, uint32_t graphicsFamily,
                            uint32_t presentFamily, VkQueue graphicsQueue,
                            VkQueue presentQueue, VkRenderPass renderPass,
                            SDL_Window* window, VkSampleCountFlagBits msaaSamples_)
    : physicalDevice_(physicalDevice), device_(device), surface_(surface),
      graphicsFamily_(graphicsFamily), presentFamily_(presentFamily),
      graphicsQueue_(graphicsQueue), presentQueue_(presentQueue),
      renderPass_(renderPass), window_(window), msaaSamples_(msaaSamples_)
{
    swapChain_ = std::make_unique<SwapChain>(
        physicalDevice_, device_, surface_, renderPass_,
        graphicsFamily_, presentFamily_, window_, msaaSamples_);

    createCommandPool();
    createSyncObjects();
    createCommandBuffers();
}

inline Presenter::~Presenter() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        cleanupSwapChainResources();
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
    }
}

inline void Presenter::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        throw std::runtime_error("Presenter: Failed to create command pool.");
}

inline void Presenter::createSyncObjects() {
    uint32_t swapchainImageCount = static_cast<uint32_t>(swapChain_->getImageCount());

    // 确保清理多余的大小配置
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    renderFinishedSemaphores_.resize(swapchainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS)
            throw std::runtime_error("Presenter: Failed to create image available semaphore.");
        if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("Presenter: Failed to create in-flight fence.");
    }

    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS)
            throw std::runtime_error("Presenter: Failed to create render finished semaphore.");
    }
}

inline void Presenter::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("Presenter: Failed to allocate command buffers.");
}

inline VkCommandBuffer Presenter::beginFrame(uint32_t& imageIndex) {
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        device_, swapChain_->getSwapChain(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Presenter: Failed to acquire swap chain image.");

    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Presenter: Failed to begin command buffer.");

    return cmd;
}

inline void Presenter::endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Presenter: Failed to record command buffer.");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores_[currentFrame_] };

    // 同步等待早期的深度测试与颜色混合输出
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    };

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores_[imageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        throw std::runtime_error("Presenter: Failed to submit draw command buffer.");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain_->getSwapChain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Presenter: Failed to present swap chain image.");
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

inline void Presenter::recreateSwapChain() {
    vkDeviceWaitIdle(device_);
    cleanupSwapChainResources();

    // 重新实例化交换链和同步结构
    swapChain_->recreate();
    createSyncObjects();
    createCommandBuffers();
}

inline void Presenter::cleanupSwapChainResources() {
    if (device_ == VK_NULL_HANDLE) return;

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(device_, commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    for (auto& sem : imageAvailableSemaphores_) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
    }
    imageAvailableSemaphores_.clear();

    for (auto& sem : renderFinishedSemaphores_) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
    }
    renderFinishedSemaphores_.clear();

    for (auto& fen : inFlightFences_) {
        if (fen != VK_NULL_HANDLE) vkDestroyFence(device_, fen, nullptr);
    }
    inFlightFences_.clear();
}

inline size_t        Presenter::getImageCount()              const { return swapChain_->getImageCount(); }
inline VkFramebuffer Presenter::getFramebuffer(size_t index) const { return swapChain_->getFramebuffer(index); }
inline VkExtent2D    Presenter::getExtent()                  const { return swapChain_->getExtent(); }
inline VkImageView   Presenter::getImageView(size_t index)   const { return swapChain_->getImageView(index); }

inline void Presenter::markResized() {
    recreateSwapChain();
}

} // namespace StuCanvas::Vulkan
