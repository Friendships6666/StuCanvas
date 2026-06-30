// stucanvas/canvas/vulkan/raytracing_pipeline.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <memory>
#include <array>

#include "vk_ctx.hpp"
#include "buffer.hpp"
#include "shader_module.hpp"

namespace StuCanvas::Vulkan {

// ─────────────────────────────────────────────────────────────
// 1. KHR 光追踪扩展函数动态载入表
// ─────────────────────────────────────────────────────────────
struct RayTracingDispatchTable {
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

    void load(VkDevice device) {
        auto loadSym = [device](const char* name) -> void* {
            auto sym = reinterpret_cast<void*>(vkGetDeviceProcAddr(device, name));
            if (!sym) throw std::runtime_error(std::string("RayTracing: Failed to load symbol: ") + name);
            return sym;
        };

        vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(loadSym("vkCreateAccelerationStructureKHR"));
        vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(loadSym("vkDestroyAccelerationStructureKHR"));
        vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(loadSym("vkGetAccelerationStructureBuildSizesKHR"));
        vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(loadSym("vkCmdBuildAccelerationStructuresKHR"));
        vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(loadSym("vkGetAccelerationStructureDeviceAddressKHR"));
        vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(loadSym("vkCreateRayTracingPipelinesKHR"));
        vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(loadSym("vkGetRayTracingShaderGroupHandlesKHR"));
        vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(loadSym("vkCmdTraceRaysKHR"));
    }
};

// ─────────────────────────────────────────────────────────────
// 2. RAII 物理加速结构封装类 (仅负责 AS 句柄与支撑 Buffer 显存管理)
// ─────────────────────────────────────────────────────────────
class AccelerationStructure {
public:
    AccelerationStructure() = default;

    ~AccelerationStructure() {
        cleanup();
    }

    // 禁止拷贝
    AccelerationStructure(const AccelerationStructure&) = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;

    // 移动语义
    AccelerationStructure(AccelerationStructure&& other) noexcept
        : device_(other.device_),
          as_(other.as_),
          buffer_(std::move(other.buffer_)),
          deviceAddress_(other.deviceAddress_),
          dispatch_(other.dispatch_)
    {
        other.as_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.deviceAddress_ = 0;
    }

    AccelerationStructure& operator=(AccelerationStructure&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            as_ = other.as_;
            buffer_ = std::move(other.buffer_);
            deviceAddress_ = other.deviceAddress_;
            dispatch_ = other.dispatch_;

            other.as_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
            other.deviceAddress_ = 0;
        }
        return *this;
    }

    [[nodiscard]] VkAccelerationStructureKHR get() const { return as_; }
    [[nodiscard]] VkDeviceAddress getDeviceAddress() const { return deviceAddress_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkAccelerationStructureKHR as_ = VK_NULL_HANDLE;
    Buffer buffer_;
    VkDeviceAddress deviceAddress_ = 0;
    RayTracingDispatchTable dispatch_;

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            if (as_ != VK_NULL_HANDLE && dispatch_.vkDestroyAccelerationStructureKHR) {
                dispatch_.vkDestroyAccelerationStructureKHR(device_, as_, nullptr);
                as_ = VK_NULL_HANDLE;
            }
            buffer_ = {};
            device_ = VK_NULL_HANDLE;
            deviceAddress_ = 0;
        }
    }

    friend class RayTracingBuilder;
};

// ─────────────────────────────────────────────────────────────
// 3. 裸 GPU 物理地址加速结构构建器
// ─────────────────────────────────────────────────────────────
class RayTracingBuilder {
public:
    RayTracingBuilder(VkDevice device, VmaAllocator allocator)
        : device_(device), allocator_(allocator)
    {
        dispatch_.load(device_);
    }

    /**
     * @brief 利用外部已绑定、准备完毕的 KHR 几何体描述直接构建底层加速结构 (BLAS)
     */
    AccelerationStructure buildBLAS(
        const std::vector<VkAccelerationStructureGeometryKHR>& geometries,
        const std::vector<uint32_t>& maxPrimitiveCounts,
        const std::vector<VkAccelerationStructureBuildRangeInfoKHR>& buildRanges,
        VkCommandPool commandPool,
        VkQueue queue
    ) {
        if (geometries.size() != maxPrimitiveCounts.size() || geometries.size() != buildRanges.size()) {
            throw std::invalid_argument("RayTracingBuilder: Input vectors must have matching dimensions.");
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        // 1. 评估尺寸
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        dispatch_.vkGetAccelerationStructureBuildSizesKHR(
            device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, maxPrimitiveCounts.data(), &sizeInfo
        );

        // 2. 分配 BLAS 支撑显存 Buffer
        Buffer asBuffer(
            device_, allocator_, sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = asBuffer.get();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VkAccelerationStructureKHR as = VK_NULL_HANDLE;
        if (dispatch_.vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &as) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingBuilder: Failed to create VkAccelerationStructureKHR for BLAS.");
        }

        // 3. 分配临时 Scratch 缓冲区
        Buffer scratchBuffer(
            device_, allocator_, sizeInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkBufferDeviceAddressInfo scratchAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        scratchAddrInfo.buffer = scratchBuffer.get();
        VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(device_, &scratchAddrInfo);

        // 4. 执行 GPU 构建
        buildInfo.dstAccelerationStructure = as;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = commandPool;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // ─────────────────────────────────────────────────────────────
        // 修复：组织合规的多几何体指针数组，指向外部真实的物理 buildRanges 数据
        // ─────────────────────────────────────────────────────────────
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> pRangeInfos(buildRanges.size());
        for (size_t i = 0; i < buildRanges.size(); ++i) {
            pRangeInfos[i] = &buildRanges[i];
        }
        dispatch_.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, pRangeInfos.data());

        vkEndCommandBuffer(cmd);

        // 同步等待 GPU 物理构建完成
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        vkCreateFence(device_, &fenceInfo, nullptr, &fence);

        vkQueueSubmit(queue, 1, &submit, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool, 1, &cmd);

        VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo{};
        asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asAddrInfo.accelerationStructure = as;
        VkDeviceAddress asDeviceAddress = dispatch_.vkGetAccelerationStructureDeviceAddressKHR(device_, &asAddrInfo);

        AccelerationStructure outAS;
        outAS.device_ = device_;
        outAS.as_ = as;
        outAS.buffer_ = std::move(asBuffer);
        outAS.deviceAddress_ = asDeviceAddress;
        outAS.dispatch_ = dispatch_;

        return outAS;
    }

    /**
     * @brief 直接使用准备好的 GPU 实例数据地址构建顶层加速结构 (TLAS)
     */
    AccelerationStructure buildTLAS(
        VkDeviceAddress instancesDeviceAddress,
        uint32_t instanceCount,
        VkCommandPool commandPool,
        VkQueue queue
    ) {
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;

        auto& instancesData = geometry.geometry.instances;
        instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instancesDeviceAddress;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        dispatch_.vkGetAccelerationStructureBuildSizesKHR(
            device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, &instanceCount, &sizeInfo
        );

        Buffer asBuffer(
            device_, allocator_, sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = asBuffer.get();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        VkAccelerationStructureKHR as = VK_NULL_HANDLE;
        if (dispatch_.vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &as) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingBuilder: Failed to create VkAccelerationStructureKHR for TLAS.");
        }

        Buffer scratchBuffer(
            device_, allocator_, sizeInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VkBufferDeviceAddressInfo scratchAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        scratchAddrInfo.buffer = scratchBuffer.get();
        VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(device_, &scratchAddrInfo);

        buildInfo.dstAccelerationStructure = as;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = commandPool;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
        dispatch_.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        vkCreateFence(device_, &fenceInfo, nullptr, &fence);

        vkQueueSubmit(queue, 1, &submit, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool, 1, &cmd);

        VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo{};
        asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asAddrInfo.accelerationStructure = as;
        VkDeviceAddress asDeviceAddress = dispatch_.vkGetAccelerationStructureDeviceAddressKHR(device_, &asAddrInfo);

        AccelerationStructure outAS;
        outAS.device_ = device_;
        outAS.as_ = as;
        outAS.buffer_ = std::move(asBuffer);
        outAS.deviceAddress_ = asDeviceAddress;
        outAS.dispatch_ = dispatch_;

        return outAS;
    }

private:
    VkDevice device_;
    VmaAllocator allocator_;
    RayTracingDispatchTable dispatch_;
};

// ─────────────────────────────────────────────────────────────
// 4. 着色器绑定表 (SBT) 内存布局控制信息
// ─────────────────────────────────────────────────────────────
struct ShaderBindingTable {
    Buffer rgenBuffer;
    Buffer missBuffer;
    Buffer hitBuffer;
    Buffer callableBuffer;

    VkStridedDeviceAddressRegionKHR rgenRegion{};
    VkStridedDeviceAddressRegionKHR missRegion{};
    VkStridedDeviceAddressRegionKHR hitRegion{};
    VkStridedDeviceAddressRegionKHR callableRegion{};
};

// ─────────────────────────────────────────────────────────────
// 5. 光追组态配置结构体
// ─────────────────────────────────────────────────────────────
struct RayTracingGroup {
    VkRayTracingShaderGroupTypeKHR type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    uint32_t generalShaderIndex = VK_SHADER_UNUSED_KHR;
    uint32_t closestHitShaderIndex = VK_SHADER_UNUSED_KHR;
    uint32_t anyHitShaderIndex = VK_SHADER_UNUSED_KHR;
    uint32_t intersectionShaderIndex = VK_SHADER_UNUSED_KHR;
};

struct RayTracingPipelineConfig {
    std::vector<ShaderModule> shaders;
    std::vector<RayTracingGroup> groups;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    std::vector<VkPushConstantRange> pushConstantRanges;
    uint32_t maxRayRecursionDepth = 1;
};

// ─────────────────────────────────────────────────────────────
// 6. 核心光照/路径追踪管线类 (RAII)
// ─────────────────────────────────────────────────────────────
class RayTracingPipeline {
public:
    RayTracingPipeline(VkPhysicalDevice physicalDevice, VkDevice device, const RayTracingPipelineConfig& config)
        : device_(device)
    {
        dispatch_.load(device_);

        VkPhysicalDeviceProperties2 deviceProperties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        deviceProperties2.pNext = &rtProperties_;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

        createPipelineLayout(config);
        createPipeline(config);
    }

    ~RayTracingPipeline() {
        cleanup();
    }

    // 禁止拷贝
    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;

    // 支持移动
    RayTracingPipeline(RayTracingPipeline&& other) noexcept
        : device_(other.device_),
          pipeline_(other.pipeline_),
          layout_(other.layout_),
          rtProperties_(other.rtProperties_),
          dispatch_(other.dispatch_)
    {
        other.pipeline_ = VK_NULL_HANDLE;
        other.layout_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    RayTracingPipeline& operator=(RayTracingPipeline&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            pipeline_ = other.pipeline_;
            layout_ = other.layout_;
            rtProperties_ = other.rtProperties_;
            dispatch_ = other.dispatch_;

            other.pipeline_ = VK_NULL_HANDLE;
            other.layout_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkPipeline get() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout getLayout() const { return layout_; }

    /**
     * @brief 为当前光追管线编译生成相对应的 Shader 绑定表 (SBT)
     */
    ShaderBindingTable createSBT(
        VmaAllocator allocator,
        VkCommandPool commandPool,
        VkQueue queue,
        const std::vector<RayTracingGroup>& groups,
        const std::vector<uint32_t>& rgenGroupIndices,
        const std::vector<uint32_t>& missGroupIndices,
        const std::vector<uint32_t>& hitGroupIndices,
        const std::vector<uint32_t>& callableGroupIndices = {}
    ) {
        uint32_t handleSize = rtProperties_.shaderGroupHandleSize;
        uint32_t handleAlignment = rtProperties_.shaderGroupHandleAlignment;
        uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        auto groupCount = static_cast<uint32_t>(groups.size());
        uint32_t dataSize = groupCount * handleSize;
        std::vector<uint8_t> handles(dataSize);

        if (dispatch_.vkGetRayTracingShaderGroupHandlesKHR(
            device_, pipeline_, 0, groupCount, dataSize, handles.data()) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingPipeline: Failed to retrieve shader group handles.");
        }

        ShaderBindingTable sbt;

        auto buildSBTSection = [&](const std::vector<uint32_t>& indices, Buffer& outBuffer, VkStridedDeviceAddressRegionKHR& outRegion) {
            if (indices.empty()) return;

            uint32_t stride = handleSizeAligned;
            VkDeviceSize bufferSize = indices.size() * stride;

            std::vector<uint8_t> sbtData(bufferSize, 0);
            for (size_t i = 0; i < indices.size(); ++i) {
                uint32_t groupIndex = indices[i];
                const uint8_t* handlePtr = &handles[groupIndex * handleSize];
                std::memcpy(&sbtData[i * stride], handlePtr, handleSize);
            }

            outBuffer = Buffer::CreateAndUpload(
                device_, allocator, commandPool, queue,
                sbtData.data(), bufferSize,
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            );

            VkBufferDeviceAddressInfo addrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
            addrInfo.buffer = outBuffer.get();
            VkDeviceAddress address = vkGetBufferDeviceAddress(device_, &addrInfo);

            outRegion.deviceAddress = address;
            outRegion.stride = stride;
            outRegion.size = bufferSize;
        };

        buildSBTSection(rgenGroupIndices, sbt.rgenBuffer, sbt.rgenRegion);
        buildSBTSection(missGroupIndices, sbt.missBuffer, sbt.missRegion);
        buildSBTSection(hitGroupIndices, sbt.hitBuffer, sbt.hitRegion);
        buildSBTSection(callableGroupIndices, sbt.callableBuffer, sbt.callableRegion);

        return sbt;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties_{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
    RayTracingDispatchTable dispatch_;

    void createPipelineLayout(const RayTracingPipelineConfig& config) {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = config.descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size());
        layoutInfo.pPushConstantRanges = config.pushConstantRanges.data();

        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("RayTracingPipeline: Failed to create pipeline layout.");
        }
    }

    void createPipeline(const RayTracingPipelineConfig& config) {
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(config.shaders.size());
        for (const auto& shader : config.shaders) {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = shader.getVulkanStage();
            stageInfo.module = shader.getModule();
            stageInfo.pName = shader.getEntryPointName().c_str();
            stages.push_back(stageInfo);
        }

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups(config.groups.size());
        for (size_t i = 0; i < config.groups.size(); ++i) {
            groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            groups[i].type = config.groups[i].type;
            groups[i].generalShader = config.groups[i].generalShaderIndex;
            groups[i].closestHitShader = config.groups[i].closestHitShaderIndex;
            groups[i].anyHitShader = config.groups[i].anyHitShaderIndex;
            groups[i].intersectionShader = config.groups[i].intersectionShaderIndex;
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = config.maxRayRecursionDepth;
        pipelineInfo.layout = layout_;

        if (dispatch_.vkCreateRayTracingPipelinesKHR(
            device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
            throw std::runtime_error("RayTracingPipeline: Failed to create ray tracing pipeline.");
        }
    }

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