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
        std::optional<uint32_t> computeFamily;    // 异步计算
        std::optional<uint32_t> videoEncodeFamily; // 视频编码

        bool isComplete(bool headless) const {
            if (headless) {
                return graphicsFamily.has_value() && computeFamily.has_value() && videoEncodeFamily.has_value();
            }
            return graphicsFamily.has_value() && presentFamily.has_value() && computeFamily.has_value() && videoEncodeFamily.has_value();
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
    VkQueue getComputeQueue()     const { return computeQueue_; }
    VkQueue getVideoEncodeQueue() const { return videoEncodeQueue_; }

    uint32_t getGraphicsFamily()    const { return queueIndices_.graphicsFamily.value(); }
    uint32_t getPresentFamily()     const { return queueIndices_.presentFamily.value_or(0); }
    uint32_t getComputeFamily()     const { return queueIndices_.computeFamily.value_or(0); }
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
    VkQueue computeQueue_ = VK_NULL_HANDLE;

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

    // ── 物理设备选择全部实现 ──────────────────────
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        std::cout << "[Vulkan] Found " << count << " physical device(s)." << std::endl;

        // 1. 第一轮筛选：优先寻找“离散显卡” (RTX 5070 Ti) 且满足功能要求
        for (const auto& dev : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                if (isDeviceSuitable(dev)) {
                    physicalDevice_ = dev;
                    std::cout << "[Vulkan] Selected Discrete GPU: " << props.deviceName << std::endl;
                    break;
                }
            }
        }

        // 2. 第二轮筛选：如果没找到独显，寻找任何满足要求的设备（如高性能集成显卡）
        if (physicalDevice_ == VK_NULL_HANDLE) {
            for (const auto& dev : devices) {
                if (isDeviceSuitable(dev)) {
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(dev, &props);
                    physicalDevice_ = dev;
                    std::cout << "[Vulkan] Fallback to GPU: " << props.deviceName << std::endl;
                    break;
                }
            }
        }

        if (physicalDevice_ == VK_NULL_HANDLE) {
            throw std::runtime_error("No suitable GPU found (Missing 8K AV1 Encode support or Graphics Queues)");
        }
    }


    bool isDeviceSuitable(VkPhysicalDevice device) {
        // A. 检查队列族支持情况
        QueueFamilyIndices indices = findQueueFamilies(device);

        // B. 检查扩展支持情况 (重点：AV1 编码扩展名探测)
        bool extensionsSupported = checkDeviceExtensionSupport(device);

        // C. 检查 8K 渲染能力（可选：检查 maxImageDimension2D 是否 >= 7680）
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        bool canHandle8K = props.limits.maxImageDimension2D >= 7680;

        return indices.isComplete(headless_) && extensionsSupported && canHandle8K;
    }

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // ---------------------------------------------------------
    // 第一步：寻找视频编码家族 (Video Encode)
    // ---------------------------------------------------------
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) {
            indices.videoEncodeFamily = i;
            // 优先选择，找到就先占住
            break;
        }
    }

    // ---------------------------------------------------------
    // 第二步：寻找渲染与呈现家族 (Graphics & Present)
    // ---------------------------------------------------------
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        bool hasGraphics = (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT);
        bool hasPresent = true;

        if (!headless_) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
            hasPresent = presentSupport;
        }

        if (hasGraphics && hasPresent) {
            indices.graphicsFamily = i;
            if (!headless_) indices.presentFamily = i;
            break;
        }
    }

    // ---------------------------------------------------------
    // 第三步：寻找计算家族 (Compute) - 智能化优先级逻辑
    // ---------------------------------------------------------

    // 优先级 A：纯计算家族 (Dedicated Compute)，这是 Async Compute 的最佳选择
    // 这种家族支持 Compute，但【不支持】Graphics
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
           !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.computeFamily = i;
            break;
        }
    }

    // 优先级 B：如果没找到纯计算家族，找一个和渲染家族【不同】的家族
    if (!indices.computeFamily.has_value()) {
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                i != indices.graphicsFamily.value_or(9999)) {
                indices.computeFamily = i;
                break;
            }
        }
    }

    // 优先级 C：最后的回退方案，使用跟渲染同一个家族
    if (!indices.computeFamily.has_value()) {
        indices.computeFamily = indices.graphicsFamily;
    }

    // 打印探测结果，方便调试
    std::cout << "[Vulkan] Queue Families Picked: " << std::endl;
    std::cout << " - Graphics: " << indices.graphicsFamily.value_or(-1) << std::endl;
    if (!headless_) std::cout << " - Present:  " << indices.presentFamily.value_or(-1) << std::endl;
    std::cout << " - Compute:  " << indices.computeFamily.value_or(-1) << " (Async Capability)" << std::endl;
    std::cout << " - VideoEnc: " << indices.videoEncodeFamily.value_or(-1) << std::endl;

    queueIndices_ = indices;
    return indices;
}

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        std::vector<const char*> required;
        if (!headless_) {
            required.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        } else {
            required.push_back("VK_KHR_video_queue");
            required.push_back("VK_KHR_video_encode_queue");
            required.push_back("VK_KHR_video_encode_av1"); // 必须是 KHR
            // 同时兼容 KHR 和 EXT 版本的 AV1 编码名
            bool hasAV1 = false;
            for (const auto& ext : available) {
                if (strcmp(ext.extensionName, "VK_KHR_video_encode_av1") == 0 ||
                    strcmp(ext.extensionName, "VK_EXT_video_encode_av1") == 0) {
                    hasAV1 = true;
                    break;
                    }
            }
            if (!hasAV1) return false;
        }

        // 添加 Vulkan-CUDA 互操作所需的共享内存与信号量同步设备扩展
        required.push_back("VK_KHR_external_memory");
        required.push_back("VK_KHR_external_semaphore");
