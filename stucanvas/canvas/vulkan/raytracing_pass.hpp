// stucanvas/canvas/vulkan/raytracing_pass.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <stdexcept>
#include <memory>
#include <array>

#include "vk_ctx.hpp"
#include "buffer.hpp"
#include "shader_module.hpp"
#include "raytracing_pipeline.hpp"
#include "geometry.hpp"

namespace StuCanvas::Vulkan {

/**
 * @brief 对接 Slang 着色器参数的 C++ 相机 UBO 布局（物理 16 字节边界对齐）
 * ─────────────────────────────────────────────────────────────
 * 修复：已更新为此结构，使 C++、Vulkan 显存绑定与 Slang 编译的
 * [[vk::binding(0, 0)]] ConstantBuffer<CameraUniforms> 物理结构完全对准 [1]
 * ─────────────────────────────────────────────────────────────
 */
struct CameraUniforms {
    float cameraPos[3];
    float pad0;
    float cameraFront[3];
    float pad1;
    float cameraRight[3];
    float pad2;
    float cameraUp[3];
    uint32_t frameCount;
};

/**
 * @brief 光追渲染通道驱动类 (RAII)
 */
class RayTracingPass {
public:
    RayTracingPass(VkPhysicalDevice physicalDevice,
                   VkDevice device,
                   VmaAllocator allocator,
                   ShaderModule rayGenShader,
                   ShaderModule missShader,
                   ShaderModule closestHitShader)
        : device_(device), allocator_(allocator)
    {
        dispatch_.load(device_);

        // 1. 创建相机 Uniform 缓冲区（HOST_VISIBLE，供 CPU 实时修改）
        cameraUBO_ = Buffer::Create(
            device_, allocator_, sizeof(CameraUniforms),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        // 2. 创建描述符集布局
        createDescriptorSetLayout();

        // 3. 编译并组装光追管线
        RayTracingPipelineConfig rtConfig{};

        rtConfig.shaders.push_back(std::move(rayGenShader));
        rtConfig.shaders.push_back(std::move(missShader));
        rtConfig.shaders.push_back(std::move(closestHitShader));

        // General Group: RayGen
        RayTracingGroup rgGroup{};
        rgGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rgGroup.generalShaderIndex = 0; // 对应 shaders[0]
        rtConfig.groups.push_back(rgGroup);

        // General Group: Miss
        RayTracingGroup msGroup{};
        msGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        msGroup.generalShaderIndex = 1; // 对应 shaders[1]
        rtConfig.groups.push_back(msGroup);

        // Hit Group: Closest Hit (Triangles)
        RayTracingGroup hGroup{};
        hGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        hGroup.closestHitShaderIndex = 2; // 对应 shaders[2]
        rtConfig.groups.push_back(hGroup);

        rtConfig.descriptorSetLayouts = { descriptorSetLayout_ };
        rtConfig.maxRayRecursionDepth = 4; // 支持最大弹射深度

        pipeline_ = std::make_unique<RayTracingPipeline>(physicalDevice, device_, rtConfig);

        // 4. 创建描述符池
        createDescriptorPool();

        // 5. 分配描述符集
        allocateDescriptorSet();
    }

    ~RayTracingPass() {
        cleanup();
    }

    // 禁止拷贝
    RayTracingPass(const RayTracingPass&) = delete;
    RayTracingPass& operator=(const RayTracingPass&) = delete;

    // 允许移动语义
    RayTracingPass(RayTracingPass&& other) noexcept
        : device_(other.device_),
          allocator_(other.allocator_),
          descriptorSetLayout_(other.descriptorSetLayout_),
          descriptorPool_(other.descriptorPool_),
          descriptorSet_(other.descriptorSet_),
          cameraUBO_(std::move(other.cameraUBO_)),
          pipeline_(std::move(other.pipeline_)),
          sbt_(std::move(other.sbt_)),
          dispatch_(other.dispatch_)
    {
        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.descriptorSetLayout_ = VK_NULL_HANDLE;
        other.descriptorPool_ = VK_NULL_HANDLE;
        other.descriptorSet_ = VK_NULL_HANDLE;
    }

    RayTracingPass& operator=(RayTracingPass&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            allocator_ = other.allocator_;
            descriptorSetLayout_ = other.descriptorSetLayout_;
            descriptorPool_ = other.descriptorPool_;
            descriptorSet_ = other.descriptorSet_;
            cameraUBO_ = std::move(other.cameraUBO_);
            pipeline_ = std::move(other.pipeline_);
            sbt_ = std::move(other.sbt_);
            dispatch_ = other.dispatch_;

            other.device_ = VK_NULL_HANDLE;
            other.allocator_ = VK_NULL_HANDLE;
            other.descriptorSetLayout_ = VK_NULL_HANDLE;
            other.descriptorPool_ = VK_NULL_HANDLE;
            other.descriptorSet_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    /**
     * @brief 构建并装配管线配套的 SBT 绑定表
     */
    void buildShaderBindingTable(VkCommandPool commandPool, VkQueue queue) {
        sbt_ = pipeline_->createSBT(
            allocator_, commandPool, queue,
            {
                { VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 0 },
                { VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 1 },
                { VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR, 2 }
            },
            { 0 }, // RGen 处于 Index 0
            { 1 }, // Miss 处于 Index 1
            { 2 }  // Hit 处于 Index 2
        );
    }

    /**
     * @brief 更新相机 Uniform 缓冲区（支持运行时每一帧调用）
     */
    void updateCamera(const CameraUniforms& cameraData) {
        cameraUBO_.uploadData(&cameraData, sizeof(CameraUniforms));
    }

    /**
     * @brief 更新描述符集绑定关系，将 GPU 场景数据与着色器通路打通
     * @param tlas 构建完毕的顶层加速结构 (TLAS)
     * @param outputImageView 离屏或渲染输出的 Storage 图像视图 (RWTexture2D)
     * @param sceneMesh 外部已就绪的纯 GPU 网格资产（GPUMesh）
     */
    void updateDescriptorSet(
        VkAccelerationStructureKHR tlas,
        VkImageView outputImageView,
        const GPUMesh& sceneMesh
    ) {
        std::array<VkWriteDescriptorSet, 5> writes{};

        // Binding 0: Camera Uniform
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = cameraUBO_.get();
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(CameraUniforms);

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet_;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &cameraInfo;

        // Binding 1: Acceleration Structure (TLAS)
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
        asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &tlas;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].pNext = &asWrite;
        writes[1].dstSet = descriptorSet_;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        // Binding 2: Output Image (Storage Image)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = outputImageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSet_;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo = &imageInfo;

        // Binding 3: Vertex StructuredBuffer
        VkDescriptorBufferInfo vertexInfo{};
        vertexInfo.buffer = sceneMesh.getVertexBuffer();
        vertexInfo.offset = 0;
        vertexInfo.range = sceneMesh.getVertexCount() * sizeof(Vertex);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSet_;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &vertexInfo;

        // Binding 4: Index StructuredBuffer
        VkDescriptorBufferInfo indexInfo{};
        indexInfo.buffer = sceneMesh.getIndexBuffer();
        indexInfo.offset = 0;
        indexInfo.range = sceneMesh.getIndexCount() * sizeof(uint32_t);

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSet_;
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo = &indexInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    /**
     * @brief 录制并下发 GPU 光追命令 (vkCmdTraceRaysKHR)
     */
    void cmdTraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_->get());

        VkDescriptorSet descriptorSets[] = { descriptorSet_ };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_->getLayout(), 0, 1, descriptorSets, 0, nullptr);

