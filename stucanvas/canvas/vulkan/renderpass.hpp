// stucanvas/canvas/vulkan/renderpass.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <array>
#include "vk_ctx.hpp" // 引入新上下文以实现无缝对接

namespace StuCanvas::Vulkan {

/**
 * @brief 封装 Vulkan 渲染通道（Render Pass）的创建与销毁。
 * 支持多重采样（MSAA）颜色附件、深度测试附件，以及最终 Resolve 至单采样目标。
 */
class RenderPass {
public:
    /**
     * @brief 构造函数：支持直接传入逻辑设备句柄 VkDevice
     */
    RenderPass(VkDevice device, VkFormat colorFormat, VkSampleCountFlagBits msaaSamples,
               VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        : device_(device), msaaSamples_(msaaSamples)
    {
        createRenderPass(colorFormat, finalLayout);
    }


    ~RenderPass() {
        cleanup();
    }

    // 禁止拷贝，保证句柄独占性
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    // 允许移动语义
    RenderPass(RenderPass&& other) noexcept
        : device_(other.device_), renderPass_(other.renderPass_), msaaSamples_(other.msaaSamples_)
    {
        other.renderPass_ = VK_NULL_HANDLE;
    }

    RenderPass& operator=(RenderPass&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            renderPass_ = other.renderPass_;
            msaaSamples_ = other.msaaSamples_;
            other.renderPass_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkRenderPass get() const { return renderPass_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    void createRenderPass(VkFormat colorFormat, VkImageLayout finalLayout) {
        // 1. 多重采样（MSAA）颜色附件描述
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = colorFormat;
        colorAttachment.samples        = msaaSamples_;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前清空
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;  // 渲染后保存
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 2. 多重采样（MSAA）深度附件描述
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = VK_FORMAT_D32_SFLOAT;          // 标准 32 位浮点深度格式
        depthAttachment.samples        = msaaSamples_;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前清空深度
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 渲染后不需要持久化深度
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 3. 最终解析目标附件描述（Resolve Attachment, 采样率强制为 1）
        VkAttachmentDescription colorAttachmentResolve{};
        colorAttachmentResolve.format         = colorFormat;
        colorAttachmentResolve.samples        = VK_SAMPLE_COUNT_1_BIT; // 单重采样
        colorAttachmentResolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;  // 保存单采样图像以供显示或编码
        colorAttachmentResolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachmentResolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachmentResolve.finalLayout    = finalLayout; // 使用调用端指定的最终布局（SwapChain 呈现或离屏管线）

        // 4. 子通道引用的附件槽位设定
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;   // 绑定深度附件至渲染管线
        subpass.pResolveAttachments     = &resolveRef; // 绑定解析（Resolve）目标

        // 5. 子通道同步依赖关系（同步颜色输出与深度写入）
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // 6. 创建 RenderPass 实例
        std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments    = attachments.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
            throw std::runtime_error("RenderPass: Failed to create render pass with depth & resolve support.");
        }
    }

    void cleanup() {
        if (device_ != VK_NULL_HANDLE && renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan