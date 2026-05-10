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
    VulkanInit(const char* appName = "StuCanvas",
               uint32_t width = 800, uint32_t height = 600,
               bool enableValidation = true)
        : validationEnabled_(enableValidation)
    {
        // 1. 初始化 SDL 窗口
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error("Failed to initialize SDL: " + std::string(SDL_GetError()));
        }

        window_ = SDL_CreateWindow(appName, width, height,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window_)
            throw std::runtime_error("Failed to create SDL window: " + std::string(SDL_GetError()));

        // 2. 创建 Vulkan 实例（含调试信使，如果启用）
        createInstance(appName);
        if (validationEnabled_)
            setupDebugMessenger();

        // 3. 创建表面
        createSurface();

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

        SDL_Quit();
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

    VkQueue getGraphicsQueue() const { return graphicsQueue_; }
    VkQueue getPresentQueue()  const { return presentQueue_; }

    uint32_t getGraphicsFamily() const { return queueIndices_.graphicsFamily.value(); }
    uint32_t getPresentFamily()  const { return queueIndices_.presentFamily.value(); }

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueIndices_; }

private:
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_  = VK_NULL_HANDLE;

    QueueFamilyIndices queueIndices_;
    bool validationEnabled_;

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

    void setupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);
        if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS)
            throw std::runtime_error("Failed to set up debug messenger");
    }

    void createSurface() {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw std::runtime_error("Failed to create surface: " + std::string(SDL_GetError()));
    }

    // ── 物理设备选择 ──────────────────────
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0)
            throw std::runtime_error("No Vulkan-capable GPU found");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        for (const auto& dev : devices) {
            if (isDeviceSuitable(dev)) {
                physicalDevice_ = dev;
                break;
            }
        }

        if (physicalDevice_ == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable GPU found");
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsOk = checkDeviceExtensionSupport(device);
        return indices.isComplete() && extensionsOk;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.graphicsFamily = i;

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
            if (presentSupport)
                indices.presentFamily = i;

            if (indices.isComplete()) break;
        }

        queueIndices_ = indices;   // 保存下来供创建逻辑设备使用
        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        std::set<std::string> required(deviceExtensions_.begin(), deviceExtensions_.end());
        for (const auto& ext : available)
            required.erase(ext.extensionName);

        return required.empty();
    }

    // ── 逻辑设备创建 ──────────────────────
    void createLogicalDevice() {
        // 确保 queueIndices_ 已填充（已在 pickPhysicalDevice 中调用 findQueueFamilies）
        queueIndices_ = findQueueFamilies(physicalDevice_);

        std::set<uint32_t> uniqueFamilies = {
            queueIndices_.graphicsFamily.value(),
            queueIndices_.presentFamily.value()
        };

        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        float priority = 1.0f;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo qInfo{};
            qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qInfo.queueFamilyIndex = family;
            qInfo.queueCount = 1;
            qInfo.pQueuePriorities = &priority;
            queueInfos.push_back(qInfo);
        }

        VkPhysicalDeviceShaderDrawParametersFeatures drawParamsFeatures{};
        drawParamsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        drawParamsFeatures.shaderDrawParameters = VK_TRUE;

        VkPhysicalDeviceFeatures features{};


        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos    = queueInfos.data();
        createInfo.pEnabledFeatures     = &features;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions_.data();
        createInfo.pNext = &drawParamsFeatures;   // 关键：链接到 pNext 链

        if (validationEnabled_) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
            createInfo.ppEnabledLayerNames = validationLayers_.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
            throw std::runtime_error("Failed to create logical device");
    }

    void retrieveQueues() {
        vkGetDeviceQueue(device_, queueIndices_.graphicsFamily.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, queueIndices_.presentFamily.value(), 0, &presentQueue_);
    }

    // ── 辅助函数 ──────────────────────────
    std::vector<const char*> getRequiredInstanceExtensions() const {
        uint32_t sdlExtCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
        if (validationEnabled_)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return extensions;
    }

    bool checkValidationLayerSupport() const {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers_) {
            bool found = false;
            for (const auto& layer : availableLayers)
                if (strcmp(layerName, layer.layerName) == 0) {
                    found = true;
                    break;
                }
            if (!found) return false;
        }
        return true;
    }

    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                                   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        std::cerr << "Validation: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }

    static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
                                                  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkDebugUtilsMessengerEXT* pDebugMessenger) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        return func ? func(instance, pCreateInfo, pAllocator, pDebugMessenger)
                    : VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    static void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                               VkDebugUtilsMessengerEXT messenger,
                                               const VkAllocationCallbacks* pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(instance, messenger, pAllocator);
    }

    const std::vector<const char*> validationLayers_ = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions_ = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
};

} // namespace StuCanvas::Vulkan