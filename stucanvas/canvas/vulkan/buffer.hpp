// stucanvas/canvas/vulkan/buffer.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> // 引入 VMA 头文件
#include <stdexcept>
#include <cstring>
#include <vector>

namespace StuCanvas::Vulkan {

/**
 * @brief RAII 封装的 Vulkan Buffer 缓冲区（使用 VMA 进行内存自动分配与绑定）
 */
class Buffer {
public:
    Buffer() = default;

    /**
     * @brief 构造函数：利用 VMA 自动创建缓冲区并分配内存
     * @param device Vulkan 逻辑设备句柄（供命令提交与同步操作使用）
     * @param allocator VMA 分配器实例
     * @param size 缓冲区大小
     * @param usage 缓冲区用途标志
     * @param properties 内存属性（通过 VMA 自动转换匹配）
     * @param pNext 扩展结构体指针
     */
    Buffer(VkDevice device,
           VmaAllocator allocator,
           VkDeviceSize size,
           VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties,
           const void* pNext = nullptr)
        : device_(device), allocator_(allocator)
    {
        // 1. 配置缓冲区句柄创建信息
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = pNext;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // 2. 配置 VMA 内存分配请求
        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO; // VMA 3.0+ 推荐使用的自动内存类型匹配机制
        allocCreateInfo.requiredFlags = properties;

        // 如果用户需要 Host Visible（CPU可读写）
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            // 如果可能被用作数据回读 (Transfer Destination)，使用 RANDOM 访问
            if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
                allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT; // 持久化映射
            } else {
                // Staging / Uniform 缓冲区等顺序写入，使用 SEQUENTIAL_WRITE 提升总线带宽写入性能
                allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT; // 持久化映射
            }
        }

        // 3. 一键创建缓冲区、分配显存并执行绑定
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocCreateInfo, &buffer_, &allocation_, &allocationInfo_) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to create VkBuffer with VMA.");
        }
    }

    /**
     * @brief 高级构造函数：支持外部直接传入原生的 VmaAllocationCreateInfo 结构体
     */
    Buffer(VkDevice device,
           VmaAllocator allocator,
           VkDeviceSize size,
           VkBufferUsageFlags usage,
           const VmaAllocationCreateInfo& allocCreateInfo,
           const void* pNext = nullptr)
        : device_(device), allocator_(allocator)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = pNext;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocCreateInfo, &buffer_, &allocation_, &allocationInfo_) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to create VkBuffer with VMA.");
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
        : device_(other.device_),
          allocator_(other.allocator_),
          buffer_(other.buffer_),
          allocation_(other.allocation_),
          allocationInfo_(other.allocationInfo_)
    {
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.allocationInfo_ = {};
    }

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            allocator_ = other.allocator_;
            buffer_ = other.buffer_;
            allocation_ = other.allocation_;
            allocationInfo_ = other.allocationInfo_;

            other.buffer_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.allocator_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
            other.allocationInfo_ = {};
        }
        return *this;
    }

    // 静态构造方法
    static Buffer Create(VkDevice device,
                         VmaAllocator allocator,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         const void* pNext = nullptr) {
        return Buffer(device, allocator, size, usage, properties, pNext);
    }

    static Buffer Create(VkDevice device,
                         VmaAllocator allocator,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         const VmaAllocationCreateInfo& allocCreateInfo,
                         const void* pNext = nullptr) {
        return Buffer(device, allocator, size, usage, allocCreateInfo, pNext);
    }

    // ====================================================================
    // 内存控制
    // ====================================================================

    void* map() {
        // 如果在创建时已经启用了持久化映射，直接返回已缓存在 allocationInfo_ 里的数据指针，降低 CPU 开销
        if (allocationInfo_.pMappedData != nullptr) {
            return allocationInfo_.pMappedData;
        }
        void* data = nullptr;
        if (vmaMapMemory(allocator_, allocation_, &data) != VK_SUCCESS) {
            throw std::runtime_error("Buffer: Failed to map memory.");
        }
        return data;
    }

    void unmap() {
        if (allocationInfo_.pMappedData == nullptr) {
            vmaUnmapMemory(allocator_, allocation_);
        }
    }

    void mapMemory(void** ppData) {
        if (allocationInfo_.pMappedData != nullptr) {
            *ppData = allocationInfo_.pMappedData;
        } else {
            if (vmaMapMemory(allocator_, allocation_, ppData) != VK_SUCCESS) {
                throw std::runtime_error("Buffer: Failed to map memory.");
            }
        }
    }

    void unmapMemory() {
        if (allocationInfo_.pMappedData == nullptr) {
            vmaUnmapMemory(allocator_, allocation_);
        }
    }

    void uploadData(const void* data, VkDeviceSize size) {
        void* mapped = nullptr;
        mapMemory(&mapped);
        std::memcpy(mapped, data, static_cast<size_t>(size));
        unmapMemory();
    }

    /**
     * @brief 手动刷新（Flush）非 Coherent 的 CPU 写入缓存，使其在 GPU 侧可见
     */
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
        vmaFlushAllocation(allocator_, allocation_, offset, size);
    }

    /**
     * @brief 使非 Coherent 的 GPU 写入内存在 CPU 侧失效（Invalidate）重新加载
     */
    void invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
        vmaInvalidateAllocation(allocator_, allocation_, offset, size);
    }

    /**
     * @brief 利用物理同步栅栏（Fence）将本缓冲区数据异步拷贝至另一缓冲区
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
                                  VmaAllocator allocator,
                                  VkCommandPool commandPool,
                                  VkQueue queue,
                                  const void* data,
                                  VkDeviceSize size,
                                  VkBufferUsageFlags usage) {
        // 1. 创建 Staging Buffer (Host Visible)
        // 内部已经利用 VMA 映射标志自动开启了 Persistent Map 快速填充
        Buffer staging = Buffer::Create(
            device, allocator, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.uploadData(data, size);

        // 2. 创建 Device Local 缓冲区
        Buffer deviceBuffer = Buffer::Create(
            device, allocator, size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // 3. GPU 侧异步拷贝
        staging.copyTo(deviceBuffer.buffer_, size, commandPool, queue);

        return deviceBuffer;
    }

    // ====================================================================
    // 句柄访问器
    // ====================================================================
    [[nodiscard]] VkBuffer get() const { return buffer_; }
    [[nodiscard]] VkBuffer getBuffer() const { return buffer_; }
    [[nodiscard]] VmaAllocation getAllocation() const { return allocation_; }
    [[nodiscard]] const VmaAllocationInfo& getAllocationInfo() const { return allocationInfo_; }

    /**
     * @brief 兼容原有接口，获取底层实际物理分配的设备内存
     */
    [[nodiscard]] VkDeviceMemory getMemory() const { return allocationInfo_.deviceMemory; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo_{};

    void cleanup() {
        if (allocator_ != VK_NULL_HANDLE) {
            if (buffer_ != VK_NULL_HANDLE) {
                // VMA 会一并释放 allocation 对应的显存、销毁 VkBuffer，并清理可能存在的 mapped 指针
                vmaDestroyBuffer(allocator_, buffer_, allocation_);
                buffer_ = VK_NULL_HANDLE;
                allocation_ = VK_NULL_HANDLE;
            }
            allocator_ = VK_NULL_HANDLE;
        }
        device_ = VK_NULL_HANDLE;
    }
};

} // namespace StuCanvas::Vulkan