#ifdef _WIN32
        required.push_back("VK_KHR_external_memory_win32");
        required.push_back("VK_KHR_external_semaphore_win32");
#else
        required.push_back("VK_KHR_external_memory_fd");
        required.push_back("VK_KHR_external_semaphore_fd");
#endif

        std::set<std::string> requiredSet(required.begin(), required.end());
        for (const auto& ext : available) {
            requiredSet.erase(ext.extensionName);
        }

        if (!requiredSet.empty()) {
            std::cout << "Missing extensions on this GPU:" << std::endl;
            for (auto& s : requiredSet) std::cout << " - " << s << std::endl;
        }

        return requiredSet.empty();
    }

void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

        // 使用 set 去重，防止同一个家族被创建多次
        std::set<uint32_t> uniqueFamilies;
        if (indices.graphicsFamily.has_value())    uniqueFamilies.insert(indices.graphicsFamily.value());
        if (indices.presentFamily.has_value())     uniqueFamilies.insert(indices.presentFamily.value());
        if (indices.computeFamily.has_value())     uniqueFamilies.insert(indices.computeFamily.value());
        if (indices.videoEncodeFamily.has_value()) uniqueFamilies.insert(indices.videoEncodeFamily.value());

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        float queuePriority = 1.0f;
        for (uint32_t familyIndex : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfo.queueFamilyIndex = familyIndex;
            queueCreateInfo.queueCount = 1; // 统一取 0 号队列
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceSynchronization2Features sync2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
        sync2Features.synchronization2 = VK_TRUE;

        VkPhysicalDeviceVideoEncodeAV1FeaturesKHR av1Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_AV1_FEATURES_KHR };
        av1Features.videoEncodeAV1 = VK_TRUE;
        av1Features.pNext = &sync2Features;
        VkPhysicalDeviceShaderDrawParametersFeatures drawParamsFeatures{};
        drawParamsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        drawParamsFeatures.shaderDrawParameters = VK_TRUE;
        drawParamsFeatures.pNext = &av1Features;

        VkPhysicalDeviceFeatures features{};
        features.sampleRateShading = VK_TRUE;

        VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos    = queueCreateInfos.data();
        createInfo.pEnabledFeatures     = &features;
        createInfo.pNext = &drawParamsFeatures;

        std::vector<const char*> extensions;
        if (!headless_) {
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        } else {
            extensions.push_back("VK_KHR_video_queue");
            extensions.push_back("VK_KHR_video_encode_queue");
            extensions.push_back("VK_KHR_video_encode_av1"); // 确保这里也是 KHR
        }

        // 请求 Vulkan-CUDA 显存与信号量同步设备扩展
        extensions.push_back("VK_KHR_external_memory");
        extensions.push_back("VK_KHR_external_semaphore");
#ifdef _WIN32
        extensions.push_back("VK_KHR_external_memory_win32");
        extensions.push_back("VK_KHR_external_semaphore_win32");
#else
        extensions.push_back("VK_KHR_external_memory_fd");
        extensions.push_back("VK_KHR_external_semaphore_fd");
#endif

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
        if (queueIndices_.computeFamily.has_value())
            vkGetDeviceQueue(device_, queueIndices_.computeFamily.value(), 0, &computeQueue_);
    }

    std::vector<const char*> getRequiredInstanceExtensions() const {
        std::vector<const char*> extensions;
        if (!headless_) {
            uint32_t sdlExtCount = 0;
            const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
            for(uint32_t i=0; i<sdlExtCount; ++i) extensions.push_back(sdlExts[i]);
        }

        // 关键：视频编码与外部能力查询必需的实例扩展
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        // 使能外部内存与信号量能力查询扩展
        extensions.push_back("VK_KHR_external_memory_capabilities");
        extensions.push_back("VK_KHR_external_semaphore_capabilities");

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