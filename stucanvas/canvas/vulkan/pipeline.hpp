// stucanvas/canvas/vulkan/pipeline.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace StuCanvas::Vulkan {

/**
 * @brief 图形管线配置结构体
 *
 * 封装创建 VkGraphicsPipeline 所需的所有状态。
 * 默认配置：动态视口+裁剪、填充三角形、背面剔除、无深度测试、无混合。
 */
struct PipelineConfig {
    // ── 着色器阶段 ──
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    const char* vertEntry = "main";
    const char* fragEntry = "main";

    // ── 顶点输入（默认无顶点缓冲）──
    uint32_t vertexBindingCount = 0;
    VkVertexInputBindingDescription* pVertexBindings = nullptr;
    uint32_t vertexAttributeCount = 0;
    VkVertexInputAttributeDescription* pVertexAttributes = nullptr;

    // ── 输入装配 ──
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitiveRestart = VK_FALSE;
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 是否开启硬件级样本着色 (提升内边抗锯齿)
    VkBool32 sampleShadingEnable = VK_FALSE;
    float minSampleShading = 0.2f;
    // ── 视口与裁剪（如果设为动态，则忽略下面静态值）──
    bool dynamicViewport = true;
    bool dynamicScissor  = true;
    // 静态视口（当 dynamicViewport == false 时使用）
    float viewportX = 0.f, viewportY = 0.f;
    float viewportWidth = 800.f, viewportHeight = 600.f;
    float viewportMinDepth = 0.f, viewportMaxDepth = 1.f;
    // 静态裁剪（当 dynamicScissor == false 时使用）
    VkRect2D scissor = {{0, 0}, {800, 600}};

    // ── 光栅化 ──
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    float lineWidth = 1.f;
    VkBool32 depthClampEnable = VK_FALSE;
    VkBool32 rasterizerDiscardEnable = VK_FALSE;

    // ── 多重采样 ──


    // ── 深度与模板（如需深度，可设置 depthTestEnable = VK_TRUE 并提供深度附件格式）──
    VkBool32 depthTestEnable = VK_FALSE;
    VkBool32 depthWriteEnable = VK_FALSE;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    VkBool32 stencilTestEnable = VK_FALSE;
    VkStencilOpState front = {};
    VkStencilOpState back = {};

    // ── 颜色混合（单个附件）──
    VkBool32 blendEnable = VK_FALSE;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // ── 动态状态列表 ──
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    // ── 管线布局资源（暂时空）──
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    std::vector<VkPushConstantRange> pushConstantRanges;
};

/**
 * @brief 图形管线封装
 *
 * 根据 PipelineConfig 创建 VkPipeline 和 VkPipelineLayout。
 * 构造后可通过 get() 和 getLayout() 获取句柄。
 */
class Pipeline {
public:
    Pipeline(VkDevice device, VkRenderPass renderPass, const PipelineConfig& config)
        : device_(device)
    {
        createPipelineLayout(config);
        createPipeline(renderPass, config);
    }

    ~Pipeline() {
        if (pipeline_ != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, pipeline_, nullptr);
        if (layout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, layout_, nullptr);
    }

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept
        : device_(other.device_), pipeline_(other.pipeline_), layout_(other.layout_)
    {
        other.pipeline_ = VK_NULL_HANDLE;
        other.layout_   = VK_NULL_HANDLE;
    }

    Pipeline& operator=(Pipeline&& other) noexcept {
        if (this != &other) {
            if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
            if (layout_   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, layout_, nullptr);
            device_   = other.device_;
            pipeline_ = other.pipeline_;
            layout_   = other.layout_;
            other.pipeline_ = VK_NULL_HANDLE;
            other.layout_   = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkPipeline get()       const { return pipeline_; }
    VkPipelineLayout getLayout() const { return layout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;

    void createPipelineLayout(const PipelineConfig& config) {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = config.descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
        layoutInfo.pPushConstantRanges = config.pushConstantRanges.data();

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create pipeline layout");
        }
    }

    void createPipeline(VkRenderPass renderPass, const PipelineConfig& config) {
        // ── 着色器阶段 ──
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = config.vertShaderModule;
        vertStage.pName = config.vertEntry;

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = config.fragShaderModule;
        fragStage.pName = config.fragEntry;

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        // ── 顶点输入 ──
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = config.vertexBindingCount;
        vertexInput.pVertexBindingDescriptions = config.pVertexBindings;
        vertexInput.vertexAttributeDescriptionCount = config.vertexAttributeCount;
        vertexInput.pVertexAttributeDescriptions = config.pVertexAttributes;

        // ── 输入装配 ──
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = config.topology;
        inputAssembly.primitiveRestartEnable = config.primitiveRestart;

        // ── 视口与裁剪（动态时忽略静态值）──
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;                             // 即使动态，通道需要至少1
        viewportState.scissorCount  = 1;
        // 如果动态，这两个指针可以为 nullptr，但规范允许链接静态值，我们提供 nullptr 表示动态
        VkViewport staticViewport{};
        if (!config.dynamicViewport) {
            staticViewport.x = config.viewportX;
            staticViewport.y = config.viewportY;
            staticViewport.width = config.viewportWidth;
            staticViewport.height = config.viewportHeight;
            staticViewport.minDepth = config.viewportMinDepth;
            staticViewport.maxDepth = config.viewportMaxDepth;
            viewportState.pViewports = &staticViewport;
        } else {
            viewportState.pViewports = nullptr;
        }
        VkRect2D staticScissor{};
        if (!config.dynamicScissor) {
            staticScissor = config.scissor;
            viewportState.pScissors = &staticScissor;
        } else {
            viewportState.pScissors = nullptr;
        }
        // 注意：对于动态视口/裁剪，Vulkan 的 viewportCount 和 scissorCount 仍然需要为非零，
        // 但 pViewports/pScissors 可以为 nullptr，具体驱动可能要求非空？规范说若对应状态是动态的，这些指针被忽略。
        // 为安全，我们可以设置一个假数组。
        if (config.dynamicViewport) {
            staticViewport = {}; // dummy
            viewportState.pViewports = &staticViewport;
        }
        if (config.dynamicScissor) {
            staticScissor = {};
            viewportState.pScissors = &staticScissor;
        }

        // ── 光栅化 ──
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = config.depthClampEnable;
        rasterizer.rasterizerDiscardEnable = config.rasterizerDiscardEnable;
        rasterizer.polygonMode = config.polygonMode;
        rasterizer.cullMode = config.cullMode;
        rasterizer.frontFace = config.frontFace;
        rasterizer.lineWidth = config.lineWidth;
        rasterizer.depthBiasEnable = VK_FALSE;

        // ── 多重采样 ──
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

        // 【关键点 1】：必须与 RenderPass 的 samples 严格一致
        // 如果 RenderPass 是 4x，这里必须是 VK_SAMPLE_COUNT_4_BIT
        multisampling.rasterizationSamples = config.rasterizationSamples;

        // 【关键点 2】：样本着色 (Sample Shading)
        // 开启它可以显著提升 Shader 内部逻辑（如你的贝塞尔曲线 SDF）的抗锯齿质量
        // 它会让片元着色器在像素内部运行多次。
        if (config.sampleShadingEnable) {
            multisampling.sampleShadingEnable = VK_TRUE;
            // minSampleShading 决定了采样的频率。1.0f 表示每个样本都运行一次片元着色器（最清晰但也最累）
            // 0.2f 是一个折中值，可以有效减少线条的闪烁。
            multisampling.minSampleShading = config.minSampleShading;
        } else {
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.minSampleShading = 1.0f;
        }

        multisampling.pSampleMask = nullptr; // 可选
        multisampling.alphaToCoverageEnable = VK_FALSE; // 如果做植被等半透明裁剪可开启
        multisampling.alphaToOneEnable = VK_FALSE;      // 可选

        // ── 深度模板 ──
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = config.depthTestEnable;
        depthStencil.depthWriteEnable = config.depthWriteEnable;
        depthStencil.depthCompareOp = config.depthCompareOp;
        depthStencil.stencilTestEnable = config.stencilTestEnable;
        depthStencil.front = config.front;
        depthStencil.back = config.back;

        // ── 颜色混合 ──
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = config.blendEnable;
        colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
        colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
        colorBlendAttachment.colorBlendOp = config.colorBlendOp;
        colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
        colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
        colorBlendAttachment.alphaBlendOp = config.alphaBlendOp;
        colorBlendAttachment.colorWriteMask = config.colorWriteMask;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // ── 动态状态 ──
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(config.dynamicStates.size());
        dynamicState.pDynamicStates = config.dynamicStates.data();

        // ── 图形管线创建 ──
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layout_;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0; // 默认子通道0
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create graphics pipeline");
        }
    }
};

} // namespace StuCanvas::Vulkan