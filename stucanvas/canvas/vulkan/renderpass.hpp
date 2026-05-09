// stucanvas/canvas/vulkan/renderpass.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>

namespace StuCanvas::Vulkan {

/**
 * @brief 封装 Vulkan 渲染通道（Render Pass）的创建与销毁。
 *
 * 当前实现仅包含单个颜色附件（无深度/模板），适用于简单三角形绘制等场景。
 * 后续可扩展深度附件、多重采样、子通道依赖等。
 */
class RenderPass {
public:
    /**
     * @brief 构造并创建渲染通道。
     * @param device        Vulkan 逻辑设备句柄。
     * @param colorFormat   颜色附件的格式（通常与交换链图像格式一致）。
     */
    RenderPass(VkDevice device, VkFormat colorFormat)
        : device_(device), renderPass_(VK_NULL_HANDLE)
    {
        createRenderPass(colorFormat);
    }

    ~RenderPass() {
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        }
    }

    // 禁止拷贝
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    // 允许移动
    RenderPass(RenderPass&& other) noexcept
        : device_(other.device_), renderPass_(other.renderPass_)
    {
        other.renderPass_ = VK_NULL_HANDLE;
    }
    RenderPass& operator=(RenderPass&& other) noexcept {
        if (this != &other) {
            if (renderPass_ != VK_NULL_HANDLE)
                vkDestroyRenderPass(device_, renderPass_, nullptr);
            device_ = other.device_;
            renderPass_ = other.renderPass_;
            other.renderPass_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkRenderPass get() const { return renderPass_; }

private:
    VkDevice device_;
    VkRenderPass renderPass_;

    void createRenderPass(VkFormat colorFormat) {
        // 颜色附件描述
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = colorFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // 颜色附件引用（子通道布局）
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 子通道
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorAttachmentRef;

        // 子通道依赖（确保渲染在图像可用后开始）
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // 渲染通道创建信息
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass.");
        }
    }
};

} // namespace StuCanvas::Vulkan