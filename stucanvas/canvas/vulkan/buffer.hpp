// stucanvas/canvas/vulkan/buffer.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <cstring>
#include <vector>

namespace StuCanvas::Vulkan {

/**
 * @brief 查找合适的内存类型索引
 */
inline uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t memoryTypeBits,
                               VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    // 第一轮：严格匹配首选属性
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    // 第二轮：回退至任何支持该缓冲要求的内存类型 (例如硬件硬解/硬编专用显存区)
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (memoryTypeBits & (1 << i)) return i;
    }

    throw std::runtime_error("Buffer: Failed to find suitable memory type.");
}

/**
 * @brief RAII 封装的 Vulkan Contiguous Buffer（缓冲区与绑定的物理内存）
 */
class Buffer {
public:
    Buffer() = default;

    /**
     * @brief 构造函数：创建缓冲区并分配绑定内存
     * @param pNext 扩展结构体指针 (例如用于视频流或特定硬件扩展配置)
     */
    Buffer(VkDevice device,
           VkPhysicalDevice physicalDevice,
           VkDeviceSize size,
           VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties,
           const void* pNext = nullptr) : device_(device)
    {
        // 1. 创建缓冲区句柄
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = pNext;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to create VkBuffer.");
        }

        // 2. 检索内存需求
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, buffer_, &memReq);

        // 3. 分配显存/主机可见内存
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
            throw std::runtime_error("Buffer: Failed to allocate VkDeviceMemory.");
        }

        // 4. 原子绑定
        if (vkBindBufferMemory(device, buffer_, memory_, 0) != VK_SUCCESS) {
            cleanup();
            throw std::runtime_error("Buffer: Failed to bind memory to buffer.");
        }
    }

    ~Buffer() {
        cleanup();
    }

    // 禁止拷贝，保证资源唯一性
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // 完备的移动语义
    Buffer(Buffer&& other) noexcept
        : device_(other.device_), buffer_(other.buffer_), memory_(other.memory_)
    {
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            buffer_ = other.buffer_;
            memory_ = other.memory_;
            other.buffer_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 静态构造方法，直接复用主构造函数
    static Buffer Create(VkDevice device,
                         VkPhysicalDevice physicalDevice,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         const void* pNext = nullptr) {
        return Buffer(device, physicalDevice, size, usage, properties, pNext);
    }

    // ====================================================================
    // 内存控制
    // ====================================================================

    void* map() {
        void* data = nullptr;
        if (vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &data) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to map memory.");
        }
        return data;
    }

    void unmap() {
        vkUnmapMemory(device_, memory_);
    }

    void mapMemory(void** ppData) {
        if (vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, ppData) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to map memory.");
        }
    }

    void unmapMemory() {
        vkUnmapMemory(device_, memory_);
    }

    void uploadData(const void* data, VkDeviceSize size) {
        void* mapped = nullptr;
        mapMemory(&mapped);
        std::memcpy(mapped, data, static_cast<size_t>(size));
        unmapMemory();
    }

    /**
     * @brief 利用物理同步栅栏（Fence）将本缓冲区数据精准、异步地拷贝至另一缓冲区
     */
    void copyTo(VkBuffer dstBuffer, VkDeviceSize size, VkCommandPool commandPool, VkQueue queue) const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, buffer_, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        // 使用 Fence 同步，防止 vkQueueWaitIdle 粗暴卡死队列中的其他线程任务
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        if (vkCreateFence(device_, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            vkFreeCommandBuffers(device_, commandPool, 1, &cmd);
            throw std::runtime_error("Buffer: Failed to create sync fence.");
        }

        vkQueueSubmit(queue, 1, &submitInfo, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool, 1, &cmd);
    }

    /**
     * @brief 一键在设备（GPU）本地创建缓冲区并安全上传数据
     */
    static Buffer CreateAndUpload(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  VkCommandPool commandPool,
                                  VkQueue queue,
                                  const void* data,
                                  VkDeviceSize size,
                                  VkBufferUsageFlags usage) {
        // 1. 创建 Staging Buffer (Host Visible) 并载入数据
        Buffer staging = Buffer::Create(
            device, physicalDevice, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.uploadData(data, size);

        // 2. 创建 Device Local 缓冲区
        Buffer deviceBuffer = Buffer::Create(
            device, physicalDevice, size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // 3. GPU 侧拷贝
        staging.copyTo(deviceBuffer.buffer_, size, commandPool, queue);

        return deviceBuffer;
    }

    // ====================================================================
    // 句柄访问器
    // ====================================================================
    [[nodiscard]] VkBuffer get() const { return buffer_; }
    [[nodiscard]] VkBuffer getBuffer() const { return buffer_; }
    [[nodiscard]] VkDeviceMemory getMemory() const { return memory_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            if (memory_ != VK_NULL_HANDLE) {
                vkFreeMemory(device_, memory_, nullptr);
                memory_ = VK_NULL_HANDLE;
            }
            if (buffer_ != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, buffer_, nullptr);
                buffer_ = VK_NULL_HANDLE;
            }
            device_ = VK_NULL_HANDLE;
        }
    }
};

} // namespace StuCanvas::Vulkan