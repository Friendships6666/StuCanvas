// stucanvas/canvas/vulkan/compute_pipeline.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <string>

namespace StuCanvas::Vulkan {

/**
 * @brief 计算管线封装类 (RAII)
 * 专门用于处理类似颜色空间转换 (RGB -> YUV) 等重型计算任务。
 */
class ComputePipeline {
public:
    ComputePipeline(VkDevice device,
                    VkShaderModule shaderModule,
                    const std::vector<VkDescriptorSetLayout>& setLayouts)
        : device_(device)
    {
        // 1. 创建管线布局 (Pipeline Layout)
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("ComputePipeline: Failed to create compute pipeline layout.");
        }

        // 2. 创建计算管线 (Compute Pipeline)
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main"; // 对应 Slang 中的 [shader("compute")] 入口名
        pipelineInfo.layout = layout_;

        // 计算管线的创建独立于 RenderPass
        if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
            throw std::runtime_error("ComputePipeline: Failed to create compute pipeline.");
        }
    }

    ~ComputePipeline() {
        cleanup();
    }

    // 禁止拷贝
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // 允许移动语义，保证多重场景下的生命周期流转安全
    ComputePipeline(ComputePipeline&& other) noexcept
        : device_(other.device_), layout_(other.layout_), pipeline_(other.pipeline_)
    {
        other.pipeline_ = VK_NULL_HANDLE;
        other.layout_   = VK_NULL_HANDLE;
        other.device_   = VK_NULL_HANDLE;
    }

    ComputePipeline& operator=(ComputePipeline&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_   = other.device_;
            layout_   = other.layout_;
            pipeline_ = other.pipeline_;
            other.pipeline_ = VK_NULL_HANDLE;
            other.layout_   = VK_NULL_HANDLE;
            other.device_   = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 获取句柄
    [[nodiscard]] VkPipeline get() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout getLayout() const { return layout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            if (pipeline_ != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, pipeline_, nullptr);
                pipeline_ = VK_NULL_HANDLE;
            }
            if (layout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_, layout_, nullptr);
                layout_ = VK_NULL_HANDLE;
            }
            device_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan