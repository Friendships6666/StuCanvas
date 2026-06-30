// stucanvas/canvas/vulkan/geometry.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <stdexcept>
#include <memory>

#include "vk_ctx.hpp"
#include "buffer.hpp"

namespace StuCanvas::Vulkan {

/**
 * @brief 专为 GPU 渲染与光追求交优化的标准顶点格式（48 字节物理内存对齐）
 */
struct Vertex {
    float position[4]; // 16 字节
    float normal[4];   // 16 字节
    float texCoord[2]; // 8 字节
    float pad[2];      // 8 字节填充，总体完全对齐为 48 字节
};

/**
 * @brief GPU 侧网格几何体 RAII 封装类
 * 持有物理缓冲区（VMA 分配）并导出光追所需的虚拟地址及 KHR 几何体描述。
 */
class GPUMesh {
public:
    GPUMesh() = default;

    /**
     * @brief 构造函数：直接传入已分配好的顶点和索引 GPU 缓冲区
     */
    GPUMesh(VkDevice device,
            Buffer&& vertexBuffer,
            uint32_t vertexCount,
            Buffer&& indexBuffer,
            uint32_t indexCount)
        : device_(device),
          vertexBuffer_(std::move(vertexBuffer)),
          vertexCount_(vertexCount),
          indexBuffer_(std::move(indexBuffer)),
          indexCount_(indexCount)
    {
        retrieveDeviceAddresses();
    }

    ~GPUMesh() {
        cleanup();
    }

    // 禁止拷贝，保证 GPU 显存生命周期安全
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh& operator=(const GPUMesh&) = delete;

    // 支持移动语义
    GPUMesh(GPUMesh&& other) noexcept
        : device_(other.device_),
          vertexBuffer_(std::move(other.vertexBuffer_)),
          vertexCount_(other.vertexCount_),
          indexBuffer_(std::move(other.indexBuffer_)),
          indexCount_(other.indexCount_),
          vertexAddress_(other.vertexAddress_),
          indexAddress_(other.indexAddress_)
    {
        other.device_ = VK_NULL_HANDLE;
        other.vertexCount_ = 0;
        other.indexCount_ = 0;
        other.vertexAddress_ = 0;
        other.indexAddress_ = 0;
    }

    GPUMesh& operator=(GPUMesh&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            vertexBuffer_ = std::move(other.vertexBuffer_);
            vertexCount_ = other.vertexCount_;
            indexBuffer_ = std::move(other.indexBuffer_);
            indexCount_ = other.indexCount_;
            vertexAddress_ = other.vertexAddress_;
            indexAddress_ = other.indexAddress_;

            other.device_ = VK_NULL_HANDLE;
            other.vertexCount_ = 0;
            other.indexCount_ = 0;
            other.vertexAddress_ = 0;
            other.indexAddress_ = 0;
        }
        return *this;
    }

    // ─────────────────────────────────────────────────────────────
    // 静态辅助上传方法（CPU -> GPU 数据桥梁）
    // ─────────────────────────────────────────────────────────────

    /**
     * @brief 一键将 CPU 端准备好的顶点与索引数组上传至 GPU 本地（使用 VMA 与临时 Staging 拷贝）
     */
    static GPUMesh upload(
        VkDevice device,
        VmaAllocator allocator,
        VkCommandPool commandPool,
        VkQueue queue,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices
    ) {
        VkDeviceSize vertexSize = vertices.size() * sizeof(Vertex);
        VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);

        // 创建并上传 Vertex Buffer (支持 AS 构建和 64位 Buffer Device Address 检索)
        Buffer stageVertex = Buffer::Create(
            device, allocator, vertexSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        stageVertex.uploadData(vertices.data(), vertexSize);

        Buffer gpuVertex = Buffer::Create(
            device, allocator, vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        stageVertex.copyTo(gpuVertex.get(), vertexSize, commandPool, queue);

        // 创建并上传 Index Buffer
        Buffer stageIndex = Buffer::Create(
            device, allocator, indexSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        stageIndex.uploadData(indices.data(), indexSize);

        Buffer gpuIndex = Buffer::Create(
            device, allocator, indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        stageIndex.copyTo(gpuIndex.get(), indexSize, commandPool, queue);

        return GPUMesh(device, std::move(gpuVertex), static_cast<uint32_t>(vertices.size()),
                       std::move(gpuIndex), static_cast<uint32_t>(indices.size()));
    }

    // ─────────────────────────────────────────────────────────────
    // 光追 KHR 描述符一键生成接口
    // ─────────────────────────────────────────────────────────────

    /**
     * @brief 获取适配 KHR 底层加速结构（BLAS）所需的三角形几何体描述
     */
    [[nodiscard]] VkAccelerationStructureGeometryKHR getASGeometry() const {
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

        auto& triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;

        // ─────────────────────────────────────────────────────────────
        // 核心修复：将其更改为 R32G32B32_SFLOAT，彻底解决光追顶点格式校验报错
        // ─────────────────────────────────────────────────────────────
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = vertexAddress_;
        triangles.vertexStride = sizeof(Vertex); // 保持 48 字节 stride
        triangles.maxVertex = vertexCount_ > 0 ? (vertexCount_ - 1) : 0;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = indexAddress_;

        return geometry;
    }

    /**
     * @brief 获取适配 KHR 光追构建对应的区间范围描述
     */
    [[nodiscard]] VkAccelerationStructureBuildRangeInfoKHR getASBuildRange() const {
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = indexCount_ / 3; // 三角形总数
        range.primitiveOffset = 0;
        range.firstVertex = 0;
        range.transformOffset = 0;
        return range;
    }

    // ─────────────────────────────────────────────────────────────
    // 访问器
    // ─────────────────────────────────────────────────────────────
    [[nodiscard]] VkBuffer getVertexBuffer() const { return vertexBuffer_.get(); }
    [[nodiscard]] VkBuffer getIndexBuffer()  const { return indexBuffer_.get(); }
    [[nodiscard]] uint32_t getVertexCount()  const { return vertexCount_; }
    [[nodiscard]] uint32_t getIndexCount()   const { return indexCount_; }

    [[nodiscard]] VkDeviceAddress getVertexAddress() const { return vertexAddress_; }
    [[nodiscard]] VkDeviceAddress getIndexAddress()  const { return indexAddress_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    Buffer vertexBuffer_;
    uint32_t vertexCount_ = 0;
    Buffer indexBuffer_;
    uint32_t indexCount_ = 0;

    VkDeviceAddress vertexAddress_ = 0;
    VkDeviceAddress indexAddress_ = 0;

    void retrieveDeviceAddresses() {
        if (device_ == VK_NULL_HANDLE) return;

        VkBufferDeviceAddressInfo vertexAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        vertexAddrInfo.buffer = vertexBuffer_.get();
        vertexAddress_ = vkGetBufferDeviceAddress(device_, &vertexAddrInfo);

        VkBufferDeviceAddressInfo indexAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        indexAddrInfo.buffer = indexBuffer_.get();
        indexAddress_ = vkGetBufferDeviceAddress(device_, &indexAddrInfo);
    }

    void cleanup() {
        vertexBuffer_ = {};
        indexBuffer_ = {};
        device_ = VK_NULL_HANDLE;
        vertexCount_ = 0;
        indexCount_ = 0;
        vertexAddress_ = 0;
        indexAddress_ = 0;
    }
};

} // namespace StuCanvas::Vulkan