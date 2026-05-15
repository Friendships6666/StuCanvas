// stucanvas/canvas/vulkan/init.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vector>
#include <optional>
#include <set>
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace StuCanvas::Vulkan {

class VulkanInit {
public:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> videoEncodeFamily; // 新增：视频编码队列

        bool isComplete(bool headless) const {
            if (headless) {
                // Headless 模式下只需要图形和编码队列
                return graphicsFamily.has_value() && videoEncodeFamily.has_value();
            }
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    VulkanInit(const char* appName = "StuCanvas",
               uint32_t width = 800, uint32_t height = 600,
               bool enableValidation = true,
               bool headless = false) // 新增参数
        : validationEnabled_(enableValidation), headless_(headless)
    {
        // 1. 初始化 SDL (仅在非 Headless 模式下创建窗口)
        if (!headless_) {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                throw std::runtime_error("Failed to initialize SDL: " + std::string(SDL_GetError()));
            }
            window_ = SDL_CreateWindow(appName, width, height,
                                       SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
            if (!window_)
                throw std::runtime_error("Failed to create SDL window: " + std::string(SDL_GetError()));
        }

        // 2. 创建 Vulkan 实例
        createInstance(appName);
        if (validationEnabled_)
            setupDebugMessenger();

        // 3. 创建表面 (Headless 模式不需要)
        if (!headless_) {
            createSurface();
        }

        // 4. 选择物理设备
        pickPhysicalDevice();

        // 5. 创建逻辑设备并获取队列
        createLogicalDevice();
        retrieveQueues();
    }

    ~VulkanInit() {
        if (device_ != VK_NULL_HANDLE)
            vkDestroyDevice(device_, nullptr);

        if (surface_ != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(instance_, surface_, nullptr);

        if (validationEnabled_ && debugMessenger_ != VK_NULL_HANDLE)
            DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);

        if (instance_ != VK_NULL_HANDLE)
            vkDestroyInstance(instance_, nullptr);

        if (window_)
            SDL_DestroyWindow(window_);

        if (!headless_) SDL_Quit();
    }

    // 禁止拷贝
    VulkanInit(const VulkanInit&) = delete;
    VulkanInit& operator=(const VulkanInit&) = delete;

    // ── 访问器 ─────────────────────────────
    VkInstance       getInstance()       const { return instance_; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice_; }
    VkDevice         getDevice()         const { return device_; }
    VkSurfaceKHR     getSurface()        const { return surface_; }
    SDL_Window*      getWindow()         const { return window_; }
    bool             isHeadless()        const { return headless_; }

    VkQueue getGraphicsQueue()    const { return graphicsQueue_; }
    VkQueue getPresentQueue()     const { return presentQueue_; }
    VkQueue getVideoEncodeQueue() const { return videoEncodeQueue_; }

    uint32_t getGraphicsFamily()    const { return queueIndices_.graphicsFamily.value(); }
    uint32_t getPresentFamily()     const { return queueIndices_.presentFamily.value_or(0); }
    uint32_t getVideoEncodeFamily() const { return queueIndices_.videoEncodeFamily.value_or(0); }

    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueIndices_; }

private:
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;

    VkQueue graphicsQueue_    = VK_NULL_HANDLE;
    VkQueue presentQueue_     = VK_NULL_HANDLE;
    VkQueue videoEncodeQueue_ = VK_NULL_HANDLE;

    QueueFamilyIndices queueIndices_;
    bool validationEnabled_;
    bool headless_;

    // ── Instance 创建 ─────────────────────
    void createInstance(const char* appName) {
        if (validationEnabled_ && !checkValidationLayerSupport())
            throw std::runtime_error("Validation layers requested, but not available");

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = appName;
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "StuCanvas";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        auto extensions = getRequiredInstanceExtensions();
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (validationEnabled_) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
            createInfo.ppEnabledLayerNames = validationLayers_.data();
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance");
    }


    void createSurface() {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw std::runtime_error("Failed to create surface: " + std::string(SDL_GetError()));
    }

    // ── 物理设备选择 ──────────────────────
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) throw std::runtime_error("No Vulkan GPU found");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (const auto& dev : devices) {
            if (isDeviceSuitable(dev)) {
                physicalDevice_ = dev;
                break;
            }
        }
        if (physicalDevice_ == VK_NULL_HANDLE) throw std::runtime_error("No suitable GPU for 8K Export found");
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsOk = checkDeviceExtensionSupport(device);

        // 如果是导出模式，我们需要检查编码功能是否支持
        return indices.isComplete(headless_) && extensionsOk;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            // 图形队列
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.graphicsFamily = i;

            // 呈现队列 (非 Headless 模式必需)
            if (!headless_) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
                if (presentSupport) indices.presentFamily = i;
            }

            // 视频编码队列 (所有模式建议拥有，导出模式必需)
            // 需要包含 VIDEO_ENCODE_BIT_KHR (0x00000040)
            if (queueFamilies[i].queueFlags & 0x00000040) {
                indices.videoEncodeFamily = i;
            }

            if (indices.isComplete(headless_)) break;
        }
        queueIndices_ = indices;
        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        std::vector<const char*> requiredExtensions;
        if (!headless_) {
            requiredExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        } else {
            // Headless 导出所需的关键视频扩展
            requiredExtensions.push_back("VK_KHR_video_queue");
            requiredExtensions.push_back("VK_KHR_video_encode_queue");
            requiredExtensions.push_back("VK_EXT_video_encode_av1");
        }

        std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
        for (const auto& ext : available)
            required.erase(ext.extensionName);

        return required.empty();
    }

    // ── 逻辑设备创建 ──────────────────────
    void createLogicalDevice() {
        std::set<uint32_t> uniqueFamilies = { queueIndices_.graphicsFamily.value() };
        if (queueIndices_.presentFamily.has_value()) uniqueFamilies.insert(queueIndices_.presentFamily.value());
        if (queueIndices_.videoEncodeFamily.has_value()) uniqueFamilies.insert(queueIndices_.videoEncodeFamily.value());

        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        float priority = 1.0f;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo qInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            qInfo.queueFamilyIndex = family;
            qInfo.queueCount = 1;
            qInfo.pQueuePriorities = &priority;
            queueInfos.push_back(qInfo);
        }
        VkPhysicalDeviceShaderDrawParametersFeatures drawParamsFeatures{};
        drawParamsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        drawParamsFeatures.shaderDrawParameters = VK_TRUE;

        // 启用功能
        VkPhysicalDeviceFeatures features{};
        features.sampleRateShading = VK_TRUE;

        // 视频编码需要额外的链式结构
        VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos    = queueInfos.data();
        createInfo.pEnabledFeatures     = &features;
        createInfo.pNext = &drawParamsFeatures;
        std::vector<const char*> extensions;
        if (!headless_) {
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        } else {
            extensions.push_back("VK_KHR_video_queue");
            extensions.push_back("VK_KHR_video_encode_queue");
            extensions.push_back("VK_EXT_video_encode_av1");
        }
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();


        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create logical device");
    }

    void retrieveQueues() {
        vkGetDeviceQueue(device_, queueIndices_.graphicsFamily.value(), 0, &graphicsQueue_);
        if (queueIndices_.presentFamily.has_value())
            vkGetDeviceQueue(device_, queueIndices_.presentFamily.value(), 0, &presentQueue_);
        if (queueIndices_.videoEncodeFamily.has_value())
            vkGetDeviceQueue(device_, queueIndices_.videoEncodeFamily.value(), 0, &videoEncodeQueue_);
    }

    std::vector<const char*> getRequiredInstanceExtensions() const {
        std::vector<const char*> extensions;
        if (!headless_) {
            uint32_t sdlExtCount = 0;
            const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
            for(uint32_t i=0; i<sdlExtCount; ++i) extensions.push_back(sdlExts[i]);
        } else {
            // Headless 模式的基础实例扩展
            // 如果后续需要使用 VkVideoSession，可能需要一些特定的外部内存扩展
        }

        if (validationEnabled_)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return extensions;
    }

    void setupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);
        if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS)
            throw std::runtime_error("Failed to set up debug messenger");
    }

    bool checkValidationLayerSupport() const {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers_) {
            bool found = false;
            for (const auto& layer : availableLayers)
                if (strcmp(layerName, layer.layerName) == 0) { found = true; break; }
            if (!found) return false;
        }
        return true;
    }

    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT s, VkDebugUtilsMessageTypeFlagsEXT t, const VkDebugUtilsMessengerCallbackDataEXT* p, void* u) {
        std::cerr << "Validation: " << p->pMessage << std::endl;
        return VK_FALSE;
    }

    static VkResult CreateDebugUtilsMessengerEXT(VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT* p, const VkAllocationCallbacks* a, VkDebugUtilsMessengerEXT* m) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
        return func ? func(inst, p, a, m) : VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    static void DestroyDebugUtilsMessengerEXT(VkInstance inst, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks* a) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(inst, m, a);
    }

    const std::vector<const char*> validationLayers_ = { "VK_LAYER_KHRONOS_validation" };
};

} // namespace StuCanvas::Vulkan