        dispatch_.vkCmdTraceRaysKHR(
            cmd,
            &sbt_.rgenRegion,
            &sbt_.missRegion,
            &sbt_.hitRegion,
            &sbt_.callableRegion,
            width, height, 1
        );
    }

    // 访问器
    [[nodiscard]] VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_; }
    [[nodiscard]] VkPipelineLayout      getPipelineLayout()      const { return pipeline_->getLayout(); }
    [[nodiscard]] VkPipeline            getPipeline()            const { return pipeline_->get(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    Buffer cameraUBO_;
    std::unique_ptr<RayTracingPipeline> pipeline_;
    ShaderBindingTable sbt_;
    RayTracingDispatchTable dispatch_;

    void createDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

        // Binding 0: Camera Uniform Block
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 1: Acceleration Structure (TLAS)
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        // Binding 2: Output Storage Image
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        // Binding 3: Vertex StructuredBuffer
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Binding 4: Index StructuredBuffer
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingPass: Failed to create descriptor set layout.");
        }
    }

    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 4> poolSizes{};

        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;

        poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[1].descriptorCount = 1;

        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[2].descriptorCount = 1;

        poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[3].descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingPass: Failed to create descriptor pool.");
        }
    }

    void allocateDescriptorSet() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout_;

        if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingPass: Failed to allocate descriptor set.");
        }
    }

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            if (descriptorPool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
                descriptorPool_ = VK_NULL_HANDLE;
            }
            if (descriptorSetLayout_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
                descriptorSetLayout_ = VK_NULL_HANDLE;
            }
            cameraUBO_ = {};
            pipeline_.reset();
            sbt_ = {};
            device_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan