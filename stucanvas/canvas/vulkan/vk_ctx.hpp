// stucanvas/canvas/vulkan/vk_ctx.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vector>
#include <set>
#include <string>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <memory>

namespace StuCanvas::Vulkan
{
    /**
     * @brief 异步队列信息
     */
    struct QueueInfo {
        VkQueue handle = VK_NULL_HANDLE;
        uint32_t familyIndex = 0xFFFFFFFF;
    };

    /**
     * @brief 逻辑设备（含关联物理设备与异步队列）的独立 RAII 封装类
     */
    class VulkanDevice {
    public:
        VulkanDevice() = default;

        VulkanDevice(VkDevice device, VkPhysicalDevice physicalDevice)
            : device_(device), physicalDevice_(physicalDevice) {}

        ~VulkanDevice() {
            destroy();
        }

        // 禁止拷贝，保证独占性
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // 允许移动语义
        VulkanDevice(VulkanDevice&& other) noexcept {
            *this = std::move(other);
        }

        VulkanDevice& operator=(VulkanDevice&& other) noexcept {
            if (this != &other) {
                destroy();
                device_ = other.device_;
                physicalDevice_ = other.physicalDevice_;
                graphicsQueue_ = other.graphicsQueue_;
                presentQueue_ = other.presentQueue_;
                computeQueue_ = other.computeQueue_;
                transferQueue_ = other.transferQueue_;
                other.device_ = VK_NULL_HANDLE;
            }
            return *this;
        }

        VkDevice get() const { return device_; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice_; }

        const QueueInfo& getGraphicsQueue() const { return graphicsQueue_; }
        const QueueInfo& getPresentQueue() const { return presentQueue_; }
        const QueueInfo& getComputeQueue() const { return computeQueue_; }
        const QueueInfo& getTransferQueue() const { return transferQueue_; }

        void destroy() {
            if (device_ != VK_NULL_HANDLE) {
                vkDestroyDevice(device_, nullptr);
                device_ = VK_NULL_HANDLE;
            }
        }

    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

        QueueInfo graphicsQueue_;
        QueueInfo presentQueue_;
        QueueInfo computeQueue_;
        QueueInfo transferQueue_;

        friend class VulkanContext;
    };

    /**
     * @brief Vulkan 实例及上下文创建配置选项
     */
    struct VulkanContextConfig {
        std::string appName = "StuCanvas";
        uint32_t apiVersion = VK_API_VERSION_1_3;
        bool enableValidation = true;

        // 调用端自由配置的扩展
        std::vector<const char*> requiredInstanceExtensions;
        std::vector<const char*> requiredDeviceExtensions;
        std::vector<const char*> requiredValidationLayers = { "VK_LAYER_KHRONOS_validation" };
    };

    /**
     * @brief 整个 Vulkan 运行期上下文环境的 RAII 封装
     */
    class VulkanContext {
    public:
        VulkanContext() = default;

        ~VulkanContext() {
            cleanup();
        }

        // 禁止拷贝
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        // 允许移动
        VulkanContext(VulkanContext&& other) noexcept {
            *this = std::move(other);
        }

        VulkanContext& operator=(VulkanContext&& other) noexcept {
            if (this != &other) {
                cleanup();
                instance_ = other.instance_;
                debugMessenger_ = other.debugMessenger_;
                surface_ = other.surface_;
                physicalDevices_ = std::move(other.physicalDevices_);
                logicalDevices_ = std::move(other.logicalDevices_);
                validationEnabled_ = other.validationEnabled_;

                other.instance_ = VK_NULL_HANDLE;
                other.debugMessenger_ = VK_NULL_HANDLE;
                other.surface_ = VK_NULL_HANDLE;
            }
            return *this;
        }

        /**
         * @brief 依据调用端传入的配置项初始化 Vulkan 实例与物理显卡列表
         */
        void initInstance(const VulkanContextConfig& config) {
            validationEnabled_ = config.enableValidation;

            if (validationEnabled_ && !checkValidationLayerSupport(config.requiredValidationLayers)) {
                throw std::runtime_error("VulkanContext: Validation layers requested but not supported.");
            }

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = config.appName.c_str();
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "StuCanvas";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = config.apiVersion;

            // 拷贝一份临时扩展列表，进行自动补齐，防止修改只读入参 config
            std::vector<const char*> instanceExtensions = config.requiredInstanceExtensions;

            // 安全机制：如果开启了 Validation 层，自动检测并补齐所需的 VK_EXT_debug_utils 实例扩展
            if (validationEnabled_) {
                bool hasDebugUtils = false;
                for (const auto& ext : instanceExtensions) {
                    if (std::strcmp(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                        hasDebugUtils = true;
                        break;
                    }
                }
                if (!hasDebugUtils) {
                    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                }
            }

            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
            createInfo.ppEnabledExtensionNames = instanceExtensions.data();

            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
            if (validationEnabled_) {
                createInfo.enabledLayerCount = static_cast<uint32_t>(config.requiredValidationLayers.size());
                createInfo.ppEnabledLayerNames = config.requiredValidationLayers.data();
                populateDebugMessengerCreateInfo(debugCreateInfo);
                createInfo.pNext = &debugCreateInfo;
            } else {
                createInfo.enabledLayerCount = 0;
                createInfo.pNext = nullptr;
            }

            if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
                throw std::runtime_error("VulkanContext: Failed to create Vulkan instance.");
            }

            if (validationEnabled_) {
                setupDebugMessenger();
            }

            // 获取系统中所有的物理显卡列表
            enumeratePhysicalDevices();
        }

