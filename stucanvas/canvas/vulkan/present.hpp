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
 *
 * 使用方法：
 *   1. 创建 Presenter 并传入必要参数
 *   2. 循环中调用 beginFrame 获取命令缓冲区和图像索引
 *   3. 使用返回的命令缓冲区录制绘制命令（需要自己开始渲染通道）
 *   4. 调用 endFrame 提交并呈现
 */
class Presenter {
public:
    /**
     * @brief 构造呈现器，同时创建交换链、同步对象和命令缓冲区。
     *
     * @param physicalDevice 物理设备
     * @param device         逻辑设备
     * @param surface        窗口表面
     * @param graphicsFamily 图形队列族索引
     * @param presentFamily  呈现队列族索引
     * @param graphicsQueue  图形队列
     * @param presentQueue   呈现队列
     * @param renderPass     渲染通道（用于帧缓冲）
     * @param window          SDL 窗口，用于获取当前尺寸
     */
    Presenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
              uint32_t graphicsFamily, uint32_t presentFamily,
              VkQueue graphicsQueue, VkQueue presentQueue,
              VkRenderPass renderPass, SDL_Window* window,VkSampleCountFlagBits msaaSamples);

    ~Presenter();

    // 禁止拷贝
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    /**
     * @brief 开始一帧，获取可用的命令缓冲区和交换链图像索引。
     *
     * 内部会等待上一帧的栅栏，从交换链获取图像，重置命令缓冲区并开始录制。
     *
     * @param imageIndex 输出参数：将渲染的交换链图像索引
     * @return VkCommandBuffer 已开始录制的命令缓冲区；若交换链已过期则返回 VK_NULL_HANDLE
     */
    VkCommandBuffer beginFrame(uint32_t& imageIndex);

    /**
     * @brief 结束一帧，提交命令缓冲区并呈现。
     *
     * @param commandBuffer 由 beginFrame 返回的已录制命令缓冲区
     * @param imageIndex    由 beginFrame 返回的图像索引
     */
    void endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // 访问器
    size_t        getImageCount()                    const;
    VkFramebuffer getFramebuffer(size_t imageIndex)  const;
    VkExtent2D    getExtent()                        const;
    VkImageView   getImageView(size_t imageIndex)    const;

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
    uint32_t         graphicsFamily_, presentFamily_;
    VkQueue          graphicsQueue_, presentQueue_;
    VkRenderPass     renderPass_;
    SDL_Window*      window_;
    VkSampleCountFlagBits msaaSamples_;

    std::unique_ptr<SwapChain> swapChain_;

    const int MAX_FRAMES_IN_FLIGHT = 2;

    std::vector<VkFence>     inFlightFences_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;

    VkCommandPool                commandPool_{};
    std::vector<VkCommandBuffer> commandBuffers_;

    size_t currentFrame_ = 0;
};

// ─────────────────────────────────────────────────────────────
// 实现（可放在 .cpp 文件中，此处为便于阅读放在头文件内）
// ─────────────────────────────────────────────────────────────

inline Presenter::Presenter(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkSurfaceKHR surface, uint32_t graphicsFamily,
                            uint32_t presentFamily, VkQueue graphicsQueue,
                            VkQueue presentQueue, VkRenderPass renderPass,
                            SDL_Window* window,VkSampleCountFlagBits msaaSamples_)
    : physicalDevice_(physicalDevice), device_(device), surface_(surface),
      graphicsFamily_(graphicsFamily), presentFamily_(presentFamily),
      graphicsQueue_(graphicsQueue), presentQueue_(presentQueue),
      renderPass_(renderPass), window_(window),msaaSamples_(msaaSamples_)
{
    swapChain_ = std::make_unique<SwapChain>(
        physicalDevice_, device_, surface_, renderPass_,
        graphicsFamily_, presentFamily_, window_,msaaSamples_);

    createCommandPool();
    createSyncObjects();
    createCommandBuffers();
}

inline Presenter::~Presenter() {
    vkDeviceWaitIdle(device_);
    cleanupSwapChainResources();
    vkDestroyCommandPool(device_, commandPool_, nullptr);
}

inline void Presenter::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

    inline void Presenter::createSyncObjects() {
    uint32_t swapchainImageCount = static_cast<uint32_t>(swapChain_->getImageCount());

    // 这些只需要 MAX_FRAMES_IN_FLIGHT 个
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    // 这个必须和交换链图像数量一致
    renderFinishedSemaphores_.resize(swapchainImageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // 创建 per-frame 的信号量和栅栏
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image available semaphore");
        if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create in-flight fence");
    }

    // 创建 per‑swapchain‑image 的渲染完成信号量
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create render finished semaphore");
    }
}

inline void Presenter::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers");
}

inline VkCommandBuffer Presenter::beginFrame(uint32_t& imageIndex) {
    // 等待上一帧完成
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    // 获取交换链图像
    VkResult result = vkAcquireNextImageKHR(
        device_, swapChain_->getSwapChain(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swap chain image");

    // 重置围栏
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    // 重置并开始命令缓冲区
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer");

    return cmd;
}

// 找到 stucanvas/canvas/vulkan/present.hpp 中的 endFrame 实现

inline void Presenter::endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores_[currentFrame_] };

    // --- 【关键修改点：增加深度测试等待阶段】 ---
    // 我们不仅要等待颜色输出，还要确保在图像准备好之前，不执行深度测试相关的写入
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    };
    // ----------------------------------------

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores_[imageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

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
        throw std::runtime_error("Failed to present swap chain image");
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

    inline void Presenter::recreateSwapChain() {
    vkDeviceWaitIdle(device_);

    // 1. 释放旧命令缓冲区
    vkFreeCommandBuffers(device_, commandPool_,
                         static_cast<uint32_t>(commandBuffers_.size()),
                         commandBuffers_.data());

    // 2. 销毁旧同步对象
    for (auto& sem : imageAvailableSemaphores_) vkDestroySemaphore(device_, sem, nullptr);
    for (auto& sem : renderFinishedSemaphores_) vkDestroySemaphore(device_, sem, nullptr);
    for (auto& fen : inFlightFences_)         vkDestroyFence(device_, fen, nullptr);

    // 3. 重建交换链本身（内部会销毁并重建图像、视图、帧缓冲）
    swapChain_->recreate();

    // 4. 根据新的交换链图像数量，重新创建同步对象
    createSyncObjects();

    // 5. 重新分配命令缓冲区
    createCommandBuffers();
}

    inline void Presenter::cleanupSwapChainResources() {
    if (device_ == VK_NULL_HANDLE) return;

    vkFreeCommandBuffers(device_, commandPool_,
                         static_cast<uint32_t>(commandBuffers_.size()),
                         commandBuffers_.data());

    for (auto& sem : imageAvailableSemaphores_) vkDestroySemaphore(device_, sem, nullptr);
    for (auto& sem : renderFinishedSemaphores_) vkDestroySemaphore(device_, sem, nullptr);
    for (auto& fen : inFlightFences_)         vkDestroyFence(device_, fen, nullptr);

    swapChain_.reset();
}

inline size_t        Presenter::getImageCount()              const { return swapChain_->getImageCount(); }
inline VkFramebuffer Presenter::getFramebuffer(size_t index) const { return swapChain_->getFramebuffer(index); }
inline VkExtent2D    Presenter::getExtent()                  const { return swapChain_->getExtent(); }
inline VkImageView   Presenter::getImageView(size_t index)   const { return swapChain_->getImageView(index); }

inline void Presenter::markResized() {
    recreateSwapChain();
}

} // namespace StuCanvas::Vulkan