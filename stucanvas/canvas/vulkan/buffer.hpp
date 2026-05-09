// stucanvas/canvas/vulkan/buffer.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <cstring>
#include <vector>

namespace StuCanvas::Vulkan {

/**
 * @brief 查找合适的内存类型索引。
 *
 * @param memoryTypeBits 需求的内存类型位掩码（从 VkMemoryRequirements 获取）
 * @param properties     期望的内存属性（如 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT）
 * @return uint32_t 内存类型索引
 */
inline uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t memoryTypeBits,
                               VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

/**
 * @brief RAII 封装的 Vulkan 缓冲区。
 *
 * 支持主机可见（staging）或设备本地缓冲区的创建，
 * 以及数据上传和拷贝。
 */
class Buffer {
public:
    Buffer() = default;

    /**
     * @brief 创建缓冲区并分配绑定内存。
     *
     * @param device         逻辑设备
     * @param physicalDevice 物理设备（用于查询内存属性）
     * @param size           缓冲区大小（字节）
     * @param usage          缓冲区使用标志
     * @param properties     期望的内存属性
     * @return Buffer 对象
     */
    static Buffer Create(VkDevice device,
                         VkPhysicalDevice physicalDevice,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties) {
        Buffer buffer;
        buffer.device_ = device;

        // 创建缓冲区
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer.buffer_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer");
        }

        // 获取内存需求
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, buffer.buffer_, &memReq);

        // 分配内存
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &buffer.memory_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate buffer memory");
        }

        // 绑定内存
        vkBindBufferMemory(device, buffer.buffer_, buffer.memory_, 0);

        return buffer;
    }

    ~Buffer() {
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory_, nullptr);
        }
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
        }
    }

    // 禁止拷贝
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // 移动构造
    Buffer(Buffer&& other) noexcept
        : device_(other.device_), buffer_(other.buffer_), memory_(other.memory_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
            if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer_, nullptr);
            device_ = other.device_;
            buffer_ = other.buffer_;
            memory_ = other.memory_;
            other.buffer_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // 访问器
    VkBuffer getBuffer() const { return buffer_; }
    VkDeviceMemory getMemory() const { return memory_; }

    /**
     * @brief 映射缓冲区内存到主机地址空间。
     *
     * 仅当缓冲区使用 HOST_VISIBLE 属性创建时有效。
     * @param ppData 输出指针，指向映射后的内存区域
     */
    void mapMemory(void** ppData) {
        if (vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, ppData) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map buffer memory");
        }
    }

    /**
     * @brief 解除内存映射。
     */
    void unmapMemory() {
        vkUnmapMemory(device_, memory_);
    }

    /**
     * @brief 直接上传数据到主机可见缓冲区（内部映射+拷贝+解映射）。
     *
     * @param data 源数据指针
     * @param size 数据大小（字节），不应超过缓冲区大小
     */
    void uploadData(const void* data, VkDeviceSize size) {
        void* mapped;
        mapMemory(&mapped);
        std::memcpy(mapped, data, static_cast<size_t>(size));
        unmapMemory();
    }

    /**
     * @brief 将当前缓冲区的内容拷贝到另一个缓冲区。
     *
     * 要求当前缓冲区有 transfer src 使用标志，目标有 transfer dst 使用标志。
     * @param dstBuffer  目标缓冲区
     * @param size       拷贝大小（字节）
     * @param commandPool 命令池（用于分配临时命令缓冲区）
     * @param queue      提交拷贝命令的队列
     */
    void copyTo(VkBuffer dstBuffer, VkDeviceSize size, VkCommandPool commandPool, VkQueue queue) const {
        // 分配临时命令缓冲区
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &allocInfo, &cmd);

        // 开始录制
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, buffer_, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        // 提交
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        // 清理临时命令缓冲区
        vkFreeCommandBuffers(device_, commandPool, 1, &cmd);
    }

    /**
     * @brief 一键创建设备本地缓冲区并上传数据。
     *
     * 内部创建 staging 缓冲区，拷贝数据后销毁 staging。
     *
     * @param device         逻辑设备
     * @param physicalDevice 物理设备
     * @param commandPool    命令池
     * @param queue          图形/计算队列
     * @param data           源数据指针
     * @param size           数据大小
     * @param usage          缓冲区用途（自动添加 VK_BUFFER_USAGE_TRANSFER_DST_BIT）
     * @return Buffer 设备本地缓冲区对象
     */
    static Buffer CreateAndUpload(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  VkCommandPool commandPool,
                                  VkQueue queue,
                                  const void* data,
                                  VkDeviceSize size,
                                  VkBufferUsageFlags usage) {
        // 创建 staging buffer
        Buffer staging = Buffer::Create(
            device, physicalDevice, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.uploadData(data, size);

        // 创建设备本地缓冲区
        Buffer deviceBuffer = Buffer::Create(
            device, physicalDevice, size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // 拷贝 staging -> device local
        staging.copyTo(deviceBuffer.buffer_, size, commandPool, queue);

        // staging 自动销毁，返回设备缓冲区
        return deviceBuffer;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

} // namespace StuCanvas::Vulkan