        /**
         * @brief 绑定窗口并建立 Vulkan 表面 (非无头模式)
         */
        void initSurface(SDL_Window* window) {
            if (instance_ == VK_NULL_HANDLE) {
                throw std::runtime_error("VulkanContext: Cannot create surface before instance initialization.");
            }
            if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
                throw std::runtime_error("VulkanContext: Failed to create SDL Vulkan surface.");
            }
        }

        /**
         * @brief 支持调用端自定义过滤规则来刷选可用的物理显卡
         */
        void filterPhysicalDevices(const std::function<bool(VkPhysicalDevice)>& predicate) {
            std::vector<VkPhysicalDevice> filtered;
            for (const auto& device : physicalDevices_) {
                if (predicate(device)) {
                    filtered.push_back(device);
                }
            }
            physicalDevices_ = filtered;
            if (physicalDevices_.empty()) {
                throw std::runtime_error("VulkanContext: No physical devices matched the filter predicate.");
            }
        }

        /**
         * @brief 在指定的物理设备上实例化独立的 RAII 逻辑设备
         * @param physicalDeviceIndex 选中的 physicalDevices_ vector 下标
         * @param requiredExtensions 需要为该逻辑设备开启的扩展
         * @param pNextFeatures 可选的 1.1/1.2/1.3 链指针（如其它定制化的物理器件功能）
         */
        void createLogicalDevice(uint32_t physicalDeviceIndex,
                                 const std::vector<const char*>& requiredExtensions,
                                 const void* pNextFeatures = nullptr) {
            if (physicalDeviceIndex >= physicalDevices_.size()) {
                throw std::out_of_range("VulkanContext: Physical device index out of range.");
            }

            VkPhysicalDevice physDev = physicalDevices_[physicalDeviceIndex];

            // A. 探测所有的队列家族
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyCount, queueFamilies.data());

            uint32_t graphicsFamily = 0xFFFFFFFF;
            uint32_t presentFamily = 0xFFFFFFFF;
            uint32_t computeFamily = 0xFFFFFFFF;
            uint32_t transferFamily = 0xFFFFFFFF;

