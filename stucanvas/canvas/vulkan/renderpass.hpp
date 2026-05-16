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
    RenderPass(VkDevice device, VkFormat colorFormat, VkSampleCountFlagBits msaaSamples,
               VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) // 默认值保持预览兼容
        : device_(device),msaaSamples_(msaaSamples)
    {
        createRenderPass(colorFormat, finalLayout);
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
    VkRenderPass renderPass_{};
    VkSampleCountFlagBits msaaSamples_{};


    void createRenderPass(VkFormat colorFormat, VkImageLayout finalLayout) {
        // 1. 颜色附件描述





        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = colorFormat;
        colorAttachment.samples        = msaaSamples_;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前清空
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;  // 渲染后保存以供显示
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        // colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 用于交换链呈现
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // 2. 深度附件描述
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = VK_FORMAT_D32_SFLOAT;          // 标准深度格式
        depthAttachment.samples        = msaaSamples_;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前清空深度缓冲区
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 渲染后不需要保存深度数据
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription colorAttachmentResolve{};
        colorAttachmentResolve.format = colorFormat;
        colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT; // 必须是 1
        colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentResolve.finalLayout = finalLayout;



        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        // 4. 子通道配置
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef; // 关键：绑定深度附件到子通道
        subpass.pResolveAttachments = &resolveRef; // 关键：在这里配置解析目标

        // 5. 子通道依赖（同步颜色和深度写入）
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        // 等待颜色输出阶段和早期的碎片测试（深度测试发生在此阶段）
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // 6. 创建渲染通道
        std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment ,colorAttachmentResolve};

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size()); // 必须是 3
        renderPassInfo.pAttachments    = attachments.data();                       // 数组指针
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass with depth support.");
        }
    }
};

} // namespace StuCanvas::Vulkan