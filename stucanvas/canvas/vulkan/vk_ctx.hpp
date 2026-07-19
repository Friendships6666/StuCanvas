// stucanvas/canvas/vulkan/vk_ctx.hpp
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vk_mem_alloc.h>   // 1. 引入 VMA 头文件
#include <vulkan/vulkan.h>

#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace StuCanvas::Vulkan
{
    /**
     * @brief 异步队列信息
     */
    struct QueueInfo
    {
        VkQueue handle = VK_NULL_HANDLE;
        uint32_t familyIndex = 0xFFFFFFFF;
    };

    /**
     * @brief 逻辑设备（含关联物理设备、VMA分配器与异步队列）的独立 RAII 封装类
     */
    class VulkanDevice
    {
    public:

        VulkanDevice () = default;

        // 2. 构造函数：接入 VmaAllocator 指针
        VulkanDevice ( VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator )
            : device_ ( device ), physicalDevice_ ( physicalDevice ), allocator_ ( allocator )
        {
        }

        ~VulkanDevice ()
        {
            destroy ();
        }

        // 禁止拷贝，保证独占性
        VulkanDevice ( const VulkanDevice& ) = delete;
        VulkanDevice& operator= ( const VulkanDevice& ) = delete;

        // 允许移动语义
        VulkanDevice ( VulkanDevice&& other ) noexcept
        {
            *this = std::move ( other );
        }

        VulkanDevice& operator= ( VulkanDevice&& other ) noexcept
        {
            if ( this != &other )
            {
                destroy ();
                device_ = other.device_;
                physicalDevice_ = other.physicalDevice_;
                allocator_ = other.allocator_;   // 转移分配器所有权
                graphicsQueue_ = other.graphicsQueue_;
                presentQueue_ = other.presentQueue_;
                computeQueue_ = other.computeQueue_;
                transferQueue_ = other.transferQueue_;

                other.device_ = VK_NULL_HANDLE;
                other.allocator_ = VK_NULL_HANDLE;   // 置空
            }
            return *this;
        }

        VkDevice get () const
        {
            return device_;
        }
        VkPhysicalDevice getPhysicalDevice () const
        {
            return physicalDevice_;
        }
        VmaAllocator getAllocator () const
        {
            return allocator_;
        }   // 3. 提供对外获取分配器的接口

        const QueueInfo& getGraphicsQueue () const
        {
            return graphicsQueue_;
        }
        const QueueInfo& getPresentQueue () const
        {
            return presentQueue_;
        }
        const QueueInfo& getComputeQueue () const
        {
            return computeQueue_;
        }
        const QueueInfo& getTransferQueue () const
        {
            return transferQueue_;
        }

        void destroy ()
        {
            // 4. 必须遵守严格的反向销毁顺序：先 VMA，后 Device
            if ( allocator_ != VK_NULL_HANDLE )
            {
                vmaDestroyAllocator ( allocator_ );
                allocator_ = VK_NULL_HANDLE;
            }
            if ( device_ != VK_NULL_HANDLE )
            {
                vkDestroyDevice ( device_, nullptr );
                device_ = VK_NULL_HANDLE;
            }
        }

    private:

        VkDevice device_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VmaAllocator allocator_ = VK_NULL_HANDLE;   // VMA 分配器句柄

        QueueInfo graphicsQueue_;
        QueueInfo presentQueue_;
        QueueInfo computeQueue_;
        QueueInfo transferQueue_;

        friend class VulkanContext;
    };

    /**
     * @brief Vulkan 实例及上下文创建配置选项
     */
    struct VulkanContextConfig
    {
        std::string appName = "StuCanvas";
        uint32_t apiVersion = VK_API_VERSION_1_3;
        bool enableValidation = true;

        // 调用端自由配置的扩展
        std::vector< const char* > requiredInstanceExtensions;
        std::vector< const char* > requiredDeviceExtensions;
        std::vector< const char* > requiredValidationLayers = { "VK_LAYER_KHRONOS_validation" };
    };

    /**
     * @brief 整个 Vulkan 运行期上下文环境的 RAII 封装
     */
    class VulkanContext
    {
    public:

        VulkanContext () = default;

        ~VulkanContext ()
        {
            cleanup ();
        }

        // 禁止拷贝
        VulkanContext ( const VulkanContext& ) = delete;
        VulkanContext& operator= ( const VulkanContext& ) = delete;

        // 允许移动
        VulkanContext ( VulkanContext&& other ) noexcept
        {
            *this = std::move ( other );
        }

        VulkanContext& operator= ( VulkanContext&& other ) noexcept
        {
            if ( this != &other )
            {
                cleanup ();
                instance_ = other.instance_;
                debugMessenger_ = other.debugMessenger_;
                surface_ = other.surface_;
                physicalDevices_ = std::move ( other.physicalDevices_ );
                logicalDevices_ = std::move ( other.logicalDevices_ );
                validationEnabled_ = other.validationEnabled_;
                apiVersion_ = other.apiVersion_;

                other.instance_ = VK_NULL_HANDLE;
                other.debugMessenger_ = VK_NULL_HANDLE;
                other.surface_ = VK_NULL_HANDLE;
                other.apiVersion_ = VK_API_VERSION_1_0;
            }
            return *this;
        }

        /**
         * @brief 依据调用端传入的配置项初始化 Vulkan 实例与物理显卡列表
         */
        void initInstance ( const VulkanContextConfig& config )
        {
            validationEnabled_ = config.enableValidation;
            apiVersion_ = config.apiVersion;   // 暂存 apiVersion，以便后续初始化 VMA

            if ( validationEnabled_ && !checkValidationLayerSupport ( config.requiredValidationLayers ) )
            {
                throw std::runtime_error ( "VulkanContext: Validation layers requested but not supported." );
            }

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = config.appName.c_str ();
            appInfo.applicationVersion = VK_MAKE_VERSION ( 1, 0, 0 );
            appInfo.pEngineName = "StuCanvas";
            appInfo.engineVersion = VK_MAKE_VERSION ( 1, 0, 0 );
            appInfo.apiVersion = config.apiVersion;

            std::vector< const char* > instanceExtensions = config.requiredInstanceExtensions;

            if ( validationEnabled_ )
            {
                bool hasDebugUtils = false;
                for ( const auto& ext : instanceExtensions )
                {
                    if ( std::strcmp ( ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) == 0 )
                    {
                        hasDebugUtils = true;
                        break;
                    }
                }
                if ( !hasDebugUtils )
                {
                    instanceExtensions.push_back ( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
                }
            }

            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast< uint32_t > ( instanceExtensions.size () );
            createInfo.ppEnabledExtensionNames = instanceExtensions.data ();

            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
            if ( validationEnabled_ )
            {
                createInfo.enabledLayerCount = static_cast< uint32_t > ( config.requiredValidationLayers.size () );
                createInfo.ppEnabledLayerNames = config.requiredValidationLayers.data ();
                populateDebugMessengerCreateInfo ( debugCreateInfo );
                createInfo.pNext = &debugCreateInfo;
            }
            else
            {
                createInfo.enabledLayerCount = 0;
                createInfo.pNext = nullptr;
            }

            if ( vkCreateInstance ( &createInfo, nullptr, &instance_ ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanContext: Failed to create Vulkan instance." );
            }

            if ( validationEnabled_ )
            {
                setupDebugMessenger ();
            }

            enumeratePhysicalDevices ();
        }

        /**
         * @brief 绑定窗口并建立 Vulkan 表面 (非无头模式)
         */
        void initSurface ( SDL_Window* window )
        {
            if ( instance_ == VK_NULL_HANDLE )
            {
                throw std::runtime_error ( "VulkanContext: Cannot create surface before instance initialization." );
            }
            if ( !SDL_Vulkan_CreateSurface ( window, instance_, nullptr, &surface_ ) )
            {
                throw std::runtime_error ( "VulkanContext: Failed to create SDL Vulkan surface." );
            }
        }

        /**
         * @brief 支持调用端自定义过滤规则来刷选可用的物理显卡
         */
        void filterPhysicalDevices ( const std::function< bool ( VkPhysicalDevice ) >& predicate )
        {
            std::vector< VkPhysicalDevice > filtered;
            for ( const auto& device : physicalDevices_ )
            {
                if ( predicate ( device ) )
                {
                    filtered.push_back ( device );
                }
            }
            physicalDevices_ = filtered;
            if ( physicalDevices_.empty () )
            {
                throw std::runtime_error ( "VulkanContext: No physical devices matched the filter predicate." );
            }
        }

        /**
         * @brief 在指定的物理设备上实例化独立的 RAII 逻辑设备并为其绑定 VMA 分配器
         */
        void createLogicalDevice ( uint32_t physicalDeviceIndex, const std::vector< const char* >& requiredExtensions,
                                   const void* pNextFeatures = nullptr )
        {
            if ( physicalDeviceIndex >= physicalDevices_.size () )
            {
                throw std::out_of_range ( "VulkanContext: Physical device index out of range." );
            }

            VkPhysicalDevice physDev = physicalDevices_[ physicalDeviceIndex ];

            // A. 探测所有的队列家族
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties ( physDev, &queueFamilyCount, nullptr );
            std::vector< VkQueueFamilyProperties > queueFamilies ( queueFamilyCount );
            vkGetPhysicalDeviceQueueFamilyProperties ( physDev, &queueFamilyCount, queueFamilies.data () );

            uint32_t graphicsFamily = 0xFFFFFFFF;
            uint32_t presentFamily = 0xFFFFFFFF;
            uint32_t computeFamily = 0xFFFFFFFF;
            uint32_t transferFamily = 0xFFFFFFFF;

            for ( uint32_t i = 0; i < queueFamilyCount; ++i )
            {
                if ( queueFamilies[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT )
                {
                    if ( graphicsFamily == 0xFFFFFFFF )
                        graphicsFamily = i;
                }
                if ( surface_ != VK_NULL_HANDLE )
                {
                    VkBool32 presentSupport = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR ( physDev, i, surface_, &presentSupport );
                    if ( presentSupport )
                    {
                        if ( presentFamily == 0xFFFFFFFF )
                            presentFamily = i;
                    }
                }
                if ( queueFamilies[ i ].queueFlags & VK_QUEUE_COMPUTE_BIT )
                {
                    if ( !( queueFamilies[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
                    {
                        computeFamily = i;
                    }
                    else if ( computeFamily == 0xFFFFFFFF )
                    {
                        computeFamily = i;
                    }
                }
                if ( queueFamilies[ i ].queueFlags & VK_QUEUE_TRANSFER_BIT )
                {
                    if ( !( queueFamilies[ i ].queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) )
                    {
                        transferFamily = i;
                    }
                    else if ( transferFamily == 0xFFFFFFFF )
                    {
                        transferFamily = i;
                    }
                }
            }

            // B. 队列参数合并去重
            std::set< uint32_t > uniqueFamilies;
            if ( graphicsFamily != 0xFFFFFFFF )
                uniqueFamilies.insert ( graphicsFamily );
            if ( presentFamily != 0xFFFFFFFF )
                uniqueFamilies.insert ( presentFamily );
            if ( computeFamily != 0xFFFFFFFF )
                uniqueFamilies.insert ( computeFamily );
            if ( transferFamily != 0xFFFFFFFF )
                uniqueFamilies.insert ( transferFamily );

            std::vector< VkDeviceQueueCreateInfo > queueCreateInfos;
            float queuePriority = 1.0f;
            for ( uint32_t familyIndex : uniqueFamilies )
            {
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = familyIndex;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;
                queueCreateInfos.push_back ( queueCreateInfo );
            }

            VkPhysicalDeviceVulkan13Features vulkan13Features{};
            vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            vulkan13Features.synchronization2 = VK_TRUE;
            vulkan13Features.dynamicRendering = VK_TRUE;

            VkPhysicalDeviceVulkan11Features vulkan11Features{};
            vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            vulkan11Features.shaderDrawParameters = VK_TRUE;

            vulkan13Features.pNext = &vulkan11Features;
            vulkan11Features.pNext = const_cast< void* > ( pNextFeatures );

            // C. 逻辑设备配置
            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext = &vulkan13Features;
            createInfo.queueCreateInfoCount = static_cast< uint32_t > ( queueCreateInfos.size () );
            createInfo.pQueueCreateInfos = queueCreateInfos.data ();

            VkPhysicalDeviceFeatures deviceFeatures{};
            deviceFeatures.sampleRateShading = VK_TRUE;
            deviceFeatures.shaderInt64 = VK_TRUE;
            createInfo.pEnabledFeatures = &deviceFeatures;

            createInfo.enabledExtensionCount = static_cast< uint32_t > ( requiredExtensions.size () );
            createInfo.ppEnabledExtensionNames = requiredExtensions.data ();

            VkDevice device = VK_NULL_HANDLE;
            if ( vkCreateDevice ( physDev, &createInfo, nullptr, &device ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanContext: Failed to create logical device." );
            }

            // ================================================================
            // 5. 核心改动：在逻辑设备创建后，紧接着构建 VMA Allocator
            // ================================================================
            VmaAllocator allocator = VK_NULL_HANDLE;
            VmaAllocatorCreateInfo allocatorInfo{};
            allocatorInfo.physicalDevice = physDev;
            allocatorInfo.device = device;
            allocatorInfo.instance = instance_;
            allocatorInfo.vulkanApiVersion = apiVersion_;
            allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;


            if ( vmaCreateAllocator ( &allocatorInfo, &allocator ) != VK_SUCCESS )
            {
                vkDestroyDevice ( device, nullptr );
                throw std::runtime_error ( "VulkanContext: Failed to create VmaAllocator." );
            }

            // 将分配器一同封入 VulkanDevice 中
            auto vulkanDevice = std::make_unique< VulkanDevice > ( device, physDev, allocator );

            if ( graphicsFamily != 0xFFFFFFFF )
            {
                vulkanDevice->graphicsQueue_.familyIndex = graphicsFamily;
                vkGetDeviceQueue ( device, graphicsFamily, 0, &vulkanDevice->graphicsQueue_.handle );
            }
            if ( presentFamily != 0xFFFFFFFF )
            {
                vulkanDevice->presentQueue_.familyIndex = presentFamily;
                vkGetDeviceQueue ( device, presentFamily, 0, &vulkanDevice->presentQueue_.handle );
            }
            if ( computeFamily != 0xFFFFFFFF )
            {
                vulkanDevice->computeQueue_.familyIndex = computeFamily;
                vkGetDeviceQueue ( device, computeFamily, 0, &vulkanDevice->computeQueue_.handle );
            }
            if ( transferFamily != 0xFFFFFFFF )
            {
                vulkanDevice->transferQueue_.familyIndex = transferFamily;
                vkGetDeviceQueue ( device, transferFamily, 0, &vulkanDevice->transferQueue_.handle );
            }

            logicalDevices_.push_back ( std::move ( vulkanDevice ) );
        }

        VkInstance getInstance () const
        {
            return instance_;
        }
        VkSurfaceKHR getSurface () const
        {
            return surface_;
        }

        const std::vector< VkPhysicalDevice >& getPhysicalDevices () const
        {
            return physicalDevices_;
        }

        const std::vector< std::unique_ptr< VulkanDevice > >& getLogicalDevices () const
        {
            return logicalDevices_;
        }

        const VulkanDevice& getPrimaryDevice () const
        {
            if ( logicalDevices_.empty () || !logicalDevices_[ 0 ] )
            {
                throw std::runtime_error ( "VulkanContext: No primary device created." );
            }
            return *logicalDevices_[ 0 ];
        }

    private:

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;

        std::vector< VkPhysicalDevice > physicalDevices_;
        std::vector< std::unique_ptr< VulkanDevice > > logicalDevices_;

        bool validationEnabled_ = false;
        uint32_t apiVersion_ = VK_API_VERSION_1_0;   // 暂存实例版本

        void enumeratePhysicalDevices ()
        {
            uint32_t count = 0;
            vkEnumeratePhysicalDevices ( instance_, &count, nullptr );
            if ( count == 0 )
            {
                throw std::runtime_error ( "VulkanContext: No Vulkan-compatible physical devices found." );
            }
            physicalDevices_.resize ( count );
            vkEnumeratePhysicalDevices ( instance_, &count, physicalDevices_.data () );
        }

        bool checkValidationLayerSupport ( const std::vector< const char* >& requiredLayers )
        {
            uint32_t count;
            vkEnumerateInstanceLayerProperties ( &count, nullptr );
            std::vector< VkLayerProperties > available ( count );
            vkEnumerateInstanceLayerProperties ( &count, available.data () );

            for ( const char* layerName : requiredLayers )
            {
                bool found = false;
                for ( const auto& layer : available )
                {
                    if ( std::strcmp ( layerName, layer.layerName ) == 0 )
                    {
                        found = true;
                        break;
                    }
                }
                if ( !found )
                    return false;
            }
            return true;
        }

        static void populateDebugMessengerCreateInfo ( VkDebugUtilsMessengerCreateInfoEXT& createInfo )
        {
            std::memset ( &createInfo, 0, sizeof ( createInfo ) );
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            createInfo.pfnUserCallback = debugCallback;
        }

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback ( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                                              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                              void* pUserData )
        {
            std::cerr << "[Validation]: " << pCallbackData->pMessage << std::endl;
            return VK_FALSE;
        }

        void setupDebugMessenger ()
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            populateDebugMessengerCreateInfo ( createInfo );
            if ( CreateDebugUtilsMessengerEXT ( instance_, &createInfo, nullptr, &debugMessenger_ ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanContext: Failed to setup debug messenger." );
            }
        }

        static VkResult CreateDebugUtilsMessengerEXT ( VkInstance instance,
                                                       const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkDebugUtilsMessengerEXT* pDebugMessenger )
        {
            auto func = ( PFN_vkCreateDebugUtilsMessengerEXT ) vkGetInstanceProcAddr (
                instance, "vkCreateDebugUtilsMessengerEXT" );
            if ( func != nullptr )
            {
                return func ( instance, pCreateInfo, pAllocator, pDebugMessenger );
            }
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        static void DestroyDebugUtilsMessengerEXT ( VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                                    const VkAllocationCallbacks* pAllocator )
        {
            auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT ) vkGetInstanceProcAddr (
                instance, "vkDestroyDebugUtilsMessengerEXT" );
            if ( func != nullptr )
            {
                func ( instance, debugMessenger, pAllocator );
            }
        }

        void cleanup ()
        {
            logicalDevices_.clear ();

            if ( surface_ != VK_NULL_HANDLE )
            {
                vkDestroySurfaceKHR ( instance_, surface_, nullptr );
                surface_ = VK_NULL_HANDLE;
            }

            if ( debugMessenger_ != VK_NULL_HANDLE )
            {
                DestroyDebugUtilsMessengerEXT ( instance_, debugMessenger_, nullptr );
                debugMessenger_ = VK_NULL_HANDLE;
            }

            if ( instance_ != VK_NULL_HANDLE )
            {
                vkDestroyInstance ( instance_, nullptr );
                instance_ = VK_NULL_HANDLE;
            }
        }
    };
}   // namespace StuCanvas::Vulkan