            for (uint32_t i = 0; i < queueFamilyCount; ++i) {
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    if (graphicsFamily == 0xFFFFFFFF) graphicsFamily = i;
                }
                if (surface_ != VK_NULL_HANDLE) {
                    VkBool32 presentSupport = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(physDev, i, surface_, &presentSupport);
                    if (presentSupport) {
                        if (presentFamily == 0xFFFFFFFF) presentFamily = i;
                    }
                }
                if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        computeFamily = i;
                    } else if (computeFamily == 0xFFFFFFFF) {
                        computeFamily = i;
                    }
                }
                if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
                    if (!(queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
                        transferFamily = i;
                    } else if (transferFamily == 0xFFFFFFFF) {
                        transferFamily = i;
                    }
                }
            }

            // B. 队列参数合并去重
            std::set<uint32_t> uniqueFamilies;
            if (graphicsFamily != 0xFFFFFFFF) uniqueFamilies.insert(graphicsFamily);
            if (presentFamily != 0xFFFFFFFF)  uniqueFamilies.insert(presentFamily);
            if (computeFamily != 0xFFFFFFFF)  uniqueFamilies.insert(computeFamily);
            if (transferFamily != 0xFFFFFFFF) uniqueFamilies.insert(transferFamily);

            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
            float queuePriority = 1.0f;
            for (uint32_t familyIndex : uniqueFamilies) {
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = familyIndex;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;
                queueCreateInfos.push_back(queueCreateInfo);
            }

            // 核心修改 1：在逻辑设备内部强制开启 Vulkan 1.3 中的 synchronization2 特征
            VkPhysicalDeviceVulkan13Features vulkan13Features{};
            vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            vulkan13Features.synchronization2 = VK_TRUE; // 强制启用，解决 vkQueueSubmit2 报错

            // 核心修改 2：强制配置 Vulkan 1.1 中的 shaderDrawParameters 特性 (与 1.3 链合)
            VkPhysicalDeviceVulkan11Features vulkan11Features{};
            vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            vulkan11Features.shaderDrawParameters = VK_TRUE;

            // 链式组装：1.3 Features -> 1.1 Features -> 外部传参（如有）
            vulkan13Features.pNext = &vulkan11Features;
            vulkan11Features.pNext = const_cast<void*>(pNextFeatures);

            // C. 逻辑设备配置
            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext = &vulkan13Features; // 将组装好的配置特征链绑定至逻辑设备
            createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
            createInfo.pQueueCreateInfos = queueCreateInfos.data();

            VkPhysicalDeviceFeatures deviceFeatures{};
            deviceFeatures.sampleRateShading = VK_TRUE;
            createInfo.pEnabledFeatures = &deviceFeatures;

            createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
            createInfo.ppEnabledExtensionNames = requiredExtensions.data();

            VkDevice device = VK_NULL_HANDLE;
            if (vkCreateDevice(physDev, &createInfo, nullptr, &device) != VK_SUCCESS) {
                throw std::runtime_error("VulkanContext: Failed to create logical device.");
            }

            // D. 安全地包装入 RAII 的 VulkanDevice 智能指针中
            auto vulkanDevice = std::make_unique<VulkanDevice>(device, physDev);

            if (graphicsFamily != 0xFFFFFFFF) {
                vulkanDevice->graphicsQueue_.familyIndex = graphicsFamily;
                vkGetDeviceQueue(device, graphicsFamily, 0, &vulkanDevice->graphicsQueue_.handle);
            }
            if (presentFamily != 0xFFFFFFFF) {
                vulkanDevice->presentQueue_.familyIndex = presentFamily;
                vkGetDeviceQueue(device, presentFamily, 0, &vulkanDevice->presentQueue_.handle);
            }
            if (computeFamily != 0xFFFFFFFF) {
                vulkanDevice->computeQueue_.familyIndex = computeFamily;
                vkGetDeviceQueue(device, computeFamily, 0, &vulkanDevice->computeQueue_.handle);
            }
            if (transferFamily != 0xFFFFFFFF) {
                vulkanDevice->transferQueue_.familyIndex = transferFamily;
                vkGetDeviceQueue(device, transferFamily, 0, &vulkanDevice->transferQueue_.handle);
            }

            logicalDevices_.push_back(std::move(vulkanDevice));
        }

        // ====================================================================
        // 6. 属性访问器 (Accessors)
        // ====================================================================

        VkInstance getInstance() const { return instance_; }
        VkSurfaceKHR getSurface() const { return surface_; }

        const std::vector<VkPhysicalDevice>& getPhysicalDevices() const { return physicalDevices_; }

        // 外部通过 unique_ptr 的只读指针数组进行安全查询
        const std::vector<std::unique_ptr<VulkanDevice>>& getLogicalDevices() const { return logicalDevices_; }

        /**
         * @brief 获取首选的主逻辑设备上下文
         */
        const VulkanDevice& getPrimaryDevice() const {
            if (logicalDevices_.empty() || !logicalDevices_[0]) {
                throw std::runtime_error("VulkanContext: No primary device created.");
            }
            return *logicalDevices_[0];
        }

    private:
        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;

        std::vector<VkPhysicalDevice> physicalDevices_;

        // 所有逻辑设备通过 RAII 独占智能指针容器存储，杜绝任何中途泄漏
        std::vector<std::unique_ptr<VulkanDevice>> logicalDevices_;

        bool validationEnabled_ = false;

        void enumeratePhysicalDevices() {
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(instance_, &count, nullptr);
            if (count == 0) {
                throw std::runtime_error("VulkanContext: No Vulkan-compatible physical devices found.");
            }
            physicalDevices_.resize(count);
            vkEnumeratePhysicalDevices(instance_, &count, physicalDevices_.data());
        }

        bool checkValidationLayerSupport(const std::vector<const char*>& requiredLayers) {
            uint32_t count;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> available(count);
            vkEnumerateInstanceLayerProperties(&count, available.data());

            for (const char* layerName : requiredLayers) {
                bool found = false;
                for (const auto& layer : available) {
                    if (std::strcmp(layerName, layer.layerName) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }

        static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
            std::memset(&createInfo, 0, sizeof(createInfo));
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            createInfo.pfnUserCallback = debugCallback;
        }

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData) {
            std::cerr << "[Validation]: " << pCallbackData->pMessage << std::endl;
            return VK_FALSE;
        }

        void setupDebugMessenger() {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            populateDebugMessengerCreateInfo(createInfo);
            if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
                throw std::runtime_error("VulkanContext: Failed to setup debug messenger.");
            }
        }

        static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
            auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
            if (func != nullptr) {
                return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
            }
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) {
                func(instance, debugMessenger, pAllocator);
            }
        }

        /**
         * @brief 严格的反向生命周期自动销毁流程
         */
        void cleanup() {
            // 1. vector 清空会自动触发其中每个 unique_ptr<VulkanDevice> 的析构，进而安全销毁各逻辑设备
            logicalDevices_.clear();

            // 2. 销毁窗口表面
            if (surface_ != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance_, surface_, nullptr);
                surface_ = VK_NULL_HANDLE;
            }

            // 3. 销毁调试接口
            if (debugMessenger_ != VK_NULL_HANDLE) {
                DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
                debugMessenger_ = VK_NULL_HANDLE;
            }

            // 4. 最后销毁实例本身
            if (instance_ != VK_NULL_HANDLE) {
                vkDestroyInstance(instance_, nullptr);
                instance_ = VK_NULL_HANDLE;
            }
        }
    };
} // namespace StuCanvas::Vulkan