/***************************************************************************
 * Copyright (c) 2025-2026 Tian Yuxuan (Friendships666)                    *
 *                                                                          *
 * StuCanvas is licensed under Mulan PSL v2.                                *
 * You can use this software according to the terms and conditions of the   *
 * Mulan PSL v2.                                                            *
 * You may obtain a copy of Mulan PSL v2 at:                                *
 *          http://license.coscl.org.cn/MulanPSL2                           *
 *                                                                          *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF     *
 * ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO        *
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.      *
 * See the Mulan PSL v2 for more details.                                   *
 ***************************************************************************/

#pragma once
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <eigen3/Eigen/Dense>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "../assets/shaders/shaders.hpp"
#include "../objects/appearance/appearance.hpp"
#include "../objects/dag/AABB.hpp"
#include "../objects/dag/graph.hpp"
#include "cameras.hpp"
#include "pinned_vector.hpp"
#include "stb/stb_image_write.h"
#include "vulkan/vk_ctx.hpp"
namespace StuCanvas
{

    struct PointData
    {
        float worldPos[ 3 ];   ///< 原始 3D 局部坐标 (12 字节)
        float _pad0 = 0.0f;    ///< 🚀 手动 4 字节填充，确保 color 16 字节对齐
        float color[ 4 ];      ///< 颜色 (16 字节)
    };

    struct PointSDFConstants
    {
        float vp[ 16 ];          ///< 观察-投影矩阵 (64 字节)
        float imageSize[ 2 ];    ///< 画布物理分辨率 (8 字节)
        float pointRadius;       ///< 点半径 (4 字节)
        float _pad0 = 0.0f;      ///< 4 字节对齐
        VkDeviceAddress cloud;   ///< 🚀 64位 GPU 物理指针 (8 字节)，对应 Slang 的 PointData* points
    };

    // =========================================================================
    // 🚀 修复点 2：前置定义提取局部点坐标的内联辅助函数
    // =========================================================================
    inline Eigen::Vector3d extractLocalPoint ( const DAGObject& object )
    {
        Eigen::Vector3d local_pt = Eigen::Vector3d::Zero ();
        switch ( object.type )
        {
            case NodeType::POINT_2D_FREE:
            case NodeType::POINT_2D_MID:
            case NodeType::POINT_2D_SECTION:
            case NodeType::POINT_2D_INTERSECT:
            {
                local_pt.x () = object.data.point_2d.x;
                local_pt.y () = object.data.point_2d.y;
                local_pt.z () = 0.0;
                break;
            }
            case NodeType::POINT_2D_SNAP:
            {
                local_pt.x () = object.data.snap_2d.x;
                local_pt.y () = object.data.snap_2d.y;
                local_pt.z () = 0.0;
                break;
            }
            case NodeType::POINT_3D_FREE:
            case NodeType::POINT_3D_MID:
            case NodeType::POINT_3D_SECTION:
            case NodeType::POINT_3D_INTERSECT:
            {
                local_pt.x () = object.data.point_3d.x;
                local_pt.y () = object.data.point_3d.y;
                local_pt.z () = object.data.point_3d.z;
                break;
            }
            case NodeType::POINT_3D_SNAP:
            {
                local_pt.x () = object.data.snap_3d.x;
                local_pt.y () = object.data.snap_3d.y;
                local_pt.z () = object.data.snap_3d.z;
                break;
            }
            default:
                break;
        }
        return local_pt;
    }
    /**
     * @brief 渲染与计算调度指令队列 (VulkanQueue)
     *
     * 内置了极速 Vulkan 运行时上下文，并管理独立的图形与异步计算命令通道。
     */

    // 🚀 内部安全数据结构：缓存头校验
    struct ShaderCacheHeader
    {
        uint8_t pipelineCacheUUID[ VK_UUID_SIZE ];   ///< 显卡设备硬件唯一标识
        uint32_t driverVersion;                      ///< 显卡驱动程序版本
        uint64_t spirv_hash;                         ///< 原始 SPIR-V 字节码哈希
        uint64_t binary_size;                        ///< 原生机器码二进制字节大小
    };

    // FNV-1a 64位无损极速哈希计算器 (0 第三方库依赖)
    static inline uint64_t computeSpirvHash ( std::span< const uint32_t > code ) noexcept
    {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for ( const uint32_t val : code )
        {
            hash ^= val;
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }
    struct RenderFrame
    {
        uint32_t width;
        uint32_t height;
        VkImage image = VK_NULL_HANDLE;              ///< 物理显存图片句柄 (GPU 渲染写入目标)
        VkImageView view = VK_NULL_HANDLE;           ///< 视口视图句柄 (供后期/合成采样使用) [1.1.1]
        VmaAllocation allocation = VK_NULL_HANDLE;   ///< VMA 显存分配追踪句柄 (用于安全自动释放) [2]

        // 🚀 核心加入：关联的逻辑视口指针
        // 在合成器（Compute Shader）阶段，用于在常数时间内快速读取其百分比范围 [x, y, w, h] 及外观 transition 效果
        ViewPort* viewport = nullptr;
    };
    class VulkanQueue
    {
    public:

        VkCommandBuffer compute = VK_NULL_HANDLE;    ///< 计算管线专用的异步命令缓冲区
        VkCommandBuffer graphics = VK_NULL_HANDLE;   ///< 图形管线专用的渲染命令缓冲区
        utils::PinnedVector< Camera, 32 > cameras;
        utils::PinnedVector< ViewPort, 32 > viewports;
        utils::PinnedVector< RenderFrame, 32 > frames;


        uint32_t createImage ( uint32_t width, uint32_t height )
        {
            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();

            // 1. 配置物理图像创建选项 (硬编码最常见的 VK_FORMAT_R8G8B8A8_UNORM 格式)
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

            VkImage image = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;

            if ( vmaCreateImage ( allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::createImage: Failed to allocate GPU image memory." );
            }

            // 2. 为该物理图像创建专用的最常见格式视图
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            if ( vkCreateImageView ( device, &viewInfo, nullptr, &view ) != VK_SUCCESS )
            {
                vmaDestroyImage ( allocator, image, allocation );
                throw std::runtime_error ( "VulkanQueue::createImage: Failed to create VkImageView." );
            }

            const uint32_t index = static_cast< uint32_t > ( frames.size () );

            RenderFrame& frame = frames.emplace_back ();
            frame.image = image;
            frame.view = view;
            frame.allocation = allocation;
            frame.viewport = nullptr;
            frame.width = width;
            frame.height = height;

            return index;
        }
        void modifyImageSize ( uint32_t index, uint32_t width, uint32_t height )
        {
            if ( index >= frames.size () ) [[unlikely]]
            {
                throw std::out_of_range ( "VulkanQueue::modifyImageSize: Index out of range." );
            }

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();

            RenderFrame& frame = frames[ index ];

            // 1. 安全清理该位置原有的 Vulkan 图像资源
            if ( frame.view != VK_NULL_HANDLE )
            {
                vkDestroyImageView ( device, frame.view, nullptr );
                frame.view = VK_NULL_HANDLE;
            }
            if ( frame.image != VK_NULL_HANDLE )
            {
                vmaDestroyImage ( allocator, frame.image, frame.allocation );
                frame.image = VK_NULL_HANDLE;
                frame.allocation = VK_NULL_HANDLE;
            }

            // 2. 重新配置物理图像创建参数 (硬编码为 VK_FORMAT_R8G8B8A8_UNORM)
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

            // 3. 执行显存重新分配
            if ( vmaCreateImage ( allocator, &imageInfo, &allocInfo, &frame.image, &frame.allocation, nullptr ) !=
                 VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::modifyImageSize: Failed to reallocate GPU image memory." );
            }

            // 4. 重新创建 2D 图像视图
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = frame.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if ( vkCreateImageView ( device, &viewInfo, nullptr, &frame.view ) != VK_SUCCESS )
            {
                vmaDestroyImage ( allocator, frame.image, frame.allocation );
                frame.image = VK_NULL_HANDLE;
                frame.allocation = VK_NULL_HANDLE;
                throw std::runtime_error ( "VulkanQueue::modifyImageSize: Failed to recreate VkImageView." );
            }
        }
        /**
         * @brief 创建一个正交相机，并在 PinnedVector 中进行托管
         * @return 托管在内存池中的具体正交相机实体引用 (可直接配置内/外参)
         */
        OrthographicCamera& createOrthographicCamera ()
        {
            Camera& cam = cameras.emplace_back ();
            cam.real_data = new OrthographicCamera ();
            return *static_cast< OrthographicCamera* > ( cam.real_data );
        }

        /**
         * @brief 创建一个透视相机，并在 PinnedVector 中进行托管
         * @return 托管在内存池中的具体透视相机实体引用 (可直接配置内/外参)
         */
        PerspectiveCamera& createPerspectiveCamera ()
        {
            Camera& cam = cameras.emplace_back ();
            cam.real_data = new PerspectiveCamera ();
            return *static_cast< PerspectiveCamera* > ( cam.real_data );
        }


        ViewPort& createViewPort ()
        {
            ViewPort& viewport = viewports.emplace_back ();
            return viewport;
        }

        /**
         * @brief 创建、编译并物理缓存一个现代着色器对象，并直接返回其物理句柄 (VkShaderEXT)
         *
         * @param stage 着色器流水线阶段 (如 VK_SHADER_STAGE_VERTEX_BIT / VK_SHADER_STAGE_FRAGMENT_BIT)
         * @param spirv_code 原始 SPIR-V 编译后的 32位字节码切片
         * @param cache_path 磁盘缓存文件路径。若传入 nullptr 则不启用磁盘缓存
         * @return 成功创建或从二进制缓存加载的着色器对象句柄 (VkShaderEXT)
         */
        VkShaderEXT createShader ( VkShaderStageFlagBits stage, std::span< const uint32_t > spirv_code,
                                   const char* cache_path )
        {
            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VkPhysicalDevice physDevice = primaryDevice.getPhysicalDevice ();

            // 获取当前显卡设备的硬件属性 (用于版本与 UUID 的安全校验)
            VkPhysicalDeviceProperties deviceProps;
            vkGetPhysicalDeviceProperties ( physDevice, &deviceProps );

            // 使用构造函数中已加载的函数指针成员，无需重复动态查询
            if ( !pfnCreateShaders || !pfnGetShaderBinaryData ) [[unlikely]]
            {
                throw std::runtime_error (
                    "VulkanQueue::createShader: Vulkan driver does not support VK_EXT_shader_object." );
            }
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = 128;   // 动态对齐 88 字节的常量块

            VkShaderEXT shader = VK_NULL_HANDLE;
            VkShaderStageFlags next_stage = ( stage == VK_SHADER_STAGE_VERTEX_BIT ) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0;
            bool cache_loaded = false;
            const uint64_t current_spirv_hash = computeSpirvHash ( spirv_code );
            const char* entry_name = ( stage == VK_SHADER_STAGE_VERTEX_BIT ) ? "vertexMain" : "fragmentMain";

            // =====================================================================
            // 1. 尝试从磁盘读取缓存 (热启动检测)
            // =====================================================================
            if ( cache_path )
            {
                std::ifstream in ( cache_path, std::ios::binary );
                if ( in )
                {
                    ShaderCacheHeader header{};
                    in.read ( reinterpret_cast< char* > ( &header ), sizeof ( ShaderCacheHeader ) );

                    // 核心安全校验：当且仅当 硬件UUID、驱动版本 以及 SPIR-V 哈希值完全一致时，缓存才可用
                    if ( std::memcmp ( header.pipelineCacheUUID, deviceProps.pipelineCacheUUID, VK_UUID_SIZE ) == 0 &&
                         header.driverVersion == deviceProps.driverVersion && header.spirv_hash == current_spirv_hash )
                        [[likely]]
                    {
                        std::vector< uint8_t > binary_data ( header.binary_size );
                        in.read ( reinterpret_cast< char* > ( binary_data.data () ), header.binary_size );

                        VkShaderCreateInfoEXT createInfo{};
                        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
                        createInfo.stage = stage;
                        createInfo.codeType = VK_SHADER_CODE_TYPE_BINARY_EXT;   // 🚀 使用二进制原生机器码加载
                        createInfo.codeSize = header.binary_size;
                        createInfo.pCode = binary_data.data ();
                        createInfo.pName = "main";
                        createInfo.nextStage = next_stage;   // 🚀 绑定衔接阶段
                        createInfo.setLayoutCount = 0;
                        createInfo.pSetLayouts = nullptr;
                        createInfo.pushConstantRangeCount = 1;         // 🚀 绑定常量范围
                        createInfo.pPushConstantRanges = &pushRange;   // 🚀 绑定


                        // 显卡驱动直接加载原生指令，跳过编译与优化阶段，时间缩短至微秒级
                        if ( pfnCreateShaders ( device, 1, &createInfo, nullptr, &shader ) == VK_SUCCESS )
                        {
                            cache_loaded = true;
                        }
                    }
                }
            }

            // =====================================================================
            // 2. 缓存未命中或失效 (冷启动兜底编译)
            // =====================================================================
            if ( !cache_loaded )
            {
                std::cout << "正在以冷启动方式编译并分析着色器字节码...\n";

                VkShaderCreateInfoEXT createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
                createInfo.stage = stage;
                createInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;   // 🚀 使用标准的 SPIR-V 编译
                createInfo.codeSize = spirv_code.size () * sizeof ( uint32_t );
                createInfo.pCode = spirv_code.data ();
                createInfo.pName = "main";
                createInfo.setLayoutCount = 0;
                createInfo.pSetLayouts = nullptr;
                createInfo.nextStage = next_stage;             // 🚀 绑定衔接阶段
                createInfo.pushConstantRangeCount = 1;         // 🚀 绑定常量范围
                createInfo.pPushConstantRanges = &pushRange;   // 🚀 绑定


                if ( pfnCreateShaders ( device, 1, &createInfo, nullptr, &shader ) != VK_SUCCESS )
                {
                    throw std::runtime_error ( "VulkanQueue::createShader: Failed to compile shader from SPIR-V." );
                }

                // =================================================================
                // 3. 提取编译完成的原生 GPU ISA 并回写磁盘
                // =================================================================
                if ( cache_path )
                {
                    // 🚀 核心修改：使用 C++17 std::filesystem 自动检查并创建缓存父文件夹 [1]
                    // 这能防止由于文件夹不存在而导致 std::ofstream 静默创建文件失败的问题 [1]
                    std::filesystem::path path_checker ( cache_path );
                    if ( path_checker.has_parent_path () ) [[likely]]
                    {
                        std::filesystem::create_directories ( path_checker.parent_path () );
                    }

                    size_t binary_size = 0;
                    // 获取二进制数据实际大小
                    if ( pfnGetShaderBinaryData ( device, shader, &binary_size, nullptr ) == VK_SUCCESS &&
                         binary_size > 0 )
                    {
                        std::vector< uint8_t > binary_data ( binary_size );
                        if ( pfnGetShaderBinaryData ( device, shader, &binary_size, binary_data.data () ) ==
                             VK_SUCCESS )
                        {
                            // 写入磁盘 (此时由于文件夹已被 std::filesystem 物理创建，写入必然成功) [1]
                            std::ofstream out ( cache_path, std::ios::binary );
                            if ( out )
                            {
                                ShaderCacheHeader header{};
                                std::memcpy ( header.pipelineCacheUUID, deviceProps.pipelineCacheUUID, VK_UUID_SIZE );
                                header.driverVersion = deviceProps.driverVersion;
                                header.spirv_hash = current_spirv_hash;
                                header.binary_size = binary_size;

                                out.write ( reinterpret_cast< const char* > ( &header ), sizeof ( ShaderCacheHeader ) );
                                out.write ( reinterpret_cast< const char* > ( binary_data.data () ), binary_size );
                            }
                        }
                    }
                }
            }

            // 4. 直接返回成功创建的着色器对象句柄
            return shader;
        } /**
           * @brief 自动遍历静态着色器资产表，零多余转换，极速完成硬件载入与 O(1) 物理索引对齐
           */
        void initializeAllShaders ()
        {
            // 1. 物理探测用户注册的最高 slot 索引，以此来规划 shader_objects 的容量
            uint32_t max_slot = 0;
            for ( const auto& asset : Shaders::g_shader_assets )
            {
                max_slot = std::max ( max_slot, asset.slot );
            }

            // 2. 🚀 强行重置大小。这保证了未来通过索引 `shader_objects[slot]` 访问时，绝不发生越界
            shader_objects.resize ( max_slot + 1, VK_NULL_HANDLE );

            // 3. 循环遍历一键加载
            for ( const auto& asset : Shaders::g_shader_assets )
            {
                // 为每个 Slot 生成其专属且独立的磁盘缓存路径 (如 "shader_cache/slot_0.cache")
                std::string cache_path = "shader_cache/slot_" + std::to_string ( asset.slot ) + ".cache";

                // 执行冷/热启动编译 (createShader 此时直接返回生成的 VkShaderEXT 句柄) [1.1.1]
                VkShaderEXT compiled_shader = createShader ( asset.stage, asset.code, cache_path.c_str () );

                // 🚀 极致性能对齐：直接用用户的递增 slot 作为下标物理托管，实现真正的 O(1) 物理直寻址！
                shader_objects[ asset.slot ] = compiled_shader;
            }
        }


        /**
         * @brief 一键初始化物理显卡流水线并创建命令缓冲区
         */
        VulkanQueue ()
        {
            Vulkan::VulkanContextConfig config;
            config.appName = "StuCanvas";
            config.apiVersion = VK_API_VERSION_1_4;
            config.enableValidation = true;

            // 1. 初始化 Instance
            ctx.initInstance ( config );

            // 2. 基于“独显特权 + VRAM显存大小”筛选出最强独立显卡
            const auto& devices = ctx.getPhysicalDevices ();
            VkPhysicalDevice best_device = VK_NULL_HANDLE;
            uint64_t best_score = 0;

            for ( const auto& device : devices )
            {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties ( device, &props );

                uint64_t score = 0;
                if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU )
                {
                    score += 1000000000ULL;
                }
                else if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU )
                {
                    score += 100000000ULL;
                }

                VkPhysicalDeviceMemoryProperties memProps;
                vkGetPhysicalDeviceMemoryProperties ( device, &memProps );
                uint64_t vram_bytes = 0;
                for ( uint32_t i = 0; i < memProps.memoryHeapCount; ++i )
                {
                    if ( memProps.memoryHeaps[ i ].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT )
                    {
                        vram_bytes += memProps.memoryHeaps[ i ].size;
                    }
                }

                score += ( vram_bytes / ( 1024ULL * 1024ULL ) );

                if ( score > best_score )
                {
                    best_score = score;
                    best_device = device;
                }
            }

            ctx.filterPhysicalDevices ( [ best_device ] ( VkPhysicalDevice device ) { return device == best_device; } );

            // =====================================================================
            // 3. 配置 GPU 特性链表 (光追 + SBT + 现代 1.2 核心特征)
            // =====================================================================

            // 🚀 A. 现代 Vulkan 1.2 核心特征 (包含物理寻址 BDA，并关闭 scalarBlockLayout)
            // 提示：无需在此处声明 1.3 结构体，ctx.createLogicalDevice 内部会自动前置附加 1.3 结构
            VkPhysicalDeviceVulkan12Features features12{};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.bufferDeviceAddress = VK_TRUE;   // 🚀 启用 1.2 核心 BDA，解决物理指针寻址支持
            features12.scalarBlockLayout = VK_FALSE;    // 🚀 拒绝开启 float3 紧凑对齐扩展，严格采用标准 std430 边界

            // 🚀 B. 开启 VkShaderEXT 特征 (着色器对象)
            VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures{};
            shaderObjectFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
            shaderObjectFeatures.shaderObject = VK_TRUE;
            features12.pNext = &shaderObjectFeatures;   // 🚀 1.2 特征连接至着色器对象

            // 🚀 C. 开启 KHR 加速结构特征 (光追核心)
            VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
            asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            asFeatures.accelerationStructure = VK_TRUE;
            shaderObjectFeatures.pNext = &asFeatures;   // 🚀 着色器对象特征连接至加速结构

            // 🚀 D. 开启 KHR 硬件光线追踪管线特征 (作为整个外部链表的头部)
            VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
            rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            rtFeatures.rayTracingPipeline = VK_TRUE;
            // 🚀 直接将 KHR 光追特征链入 1.2 核心，完全绕开 1.3 重复声明和 features2 冲突 [1]
            rtFeatures.pNext = &features12;

            // =====================================================================
            // 4. 设备级扩展配置 (对齐 Vulkan 1.4 标准)
            // =====================================================================
            const std::vector< const char* > device_extensions = {
                VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,          // 动态渲染
                VK_EXT_SHADER_OBJECT_EXTENSION_NAME,              // 🚀 着色器对象扩展
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,     // 加速结构 (Ray Tracing)
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,       // 硬件光追管线
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,   // 延时主机操作
                VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,      // 显存物理寻址 (BDA)
                VK_KHR_SPIRV_1_4_EXTENSION_NAME,                  // SPIR-V 1.4 支持
                VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME       // 浮点精准控制
            };

            // =====================================================================
            // 5. 🚀 一键拉起逻辑设备创建
            // =====================================================================
            // 我们将 rtFeatures（已串联好 1.2、着色器对象和光追结构体）作为链表头部传递过去
            ctx.createLogicalDevice ( 0, device_extensions, &rtFeatures );
            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();

            // 🚀 一次性加载所有 Vulkan 扩展函数指针，避免每次调用时重复动态查询
            pfnCreateShaders = ( PFN_vkCreateShadersEXT ) vkGetDeviceProcAddr ( device, "vkCreateShadersEXT" );
            pfnGetShaderBinaryData =
                ( PFN_vkGetShaderBinaryDataEXT ) vkGetDeviceProcAddr ( device, "vkGetShaderBinaryDataEXT" );
            pfnDestroyShader = ( PFN_vkDestroyShaderEXT ) vkGetDeviceProcAddr ( device, "vkDestroyShaderEXT" );
            pfnCmdSetPolygonMode =
                ( PFN_vkCmdSetPolygonModeEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetPolygonModeEXT" );
            pfnCmdSetRasterizationSamples = ( PFN_vkCmdSetRasterizationSamplesEXT ) vkGetDeviceProcAddr (
                device, "vkCmdSetRasterizationSamplesEXT" );
            pfnCmdSetSampleMask = ( PFN_vkCmdSetSampleMaskEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetSampleMaskEXT" );
            pfnCmdSetAlphaToCoverageEnable = ( PFN_vkCmdSetAlphaToCoverageEnableEXT ) vkGetDeviceProcAddr (
                device, "vkCmdSetAlphaToCoverageEnableEXT" );
            pfnCmdSetVertexInput =
                ( PFN_vkCmdSetVertexInputEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetVertexInputEXT" );
            pfnCmdSetColorBlendEnable =
                ( PFN_vkCmdSetColorBlendEnableEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetColorBlendEnableEXT" );
            pfnCmdSetColorBlendEquation =
                ( PFN_vkCmdSetColorBlendEquationEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetColorBlendEquationEXT" );
            pfnCmdSetColorWriteMask =
                ( PFN_vkCmdSetColorWriteMaskEXT ) vkGetDeviceProcAddr ( device, "vkCmdSetColorWriteMaskEXT" );
            pfnCmdBindShaders = ( PFN_vkCmdBindShadersEXT ) vkGetDeviceProcAddr ( device, "vkCmdBindShadersEXT" );
            pfnGetBufferDeviceAddress =
                ( PFN_vkGetBufferDeviceAddress ) vkGetDeviceProcAddr ( device, "vkGetBufferDeviceAddress" );

            const uint32_t graphics_family = primaryDevice.getGraphicsQueue ().familyIndex;
            const uint32_t compute_family = primaryDevice.getComputeQueue ().familyIndex;

            // =================================================================
            // 6. 创建图形与异步计算命令池 (Command Pool)
            // =================================================================
            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            // 创建图形命令池
            pool_info.queueFamilyIndex = graphics_family;
            if ( vkCreateCommandPool ( device, &pool_info, nullptr, &graphics_pool ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to create graphics command pool." );
            }

            // 创建异步计算命令池
            pool_info.queueFamilyIndex = compute_family;
            if ( vkCreateCommandPool ( device, &pool_info, nullptr, &compute_pool ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to create compute command pool." );
            }

            // =================================================================
            // 7. 分配图形与计算命令缓冲区 (Command Buffer)
            // =================================================================
            VkCommandBufferAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;

            // 从图形命令池分配 graphics 缓冲区
            alloc_info.commandPool = graphics_pool;
            if ( vkAllocateCommandBuffers ( device, &alloc_info, &graphics ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to allocate graphics command buffer." );
            }

            // 从计算命令池分配 compute 缓冲区
            alloc_info.commandPool = compute_pool;
            if ( vkAllocateCommandBuffers ( device, &alloc_info, &compute ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to allocate compute command buffer." );
            }

            initializeAllShaders ();

            // =================================================================
            // 🚀 一键初始化唯一的常驻栅栏 (移入构造函数中只执行一次)
            // 🚀 核心：设置 VK_FENCE_CREATE_SIGNALED_BIT 标志使其初始状态为绿灯 [1.2.8]
            // =================================================================
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if ( vkCreateFence ( device, &fenceInfo, nullptr, &garbage_fence ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to create resident garbage VkFence." );
            }
            // 🚀 在 VulkanQueue 构造函数中初始化：
            VkPushConstantRange push_constant_range{};
            push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push_constant_range.offset = 0;
            push_constant_range.size = 128;   // 🚀 声明 128 字节的最大安全寄存器施工范围

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &push_constant_range;
            layoutInfo.setLayoutCount = 0;   // 由于我们使用 BDA (指针直传)，不需要任何 Descriptor Sets [1]
            layoutInfo.pSetLayouts = nullptr;

            if ( vkCreatePipelineLayout ( device, &layoutInfo, nullptr, &pipeline_layout ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue: Failed to create static pipeline layout." );
            }
        }

        ~VulkanQueue () noexcept
        {
            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();   // 🚀 提取 VMA 分配器用于安全释放显存 [2]

            if ( device != VK_NULL_HANDLE ) [[likely]]
            {
                // =================================================================
                // 🚀 新增：安全释放全局 8x MSAA 颜色附件与深度附件，防范显存泄漏 [1]
                // =================================================================
                //
                cleanupGarbage ();
                for ( auto& frame : frames )
                {
                    if ( frame.view != VK_NULL_HANDLE )
                    {
                        vkDestroyImageView ( device, frame.view, nullptr );
                        frame.view = VK_NULL_HANDLE;
                    }
                    if ( frame.image != VK_NULL_HANDLE )
                    {
                        vmaDestroyImage ( allocator, frame.image, frame.allocation );
                        frame.image = VK_NULL_HANDLE;
                        frame.allocation = VK_NULL_HANDLE;
                    }
                }
                frames.clear ();   // 清空账本
                if ( pipeline_layout != VK_NULL_HANDLE )
                {
                    vkDestroyPipelineLayout ( device, pipeline_layout, nullptr );
                    pipeline_layout = VK_NULL_HANDLE;
                }

                if ( garbage_fence != VK_NULL_HANDLE )
                {
                    vkDestroyFence ( device, garbage_fence, nullptr );
                    garbage_fence = VK_NULL_HANDLE;
                }

                if ( msaa_color_view != VK_NULL_HANDLE )
                {
                    vkDestroyImageView ( device, msaa_color_view, nullptr );
                    msaa_color_view = VK_NULL_HANDLE;
                }
                if ( msaa_color_image != VK_NULL_HANDLE )
                {
                    vmaDestroyImage ( allocator, msaa_color_image, msaa_color_alloc );
                    msaa_color_image = VK_NULL_HANDLE;
                    msaa_color_alloc = VK_NULL_HANDLE;
                }

                if ( msaa_depth_view != VK_NULL_HANDLE )
                {
                    vkDestroyImageView ( device, msaa_depth_view, nullptr );
                    msaa_depth_view = VK_NULL_HANDLE;
                }
                if ( msaa_depth_image != VK_NULL_HANDLE )
                {
                    vmaDestroyImage ( allocator, msaa_depth_image, msaa_depth_alloc );
                    msaa_depth_image = VK_NULL_HANDLE;
                    msaa_depth_alloc = VK_NULL_HANDLE;
                }

                // =================================================================
                // 后续其他缓存资源的释放 (按逆序完美流转) [1]
                // =================================================================
                // 使用构造函数中已加载的函数指针成员销毁 Shader Objects
                for ( VkShaderEXT shader : shader_objects )
                {
                    if ( shader != VK_NULL_HANDLE )
                    {
                        // 🚀 采用动态加载的函数指针执行物理销毁
                        pfnDestroyShader ( device, shader, nullptr );
                    }
                }
                shader_objects.clear ();

                if ( pipeline_cache != VK_NULL_HANDLE )
                {
                    vkDestroyPipelineCache ( device, pipeline_cache, nullptr );
                    pipeline_cache = VK_NULL_HANDLE;
                }

                if ( compute_pool != VK_NULL_HANDLE )
                {
                    vkDestroyCommandPool ( device, compute_pool, nullptr );
                    compute_pool = VK_NULL_HANDLE;
                }
                if ( graphics_pool != VK_NULL_HANDLE )
                {
                    vkDestroyCommandPool ( device, graphics_pool, nullptr );
                    graphics_pool = VK_NULL_HANDLE;
                }
            }
        }   // 禁用拷贝以维护 RAII 上下文和显卡资源的唯一性
        VulkanQueue ( const VulkanQueue& ) = delete;
        VulkanQueue& operator= ( const VulkanQueue& ) = delete;

        // 移动语义支持
        VulkanQueue ( VulkanQueue&& ) noexcept = default;
        VulkanQueue& operator= ( VulkanQueue&& ) noexcept = default;


        /**
         * @brief 🚀 显存物理画面一键安全导出为 PNG 磁盘文件
         *
         * @param image_slot 目标导出的 RenderFrame 槽位索引
         * @param file_path 物理磁盘的写入目标路径 (如 "output/frame_0001.png")
         */
        inline void exportImage ( uint32_t image_slot, const std::string& file_path )
        {
            if ( image_slot >= frames.size () ) [[unlikely]]
            {
                throw std::out_of_range ( "VulkanQueue::exportImage: image_slot out of range." );
            }

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();

            // =====================================================================
            // 1. 🚀 先读取并等待成员的 garbage_fence 确定是绿灯状态，直接阻断以确保安全
            // =====================================================================
            if ( garbage_fence != VK_NULL_HANDLE ) [[likely]]
            {
                vkWaitForFences ( device, 1, &garbage_fence, VK_TRUE, UINT64_MAX );
            }

            const RenderFrame& frame = frames[ image_slot ];
            const uint32_t width = frame.width;
            const uint32_t height = frame.height;
            VkImage src_image = frame.image;

            if ( src_image == VK_NULL_HANDLE ) [[unlikely]]
            {
                throw std::runtime_error ( "VulkanQueue::exportImage: Target image is uninitialized." );
            }

            // =====================================================================
            // 2. 在 CPU 侧利用 VMA 分配一块相同尺寸的可读写 Staging 内存 [1.1.2]
            // =====================================================================
            VkDeviceSize buffer_size = static_cast< VkDeviceSize > ( width ) * height * 4;   // RGBA8 格式
            VkBuffer staging_buffer = VK_NULL_HANDLE;
            VmaAllocation staging_allocation = VK_NULL_HANDLE;

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = buffer_size;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            // 开启主机随机访问与一键自动内存映射标志，保障 CPU 读取效率 [1.1.2, 1.2.6]
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo allocationInfo{};
            if ( vmaCreateBuffer ( allocator, &bufferInfo, &allocInfo, &staging_buffer, &staging_allocation,
                                   &allocationInfo ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::exportImage: Failed to allocate CPU staging buffer." );
            }

            // =====================================================================
            // 3. 录制一次性 DMA 拷贝命令
            // =====================================================================
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool = graphics_pool;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer copy_cmd = VK_NULL_HANDLE;
            if ( vkAllocateCommandBuffers ( device, &cmdAllocInfo, &copy_cmd ) != VK_SUCCESS )
            {
                vmaDestroyBuffer ( allocator, staging_buffer, staging_allocation );
                throw std::runtime_error ( "VulkanQueue::exportImage: Failed to allocate copy command buffer." );
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer ( copy_cmd, &beginInfo );

            // 🚀 A. 内存屏障过渡：将图片布局从 COLOR_ATTACHMENT_OPTIMAL 切换至 TRANSFER_SRC_OPTIMAL [1.1.1, 1.1.2]
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = src_image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier ( copy_cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );

            // 🚀 B. 执行硬件级极速图像拷贝
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { width, height, 1 };

            vkCmdCopyImageToBuffer ( copy_cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer, 1,
                                     &region );

            // 🚀 C. 内存屏障还原：为了不污染后续渲染，将图片布局还原回 COLOR_ATTACHMENT_OPTIMAL [1.1.1]
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier ( copy_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &barrier );

            vkEndCommandBuffer ( copy_cmd );

            // =====================================================================
            // 4. 提交拷贝任务，并使用临时 Fence 确保 CPU 阻断完成
            // =====================================================================
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &copy_cmd;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence copy_fence = VK_NULL_HANDLE;
            vkCreateFence ( device, &fenceInfo, nullptr, &copy_fence );

            VkQueue graphics_queue = primaryDevice.getGraphicsQueue ().handle;
            if ( vkQueueSubmit ( graphics_queue, 1, &submitInfo, copy_fence ) != VK_SUCCESS )
            {
                vkDestroyFence ( device, copy_fence, nullptr );
                vkFreeCommandBuffers ( device, graphics_pool, 1, &copy_cmd );
                vmaDestroyBuffer ( allocator, staging_buffer, staging_allocation );
                throw std::runtime_error ( "VulkanQueue::exportImage: Failed to submit copy command buffer." );
            }

            // 阻塞等待显卡内部数据拷贝动作 100% 结束 [1.1.1, 1.2.4]
            vkWaitForFences ( device, 1, &copy_fence, VK_TRUE, UINT64_MAX );

            // 清理临时分配的同步句柄 [1]
            vkDestroyFence ( device, copy_fence, nullptr );
            vkFreeCommandBuffers ( device, graphics_pool, 1, &copy_cmd );

            // =====================================================================
            // 5. 🚀 物理写入磁盘：使用 stb 库直接导出为高精度 PNG
            // =====================================================================
            // 由于 VMA 开启了 MAPPED 属性，直接通过 mapped 物理指针安全读取 [1.1.2]
            const int stride_in_bytes = static_cast< int > ( width ) * 4;

            int write_result =
                stbi_write_png ( file_path.c_str (), static_cast< int > ( width ), static_cast< int > ( height ),
                                 4,   // 4个通道 (RGBA)
                                 allocationInfo.pMappedData, stride_in_bytes );

            // 6. 物理销毁临时 staging buffer，内存归还显存池 [1.1.2, 1.2.6]
            vmaDestroyBuffer ( allocator, staging_buffer, staging_allocation );

            if ( write_result == 0 ) [[unlikely]]
            {
                throw std::runtime_error ( "VulkanQueue::exportImage: Failed to write PNG file to disk." );
            }
        }


        /**
         * @brief 内部辅助函数：一键为 VkImage 创建专用的 2D 视图 (自动识别颜色/深度属性)
         *
         * @param image 待创建视图的物理图像句柄
         * @param format 图像的物理像素格式 (如 VK_FORMAT_R8G8B8A8_UNORM 或 VK_FORMAT_D32_SFLOAT)
         * @return 成功创建并绑定好的物理图像视图句柄 (VkImageView)
         */
        VkImageView createImageView2D ( VkImage image, VkFormat format )
        {
            // 1. 从内部 RAII 上下文中直接获取逻辑设备句柄，保障外部调用零参数负担 [2]
            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;   // 🚀 声明为标准 2D 视图 [1.1.1]
            viewInfo.format = format;                    // 对齐图像格式 [1.1.1]

            // 默认通道映射，不执行额外的通道翻转 (RGBA 1:1 映射) [1.1.1]
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // 🚀 2. 核心：自适应深度与颜色格式判断
            // 针对科学可视化常用的高精度 D32_SFLOAT 格式自动切换 aspectMask 为 DEPTH_BIT [1.1.1]
            if ( format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM ||
                 format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ) [[unlikely]]
            {
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else [[likely]]
            {
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            }

            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;   // 只用单级 Mipmap [1.1.1]
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;   // 只用单层 ArrayLayer [1.1.1]

            VkImageView view = VK_NULL_HANDLE;
            if ( vkCreateImageView ( device, &viewInfo, nullptr, &view ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::createImageView2D: Failed to create VkImageView." );
            }

            return view;
        }


        /**
         * @brief 物理准备并就绪全局共享的 8x MSAA 颜色附件与深度附件 (自适应延迟重建)
         *
         * 只有当当前请求的画布大小与已缓存的附件尺寸不一致时，才会触发同步与 VRAM 重建。
         * 在同分辨率编辑/导出期间，该函数自动退化为 O(1) 无锁缓存命中。
         *
         * @param required_w 目标画布的物理像素宽度
         * @param required_h 目标画布的物理像素高度
         */
        void prepareDynamicAttachments ( uint32_t required_w, uint32_t required_h )
        {
            // 🚀 核心优化 1：若尺寸完全匹配，直接 $O(1)$ 命中跳过，免去任何 Vulkan/VMA 系统调用
            if ( current_msaa_w == required_w && current_msaa_h == required_h ) [[likely]]
            {
                return;
            }

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();

            // 🚀 核心优化 2：物理同步。由于要销毁正在被 GPU 读写的显存，必须强制挂起 CPU 等待设备空闲
            if ( current_msaa_w != 0 && current_msaa_h != 0 )
            {
                vkDeviceWaitIdle ( device );
            }

            // 🚀 3. 清理该位置原有的 8x 颜色附件资源 (防止显存泄漏) [1]
            if ( msaa_color_view != VK_NULL_HANDLE )
            {
                vkDestroyImageView ( device, msaa_color_view, nullptr );
                msaa_color_view = VK_NULL_HANDLE;
            }
            if ( msaa_color_image != VK_NULL_HANDLE )
            {
                vmaDestroyImage ( allocator, msaa_color_image, msaa_color_alloc );
                msaa_color_image = VK_NULL_HANDLE;
                msaa_color_alloc = VK_NULL_HANDLE;
            }

            // 🚀 4. 清理该位置原有的 8x 深度附件资源 (防止显存泄漏) [1]
            if ( msaa_depth_view != VK_NULL_HANDLE )
            {
                vkDestroyImageView ( device, msaa_depth_view, nullptr );
                msaa_depth_view = VK_NULL_HANDLE;
            }
            if ( msaa_depth_image != VK_NULL_HANDLE )
            {
                vmaDestroyImage ( allocator, msaa_depth_image, msaa_depth_alloc );
                msaa_depth_image = VK_NULL_HANDLE;
                msaa_depth_alloc = VK_NULL_HANDLE;
            }

            // 🚀 5. 更新缓存的物理尺寸
            current_msaa_w = required_w;
            current_msaa_h = required_h;

            // =====================================================================
            // A. 创建全新的 8x 颜色瞬态附件 (VK_FORMAT_R8G8B8A8_UNORM) [1.1.1]
            // =====================================================================
            VkImageCreateInfo imgInfo{};
            imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgInfo.imageType = VK_IMAGE_TYPE_2D;
            imgInfo.extent.width = required_w;
            imgInfo.extent.height = required_h;
            imgInfo.extent.depth = 1;
            imgInfo.mipLevels = 1;
            imgInfo.arrayLayers = 1;
            imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;   // 与主画布像素格式对齐 [1.1.1]
            imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // 🚀 声明为 TRANSIENT 瞬态：告诉驱动其不需要保留数据
            imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
            imgInfo.samples = VK_SAMPLE_COUNT_8_BIT;   // 🚀 MSAA 8x [1.2.8]
            imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            // 🚀 声明为 LAZILY_ALLOCATED 延迟分配：允许 GPU 驱使其仅常驻在芯片 Tile/L2 高速缓存中，不消耗显存物理带宽
            // [1.2.4]
            allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            allocInfo.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

            if ( vmaCreateImage ( allocator, &imgInfo, &allocInfo, &msaa_color_image, &msaa_color_alloc, nullptr ) !=
                 VK_SUCCESS )
            {
                throw std::runtime_error (
                    "VulkanQueue::prepareDynamicAttachments: Failed to allocate MSAA color image." );
            }

            // 利用您已有的 2D ImageView 创建接口一键生成视图 [1.1.1]
            msaa_color_view = createImageView2D ( msaa_color_image, VK_FORMAT_R8G8B8A8_UNORM );

            // =====================================================================
            // B. 创建全新的 8x 深度瞬态附件 (VK_FORMAT_D32_SFLOAT) [1.1.1]
            // =====================================================================
            imgInfo.format = VK_FORMAT_D32_SFLOAT;   // 🚀 使用高精度 32 位浮点深度 [1.1.1]
            imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

            if ( vmaCreateImage ( allocator, &imgInfo, &allocInfo, &msaa_depth_image, &msaa_depth_alloc, nullptr ) !=
                 VK_SUCCESS )
            {
                // 容错清理
                vmaDestroyImage ( allocator, msaa_color_image, msaa_color_alloc );
                throw std::runtime_error (
                    "VulkanQueue::prepareDynamicAttachments: Failed to allocate MSAA depth image." );
            }

            msaa_depth_view = createImageView2D ( msaa_depth_image, VK_FORMAT_D32_SFLOAT );
        }


        inline void registerTransientBuffer ( VkBuffer buffer, VmaAllocation allocation )
        {
            garbage_buffers.push_back ( buffer );
            garbage_allocations.push_back ( allocation );
        }

        // =========================================================================
        // 🚀 3. 垃圾物理清扫成员函数 (在每次开始录制前，或程序退出时调用)
        // =========================================================================

        inline void cleanupGarbage ()
        {
            // 🚀 若垃圾账本本就为空，直接秒退，0 开销
            if ( garbage_buffers.empty () ) [[unlikely]]
            {
                return;
            }

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VmaAllocator allocator = primaryDevice.getAllocator ();   // 🚀 仅获取 VMA 分配器即可 [2]

            // 1. 顺着 std::vector 账本，一键物理释放所有 Staging Buffers 显存
            for ( size_t i = 0; i < garbage_buffers.size (); ++i )
            {
                vmaDestroyBuffer ( allocator, garbage_buffers[ i ], garbage_allocations[ i ] );
            }

            // 2. 清空账本，等待下一次垃圾收集
            garbage_buffers.clear ();
            garbage_allocations.clear ();
        }
        inline void ensureEndRecording ()
        {
            if ( graphics_record_state == RecordState::Initial )
            {
                return;
            }

            // 物理调用 vkEndCommandBuffer 合拢指令剧本
            if ( vkEndCommandBuffer ( graphics ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::endRecording: Failed to end command buffer recording." );
            }

            // 迁移状态机状态
            graphics_record_state = RecordState::Initial;
        }


        inline void ensureBeginRecording ()
        {
            if ( graphics_record_state == RecordState::Recording ) [[likely]]
            {
                return;   // 🚀 状态机判定：已经处于录制中，直接零延迟放行！
            }

            // =====================================================================
            // 核心物理动作：在后台默默开启这一代的渲染画布
            // =====================================================================


            // 2. 物理重置当前的图形命令缓冲区
            vkResetCommandBuffer ( graphics, 0 );

            // 3. 物理调用 vkBeginCommandBuffer 开始这一代的录制
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;   // 一次性提交优化

            if ( vkBeginCommandBuffer ( graphics, &beginInfo ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::ensureRecording: Failed to begin command buffer." );
            }

            // 4. 迁移状态机状态
            graphics_record_state = RecordState::Recording;
        }

        inline void submitGraphics ()
        {
            // 1. 自动合拢当前的指令剧本
            ensureEndRecording ();

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VkQueue graphics_queue = primaryDevice.getGraphicsQueue ().handle;

            // 2. 🚀 物理挂起 CPU：等待上一次动作彻底画完，避免发生任何形式的 GPU 读写冲突
            vkWaitForFences ( device, 1, &garbage_fence, VK_TRUE, UINT64_MAX );

            // 3. 🚀 物理释放上一次动作产生并常驻于垃圾箱里的全部 Staging Buffers
            cleanupGarbage ();

            // 4. 🚀 一键重置该唯一常驻栅栏为红灯状态，准备给本次动作提交使用
            vkResetFences ( device, 1, &garbage_fence );

            // 5. 组装并物理提交本次动作的所有指令
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &graphics;

            if ( vkQueueSubmit ( graphics_queue, 1, &submitInfo, garbage_fence ) != VK_SUCCESS )
            {
                throw std::runtime_error ( "VulkanQueue::submitGraphics: Failed to submit graphics command buffer." );
            }
        }


        /**
         * @brief 核心录制函数：执行 AABB 空间收敛、RTE + 2的幂次双精度归一化、以及影子相机重构
         */
        void recordImage_DAGInst ( std::span< DAGObjectInstance* > instances, uint32_t image_slot, Camera& camera )
        {
            if ( instances.empty () ) [[unlikely]]
            {
                return;
            }

            // =====================================================================
            // 1. 累积场景中所有物体的世界空间包围盒 (不进行 std::isinf 过滤，保留无限长物体)
            // =====================================================================
            constexpr double inf = std::numeric_limits< double >::infinity ();
            double total_min_x = inf;
            double total_min_y = inf;
            double total_min_z = inf;
            double total_max_x = -inf;
            double total_max_y = -inf;
            double total_max_z = -inf;

            bool has_valid_bounds = false;

            for ( const DAGObjectInstance* instance : instances )
            {
                if ( !instance || !instance->source ) [[unlikely]]
                {
                    continue;
                }

                // 直接通过 CPU 侧双精度提取原始拓扑 AABB
                AABB3D inst_aabb = computeInstanceAABB ( *instance );

                total_min_x = std::min ( total_min_x, inst_aabb.min_x );
                total_min_y = std::min ( total_min_y, inst_aabb.min_y );
                total_min_z = std::min ( total_min_z, inst_aabb.min_z );

                total_max_x = std::max ( total_max_x, inst_aabb.max_x );
                total_max_y = std::max ( total_max_y, inst_aabb.max_y );
                total_max_z = std::max ( total_max_z, inst_aabb.max_z );

                has_valid_bounds = true;
            }

            AABB3D scene_aabb{};
            if ( has_valid_bounds ) [[likely]]
            {
                scene_aabb = { total_min_x, total_min_y, total_min_z, total_max_x, total_max_y, total_max_z };
            }
            else
            {
                scene_aabb = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
            }

            // =====================================================================
            // 2. 提取并计算相机视锥体在世界空间中的有限 AABB (Frustum World AABB)
            // =====================================================================
            const CameraType cam_type = getCameraType ( camera );
            const Eigen::Matrix4d view_mat = computeCameraViewMatrix ( camera );
            const Eigen::Matrix4d inv_view = view_mat.inverse ();   // View 逆矩阵负责将视锥角点由观察空间反推回世界空间

            // 🚀 提前提取相机绝对世界位置 P_c，用于在发生退化时作为 1000^3 虚拟包围盒的对齐中心点
            double cam_x = 0.0, cam_y = 0.0, cam_z = 0.0;
            if ( cam_type == CameraType::Orthographic )
            {
                const auto* ortho = castCamera< OrthographicCamera > ( camera );
                cam_x = ortho->position[ 0 ];
                cam_y = ortho->position[ 1 ];
                cam_z = ortho->position[ 2 ];
            }
            else
            {
                const auto* persp = castCamera< PerspectiveCamera > ( camera );
                cam_x = persp->position[ 0 ];
                cam_y = persp->position[ 1 ];
                cam_z = persp->position[ 2 ];
            }

            // 声明并初始化统一的视边界描述变量
            double l_near = -1.0, r_near = 1.0, b_near = -1.0, t_near = 1.0;
            double l_far = -1.0, r_far = 1.0, b_far = -1.0, t_far = 1.0;
            double n_clip = 0.1, f_clip = 1000.0;

            // 使用 switch-case 优雅分发不同投影镜头的几何内参
            switch ( cam_type )
            {
                case CameraType::Orthographic:
                {
                    const auto* ortho = castCamera< OrthographicCamera > ( camera );

                    l_near = ortho->left;
                    r_near = ortho->right;
                    b_near = ortho->bottom;
                    t_near = ortho->top;

                    l_far = ortho->left;
                    r_far = ortho->right;
                    b_far = ortho->bottom;
                    t_far = ortho->top;

                    n_clip = ortho->near_clip;
                    f_clip = ortho->far_clip;
                    break;
                }
                case CameraType::Perspective:
                {
                    const auto* persp = castCamera< PerspectiveCamera > ( camera );

                    const double fov_rad = persp->fov_y * 3.14159265358979323846 / 180.0;
                    const double tan_half_fov = std::tan ( fov_rad * 0.5 );

                    const double h_near = persp->near_clip * tan_half_fov;
                    const double w_near = h_near * persp->aspect;

                    const double h_far = persp->far_clip * tan_half_fov;
                    const double w_far = h_far * persp->aspect;

                    l_near = -w_near;
                    r_near = w_near;
                    b_near = -h_near;
                    t_near = h_near;

                    l_far = -w_far;
                    r_far = w_far;
                    b_far = -h_far;
                    t_far = h_far;

                    n_clip = persp->near_clip;
                    f_clip = persp->far_clip;
                    break;
                }
                default:
                {
                    break;
                }
            }

            // 统一对齐至观察空间 Z 轴负方向的 8 个视锥角点
            Eigen::Vector4d view_corners[ 8 ];
            view_corners[ 0 ] = Eigen::Vector4d ( l_near, b_near, -n_clip, 1.0 );
            view_corners[ 1 ] = Eigen::Vector4d ( r_near, b_near, -n_clip, 1.0 );
            view_corners[ 2 ] = Eigen::Vector4d ( l_near, t_near, -n_clip, 1.0 );
            view_corners[ 3 ] = Eigen::Vector4d ( r_near, t_near, -n_clip, 1.0 );
            view_corners[ 4 ] = Eigen::Vector4d ( l_far, b_far, -f_clip, 1.0 );
            view_corners[ 5 ] = Eigen::Vector4d ( r_far, b_far, -f_clip, 1.0 );
            view_corners[ 6 ] = Eigen::Vector4d ( l_far, t_far, -f_clip, 1.0 );
            view_corners[ 7 ] = Eigen::Vector4d ( r_far, t_far, -f_clip, 1.0 );

            // 计算相机视锥在世界空间中的 AABB 包围盒
            double cam_min_x = inf, cam_min_y = inf, cam_min_z = inf;
            double cam_max_x = -inf, cam_max_y = -inf, cam_max_z = -inf;

            for ( int i = 0; i < 8; ++i )
            {
                Eigen::Vector4d w_pt = inv_view * view_corners[ i ];
                cam_min_x = std::min ( cam_min_x, w_pt.x () );
                cam_min_y = std::min ( cam_min_y, w_pt.y () );
                cam_min_z = std::min ( cam_min_z, w_pt.z () );

                cam_max_x = std::max ( cam_max_x, w_pt.x () );
                cam_max_y = std::max ( cam_max_y, w_pt.y () );
                cam_max_z = std::max ( cam_max_z, w_pt.z () );
            }

            // =====================================================================
            // 3. 场景 AABB 与 相机 AABB 求交，将无限长物体裁剪收敛为有限边界
            // =====================================================================
            double final_min_x = std::max ( scene_aabb.min_x, cam_min_x );
            double final_min_y = std::max ( scene_aabb.min_y, cam_min_y );
            double final_min_z = std::max ( scene_aabb.min_z, cam_min_z );

            double final_max_x = std::min ( scene_aabb.max_x, cam_max_x );
            double final_max_y = std::min ( scene_aabb.max_y, cam_max_y );
            double final_max_z = std::min ( scene_aabb.max_z, cam_max_z );

            // 🚀 核心改动：若求交后发生退化（如无交集），直接视为以当前相机为中心的 1000*1000*1000 虚拟包围盒
            if ( final_min_x > final_max_x || final_min_y > final_max_y || final_min_z > final_max_z ) [[unlikely]]
            {
                final_min_x = cam_x - 500.0;
                final_max_x = cam_x + 500.0;
                final_min_y = cam_y - 500.0;
                final_max_y = cam_y + 500.0;
                final_min_z = cam_z - 500.0;
                final_max_z = cam_z + 500.0;
            }


            AABB3D final_aabb = { final_min_x, final_min_y, final_min_z, final_max_x, final_max_y, final_max_z };

            // 🚀 开启后台指令录制通道
            if ( final_aabb.max_x - final_aabb.min_x < 1e-3 )
            {
                final_aabb.min_x -= 0.5;
                final_aabb.max_x += 0.5;
            }
            if ( final_aabb.max_y - final_aabb.min_y < 1e-3 )
            {
                final_aabb.min_y -= 0.5;
                final_aabb.max_y += 0.5;
            }
            if ( final_aabb.max_z - final_aabb.min_z < 1e-3 )
            {
                final_aabb.min_z -= 0.5;
                final_aabb.max_z += 0.5;
            }

            ensureBeginRecording ();

            // =====================================================================
            // 4. 🚀 空间统一核心：计算 CPU 端双精度归一化变换矩阵 & 重构影子相机
            // =====================================================================

            // 计算在相对相机空间下，可见裁剪区（final_aabb）的最大绝对值边界
            double rel_min_x = final_aabb.min_x - cam_x;
            double rel_max_x = final_aabb.max_x - cam_x;
            double rel_min_y = final_aabb.min_y - cam_y;
            double rel_max_y = final_aabb.max_y - cam_y;
            double rel_min_z = final_aabb.min_z - cam_z;
            double rel_max_z = final_aabb.max_z - cam_z;

            double max_val = std::max ( { std::abs ( rel_min_x ), std::abs ( rel_max_x ), std::abs ( rel_min_y ),
                                          std::abs ( rel_max_y ), std::abs ( rel_min_z ), std::abs ( rel_max_z ) } );

            // 🚀 2 的幂次向上无损缩放：保证缩放因子在二进制层面仅仅是指数偏移，完全不损失 double 的原始尾数精度 [1.1.2]
            double S_p2 = std::pow ( 2.0, std::ceil ( std::log2 ( max_val > 1e-9 ? max_val : 1.0 ) ) );
            double scale = 1.0 / S_p2;

            // 构造双精度的 4x4 空间统一变换矩阵 M_norm (平移至相机位置，并等比例缩小)
            Eigen::Matrix4d M_norm = Eigen::Matrix4d::Identity ();
            M_norm ( 0, 0 ) = scale;
            M_norm ( 1, 1 ) = scale;
            M_norm ( 2, 2 ) = scale;
            M_norm ( 0, 3 ) = -scale * cam_x;   // 🚀 RTE 平移与缩放完美合流
            M_norm ( 1, 3 ) = -scale * cam_y;
            M_norm ( 2, 3 ) = -scale * cam_z;

            // 🚀 影子相机重构：在 CPU 栈上创建并缩放新相机参数，完美克隆原本的一切视界，不污染用户的原始相机 [1.1.2]
            Eigen::Matrix4d view_mat_norm = Eigen::Matrix4d::Identity ();
            Eigen::Matrix4d proj_mat_norm = Eigen::Matrix4d::Identity ();

            switch ( cam_type )
            {
                case CameraType::Orthographic:
                {
                    const auto* orig_ortho = castCamera< OrthographicCamera > ( camera );
                    OrthographicCamera adj_ortho = *orig_ortho;   // 栈拷贝

                    for ( int j = 0; j < 3; ++j )
                    {
                        double c_val = ( j == 0 ) ? cam_x : ( j == 1 ? cam_y : cam_z );
                        adj_ortho.position[ j ] = ( orig_ortho->position[ j ] - c_val ) * scale;
                        adj_ortho.target[ j ] = ( orig_ortho->target[ j ] - c_val ) * scale;
                    }

                    adj_ortho.left *= scale;
                    adj_ortho.right *= scale;
                    adj_ortho.bottom *= scale;
                    adj_ortho.top *= scale;
                    adj_ortho.near_clip *= scale;
                    adj_ortho.far_clip *= scale;

                    view_mat_norm = computeViewMatrixHelper ( adj_ortho.position, adj_ortho.target, adj_ortho.up );

                    // 🚀 核心重构 A：Vulkan 标准正交投影公式 (近平面映射到 0.0，远平面映射到 1.0)
                    Eigen::Matrix4d proj = Eigen::Matrix4d::Zero ();
                    proj ( 0, 0 ) = 2.0 / ( adj_ortho.right - adj_ortho.left );
                    proj ( 1, 1 ) = 2.0 / ( adj_ortho.top - adj_ortho.bottom );
                    proj ( 2, 2 ) = -1.0 / ( adj_ortho.far_clip - adj_ortho.near_clip );   // 🚀 Vulkan 1.4 深度对齐
                    proj ( 0, 3 ) = -( adj_ortho.right + adj_ortho.left ) / ( adj_ortho.right - adj_ortho.left );
                    proj ( 1, 3 ) = -( adj_ortho.top + adj_ortho.bottom ) / ( adj_ortho.top - adj_ortho.bottom );
                    proj ( 2, 3 ) =
                        -adj_ortho.near_clip / ( adj_ortho.far_clip - adj_ortho.near_clip );   // 🚀 Vulkan 1.4 深度对齐
                    proj ( 3, 3 ) = 1.0;
                    proj_mat_norm = proj;
                    break;
                }
                case CameraType::Perspective:
                {
                    const auto* orig_persp = castCamera< PerspectiveCamera > ( camera );
                    PerspectiveCamera adj_persp = *orig_persp;   // 栈拷贝

                    for ( int j = 0; j < 3; ++j )
                    {
                        double c_val = ( j == 0 ) ? cam_x : ( j == 1 ? cam_y : cam_z );
                        adj_persp.position[ j ] = ( orig_persp->position[ j ] - c_val ) * scale;
                        adj_persp.target[ j ] = ( orig_persp->target[ j ] - c_val ) * scale;
                    }

                    adj_persp.near_clip *= scale;
                    adj_persp.far_clip *= scale;

                    view_mat_norm = computeViewMatrixHelper ( adj_persp.position, adj_persp.target, adj_persp.up );

                    // 🚀 核心重构 B：Vulkan 标准透视投影公式 (近平面映射到 0.0，远平面映射到 1.0)
                    const double tan_half_fov = std::tan ( ( adj_persp.fov_y * 3.14159265358979323846 / 180.0 ) * 0.5 );
                    Eigen::Matrix4d proj = Eigen::Matrix4d::Zero ();
                    proj ( 0, 0 ) = 1.0 / ( adj_persp.aspect * tan_half_fov );
                    proj ( 1, 1 ) = 1.0 / tan_half_fov;
                    proj ( 2, 2 ) =
                        -adj_persp.far_clip / ( adj_persp.far_clip - adj_persp.near_clip );   // 🚀 Vulkan 1.4 深度对齐
                    proj ( 2, 3 ) = -( adj_persp.far_clip * adj_persp.near_clip ) /
                                    ( adj_persp.far_clip - adj_persp.near_clip );   // 🚀 Vulkan 1.4 深度对齐
                    proj ( 3, 2 ) = -1.0;
                    proj_mat_norm = proj;
                    break;
                }
                default:
                    break;
            }
            std::vector< DAGObject* > unique_objects;
            unique_objects.reserve ( instances.size () );   // 预留最大可能容量

            for ( const DAGObjectInstance* instance : instances )
            {
                if ( !instance || !instance->source ) [[unlikely]]
                {
                    continue;
                }

                DAGObject* obj = instance->source;

                // 极速去重查询 (对于小规模物体，std::find 线性扫描具有最佳的 CPU 缓存局域性)
                if ( std::find ( unique_objects.begin (), unique_objects.end (), obj ) == unique_objects.end () )
                {
                    unique_objects.push_back ( obj );
                }
            }
            const RenderFrame& target_frame = frames[ image_slot ];
            const uint32_t width = target_frame.width;
            const uint32_t height = target_frame.height;


            // 🚀 核心改动：在下面使用全局附件变量之前，运行此自适应函数对齐附件尺寸
            prepareDynamicAttachments ( width, height );
            const Eigen::Matrix4f VP_norm_float = ( proj_mat_norm * view_mat_norm ).cast< float > ();
            // =====================================================================
            // 6. 🚀 核心：直接派发 Object 本身，不触碰任何 instances 提取
            // =====================================================================
            for ( DAGObject* obj : unique_objects )
            {
                switch ( obj->type )
                {
                    case NodeType::POINT_2D_FREE:
                    case NodeType::POINT_2D_MID:
                    case NodeType::POINT_2D_SECTION:
                    case NodeType::POINT_2D_INTERSECT:
                    case NodeType::POINT_2D_SNAP:
                    case NodeType::POINT_3D_FREE:
                    case NodeType::POINT_3D_MID:
                    case NodeType::POINT_3D_SECTION:
                    case NodeType::POINT_3D_INTERSECT:
                    case NodeType::POINT_3D_SNAP:
                    {
                        // 🚀 1. 派发 Object 对象本身（通过引用）
                        // 内部会自己去提取 obj->instances 数组并执行离散化、配置模型矩阵、绑定着色器
                        recordImage_DAGInstPoint ( *obj, M_norm, VP_norm_float, image_slot );
                        break;
                    }
                    case NodeType::LINE_2D_SEGMENT:
                    case NodeType::LINE_3D_SEGMENT:
                    {
                        // 🚀 2. 派发线段 Object 录制（后续实现）
                        // recordImage_DAGInstLine ( *obj, final_aabb, camera, image_slot );
                        break;
                    }
                    default:
                        break;
                }
            }
        }


    private:

        // 🚀 声明顺序极其严密：先声明 ctx，后声明 pool。
        // 这保证了在 ~VulkanQueue() 执行结束前，ctx 不会被提早销毁。
        Vulkan::VulkanContext ctx;


        // =====================================================================
        // 🚀 新增：全局唯一的 MSAA 8x 颜色与深度瞬态附件 (常驻显存，支持动态重建)
        // =====================================================================
        VkImage msaa_color_image = VK_NULL_HANDLE;         ///< 8x 颜色物理图像
        VkImageView msaa_color_view = VK_NULL_HANDLE;      ///< 8x 颜色视图 (用于 vkCmdBeginRendering 绑定) [1.1.1]
        VmaAllocation msaa_color_alloc = VK_NULL_HANDLE;   ///< 8x 颜色内存分配

        VkImage msaa_depth_image = VK_NULL_HANDLE;         ///< 8x 深度物理图像
        VkImageView msaa_depth_view = VK_NULL_HANDLE;      ///< 8x 深度视图 (用于 vkCmdBeginRendering 绑定) [1.1.1]
        VmaAllocation msaa_depth_alloc = VK_NULL_HANDLE;   ///< 8x 深度内存分配

        // 用于追踪当前这套草稿纸的物理尺寸，防止重复重建 [1.1.1]
        uint32_t current_msaa_w = 0;   ///< 当前草稿纸物理宽度
        uint32_t current_msaa_h = 0;   ///< 当前草稿纸物理高度

        VkPipelineCache pipeline_cache = VK_NULL_HANDLE;   ///< 传统管线缓存 (用于光追/计算管线)
        std::vector< VkShaderEXT > shader_objects;         ///< 现代着色器对象缓存 (VkShaderEXT 集合) [1.1.3]

        VkCommandPool graphics_pool = VK_NULL_HANDLE;
        VkCommandPool compute_pool = VK_NULL_HANDLE;

        std::vector< VkBuffer > garbage_buffers;            ///< 🚀 统一使用 std::vector 承载的垃圾 Buffer 句柄列表
        std::vector< VmaAllocation > garbage_allocations;   ///< 🚀 统一使用 std::vector 承载的垃圾内存分配句柄列表
        VkFence garbage_fence = VK_NULL_HANDLE;             ///< 🚀 唯一的硬件监控栅栏
        enum class RecordState : uint8_t
        {
            Initial,    ///< 初始静默状态（已提交或已重置，未开启录制）
            Recording   ///< 录制进行状态（已执行 vkBegin，正在不断流入绘制命令）
        };
        RecordState graphics_record_state = RecordState::Initial;
        VkPipelineLayout pipeline_layout;

        // 🚀 Vulkan 扩展函数指针 (构造函数中一次性加载，避免反复动态查询)
        PFN_vkCreateShadersEXT pfnCreateShaders = nullptr;
        PFN_vkGetShaderBinaryDataEXT pfnGetShaderBinaryData = nullptr;
        PFN_vkDestroyShaderEXT pfnDestroyShader = nullptr;
        PFN_vkCmdSetPolygonModeEXT pfnCmdSetPolygonMode = nullptr;
        PFN_vkCmdSetRasterizationSamplesEXT pfnCmdSetRasterizationSamples = nullptr;
        PFN_vkCmdSetSampleMaskEXT pfnCmdSetSampleMask = nullptr;
        PFN_vkCmdSetAlphaToCoverageEnableEXT pfnCmdSetAlphaToCoverageEnable = nullptr;
        PFN_vkCmdSetVertexInputEXT pfnCmdSetVertexInput = nullptr;
        PFN_vkCmdSetColorBlendEnableEXT pfnCmdSetColorBlendEnable = nullptr;
        PFN_vkCmdSetColorBlendEquationEXT pfnCmdSetColorBlendEquation = nullptr;
        PFN_vkCmdSetColorWriteMaskEXT pfnCmdSetColorWriteMask = nullptr;
        PFN_vkCmdBindShadersEXT pfnCmdBindShaders = nullptr;
        PFN_vkGetBufferDeviceAddress pfnGetBufferDeviceAddress = nullptr;

        struct AppearanceGroup
        {
            AppearanceType type;
            const AppearanceSimplePoint* appearance_ptr = nullptr;
            std::vector< DAGObjectInstance* > instances;
        };

        struct InstanceTransformData
        {
            Eigen::Vector3d local_pt;
            Eigen::Matrix4f M_norm_model_float;
        };

        /**
         * @brief 将 MSAA 颜色/深度图像及解析目标图像从 VK_IMAGE_LAYOUT_UNDEFINED
         *        过渡到对应的最佳写入布局（颜色附件/深度模板附件）
         * @param cmd 目标命令缓冲区
         * @param target_image 解析目标图像 (1x resolve target)
         */
        inline void transitionMSAAImages ( VkCommandBuffer cmd, VkImage target_image ) const
        {
            VkImageMemoryBarrier barriers[ 3 ]{};

            // 1. 过渡 8x MSAA 颜色附件 (msaa_color_image)
            barriers[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[ 0 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[ 0 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[ 0 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 0 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 0 ].image = msaa_color_image;
            barriers[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[ 0 ].subresourceRange.baseMipLevel = 0;
            barriers[ 0 ].subresourceRange.levelCount = 1;
            barriers[ 0 ].subresourceRange.baseArrayLayer = 0;
            barriers[ 0 ].subresourceRange.layerCount = 1;
            barriers[ 0 ].srcAccessMask = 0;
            barriers[ 0 ].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            // 2. 过渡 8x MSAA 深度附件 (msaa_depth_image)
            barriers[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[ 1 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[ 1 ].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barriers[ 1 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 1 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 1 ].image = msaa_depth_image;
            barriers[ 1 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            barriers[ 1 ].subresourceRange.baseMipLevel = 0;
            barriers[ 1 ].subresourceRange.levelCount = 1;
            barriers[ 1 ].subresourceRange.baseArrayLayer = 0;
            barriers[ 1 ].subresourceRange.layerCount = 1;
            barriers[ 1 ].srcAccessMask = 0;
            barriers[ 1 ].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            // 3. 过渡 1x 解析目标 (target_image)
            barriers[ 2 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[ 2 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[ 2 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[ 2 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 2 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[ 2 ].image = target_image;
            barriers[ 2 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[ 2 ].subresourceRange.baseMipLevel = 0;
            barriers[ 2 ].subresourceRange.levelCount = 1;
            barriers[ 2 ].subresourceRange.baseArrayLayer = 0;
            barriers[ 2 ].subresourceRange.layerCount = 1;
            barriers[ 2 ].srcAccessMask = 0;
            barriers[ 2 ].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier (
                cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                nullptr, 0, nullptr, 3, barriers );
        }

        /**
         * @brief 将实例列表按外观类型 (AppearanceType) 分组为二维列表
         * @param instances 实例指针列表
         * @return 按 AppearanceType 分组后的 AppearanceGroup 向量
         */
        inline std::vector< AppearanceGroup > groupInstancesByAppearance (
            utils::TinyVector< DAGObjectInstance* >& instances ) const
        {
            std::vector< AppearanceGroup > groups;
            groups.reserve ( 4 );

            for ( DAGObjectInstance* instance : instances )
            {
                if ( !instance )
                    continue;

                // 🚀 如果 instance->appearance 为空，直接抛出异常，不再执行默认外观兜底
                if ( !instance->appearance ) [[unlikely]]
                {
                    throw std::runtime_error (
                        "VulkanQueue::groupInstancesByAppearance: DAGObjectInstance has a null appearance pointer." );
                }

                const auto* app = static_cast< const AppearanceSimplePoint* > ( instance->appearance );

                auto it = std::find_if ( groups.begin (), groups.end (),
                                         [ app ] ( const AppearanceGroup& g ) { return g.type == app->type; } );

                if ( it != groups.end () )
                {
                    it->instances.push_back ( instance );
                }
                else
                {
                    AppearanceGroup new_group{};
                    new_group.type = app->type;
                    new_group.appearance_ptr = app;
                    new_group.instances.push_back ( instance );
                    groups.push_back ( std::move ( new_group ) );
                }
            }

            return groups;
        }

        inline InstanceTransformData computeInstanceTransform ( DAGObjectInstance* instance,
                                                                const DAGObject& object,
                                                                const Eigen::Matrix4d& M_norm ) const
        {
            const Eigen::Vector3d local_pt = extractLocalPoint ( object );

            const Eigen::Quaterniond rot_q ( instance->world_rotation[ 3 ],   // w
                                             instance->world_rotation[ 0 ],   // x
                                             instance->world_rotation[ 1 ],   // y
                                             instance->world_rotation[ 2 ]    // z
            );
            const Eigen::Matrix3d S_mat =
                Eigen::Vector3d ( instance->world_scales[ 0 ], instance->world_scales[ 1 ],
                                  instance->world_scales[ 2 ] )
                    .asDiagonal ();

            Eigen::Matrix4d M_world = Eigen::Matrix4d::Identity ();
            M_world.block< 3, 3 > ( 0, 0 ) = rot_q.normalized ().toRotationMatrix () * S_mat;
            M_world.block< 3, 1 > ( 0, 3 ) =
                Eigen::Vector3d ( instance->world_position[ 0 ], instance->world_position[ 1 ],
                                  instance->world_position[ 2 ] );

            Eigen::Matrix4d M_norm_model = M_norm * M_world;
            Eigen::Matrix4f M_norm_model_float = M_norm_model.cast< float > ();

            return { local_pt, M_norm_model_float };
        }

        /**
         * @brief 通用 GPU staging buffer 创建与上传：分配、拷贝、获取显存地址、注册为垃圾
         *
         * 封装 VMA buffer 创建、数据上传、vmaMapMemory/设备地址查询、临时生存期注册的完整流程。
         * 调用方预先将数据打包为连续内存块传入即可。
         *
         * @param allocator  VMA 分配器
         * @param device     Vulkan 逻辑设备
         * @param buffer_size 缓冲区总字节数
         * @param data        待上传的连续数据指针
         * @return VkDeviceAddress  GPU 可寻址的设备地址
         */
        inline VkDeviceAddress createAndUploadStagingBuffer ( VmaAllocator allocator, VkDevice device,
                                                              size_t buffer_size, const void* data )
        {
            VkBuffer staging_buffer = VK_NULL_HANDLE;
            VmaAllocation staging_allocation = VK_NULL_HANDLE;

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = buffer_size;
            bufferInfo.usage =
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo allocationInfo{};
            if ( vmaCreateBuffer ( allocator, &bufferInfo, &allocInfo, &staging_buffer,
                                   &staging_allocation, &allocationInfo ) != VK_SUCCESS )
            {
                throw std::runtime_error (
                    "VulkanQueue::createAndUploadStagingBuffer: Failed to allocate GPU staging buffer." );
            }

            std::memcpy ( allocationInfo.pMappedData, data, buffer_size );

            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = staging_buffer;

            VkDeviceAddress gpu_address = pfnGetBufferDeviceAddress ( device, &addressInfo );

            registerTransientBuffer ( staging_buffer, staging_allocation );

            return gpu_address;
        }

        /**
         * @brief 配置 SimplePoint 外观的渲染通道：视口、裁减、RenderPass 附件、MSAA 转换、动态状态、混合状态
         * @param width  渲染目标宽度
         * @param height 渲染目标高度
         * @param target_frame 渲染帧 (提供 resolve 视图和图像)
         */
        inline void setupSimplePointRenderPass ( uint32_t width, uint32_t height, const RenderFrame& target_frame )
        {
            // 配置动态视口与剪裁区域 (使用 target_frame 真实的宽高信息) [1.1.1]
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = static_cast< float > ( height );
            viewport.width = static_cast< float > ( width );
            viewport.height = -static_cast< float > ( height );   // Y 轴翻转，左下角为原点 [1.1.4]
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = { width, height };

            // 物理动态设置 (着色器对象必须使用带 Count 接口)
            vkCmdSetViewportWithCount ( graphics, 1, &viewport );
            vkCmdSetScissorWithCount ( graphics, 1, &scissor );

            // 物理配置动态渲染 Render Pass [1.1.1, 1.2.9]
            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = { 0, 0 };
            renderingInfo.renderArea.extent = { width, height };
            renderingInfo.layerCount = 1;

            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = msaa_color_view;   // 🚀 直接使用成员变量 [1.1.1]
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 一键清屏
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.resolveImageView =
                target_frame.view;   // 🚀 1x 解析目标 (frames[image_slot].view) [1.1.1, 1.2.9]
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;   // 🚀 显式声明解析目标写入布局
            colorAttachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

            VkRenderingAttachmentInfo depthAttachment{};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = msaa_depth_view;   // 🚀 直接使用成员变量 [1.1.1]
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = &depthAttachment;

            // =====================================================================
            // 🚀 3路合并物理布局转换屏障 (一键解决 8x颜色、8x深度、1x解析目标的未定义状态)
            // =====================================================================
            transitionMSAAImages ( graphics, target_frame.image );
            // 开始录制通道
            vkCmdBeginRendering ( graphics, &renderingInfo );

            // 🚀 动态状态配置：一键动态声明和补齐 Shader Objects 缺失的全部寄存器状态！ [1.1.2]
            vkCmdSetRasterizerDiscardEnable ( graphics, VK_FALSE );
            vkCmdSetPrimitiveTopology ( graphics, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST );
            // 🚀 临时强行关闭深度测试，进行诊断测试
            // 🚀 临时强行关闭深度测试，进行诊断测试
            vkCmdSetDepthTestEnable ( graphics, VK_FALSE );
            vkCmdSetDepthWriteEnable ( graphics, VK_FALSE );
            vkCmdSetDepthCompareOp ( graphics, VK_COMPARE_OP_LESS );


            vkCmdSetStencilTestEnable ( graphics, VK_FALSE );
            vkCmdSetDepthBiasEnable ( graphics, VK_FALSE );
            vkCmdSetPrimitiveRestartEnable ( graphics, VK_FALSE );
            vkCmdSetCullMode ( graphics, VK_CULL_MODE_NONE );
            vkCmdSetFrontFace ( graphics, VK_FRONT_FACE_COUNTER_CLOCKWISE );

            // 使用构造函数中已加载的函数指针成员设置扩展动态状态
            pfnCmdSetPolygonMode ( graphics, VK_POLYGON_MODE_FILL );
            pfnCmdSetRasterizationSamples ( graphics, VK_SAMPLE_COUNT_8_BIT );
            {
                VkSampleMask mask = 0xFFFFFFFF;   // 允许所有 8 个采样点写入
                pfnCmdSetSampleMask ( graphics, VK_SAMPLE_COUNT_8_BIT, &mask );
            }
            pfnCmdSetAlphaToCoverageEnable ( graphics, VK_FALSE );
            pfnCmdSetVertexInput ( graphics, 0, nullptr, 0, nullptr );

            // 配置混合状态 (使用成员函数指针)
            {
                VkBool32 blend_enable = VK_TRUE;
                pfnCmdSetColorBlendEnable ( graphics, 0, 1, &blend_enable );

                VkColorBlendEquationEXT blend_eq{};
                blend_eq.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                blend_eq.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                blend_eq.colorBlendOp = VK_BLEND_OP_ADD;
                blend_eq.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blend_eq.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                blend_eq.alphaBlendOp = VK_BLEND_OP_ADD;
                pfnCmdSetColorBlendEquation ( graphics, 0, 1, &blend_eq );

                VkColorComponentFlags write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                pfnCmdSetColorWriteMask ( graphics, 0, 1, &write_mask );
            }
        }

        /**
         * @brief 点云材质一键批量录制函数 (SDF Billboarding 实例化分发)
         *
         * @param object 当前被派发的点几何对象 (内含 utils::TinyVector<DAGObjectInstance*> instances) [1]
         * @param M_norm 图像下统一共享的 3D RTE 归一化平移缩放矩阵 (double 精度)
         * @param VP_norm_float 归一化影子相机的单精度观察-投影矩阵 (float 精度)
         * @param image_slot 目标写入的 RenderFrame 插槽索引
         */
        inline void recordImage_DAGInstPoint ( DAGObject& object, const Eigen::Matrix4d& M_norm,
                                               const Eigen::Matrix4f& VP_norm_float, uint32_t image_slot )
        {
            if ( graphics == VK_NULL_HANDLE || object.instances.empty () ) [[unlikely]]
            {
                return;
            }

            if ( image_slot >= frames.size () ) [[unlikely]]
            {
                throw std::out_of_range ( "VulkanQueue::recordImage_DAGInstPoint: image_slot out of range." );
            }

            const auto& primaryDevice = ctx.getPrimaryDevice ();
            VkDevice device = primaryDevice.get ();
            VmaAllocator allocator = primaryDevice.getAllocator ();

            // =====================================================================
            // 🚀 核心改动：直接从指定 slot 槽位的 RenderFrame 中，提取其物理图片真实的宽和高 [2]
            // =====================================================================
            const RenderFrame& target_frame = frames[ image_slot ];
            const uint32_t width = target_frame.width;
            const uint32_t height = target_frame.height;


            // =====================================================================
            // 1. 提取 Instances 列表并根据外观 (Appearance) 进行归类分组 (构造二维数组) [1]
            // =====================================================================
            std::vector< AppearanceGroup > groups = groupInstancesByAppearance ( object.instances );

            // =====================================================================
            // 2. 循环遍历二维分组，执行基于材质状态的分发与绘制
            // =====================================================================
            for ( const auto& group : groups )
            {
                switch ( group.type )
                {
                    // 点外观：SimplePoint 分支 case (兼容 SimpleLine 命名)
                    // =====================================================================
                    // 🚀 点外观分发：完美兼容 SimplePoint 和 SimpleLine 穿透，绝不漏掉任何默认粒子
                    // =====================================================================

                    case AppearanceType::SimplePoint:
                    {
                        setupSimplePointRenderPass ( width, height, target_frame );

                        // 绑定着色器：顶点是 0 号，片元是 1 号
                        VkShaderStageFlagBits stages[ 2 ] = { VK_SHADER_STAGE_VERTEX_BIT,
                                                              VK_SHADER_STAGE_FRAGMENT_BIT };
                        VkShaderEXT bound_shaders[ 2 ] = { shader_objects[ 0 ], shader_objects[ 1 ] };

                        if ( !pfnCmdBindShaders ) [[unlikely]]
                        {
                            throw std::runtime_error ( "VulkanQueue: Driver does not support vkCmdBindShadersEXT." );
                        }
                        pfnCmdBindShaders ( graphics, 2, stages, bound_shaders );

                        // 开始顺序录制并绘制该外观下的每个实例
                        for ( DAGObjectInstance* instance : group.instances )
                        {
                            auto [ local_pt, M_norm_model_float ] = computeInstanceTransform ( instance, object, M_norm );

                            PointData pt{};
                            pt.worldPos[ 0 ] = static_cast< float > ( local_pt.x () );
                            pt.worldPos[ 1 ] = static_cast< float > ( local_pt.y () );
                            pt.worldPos[ 2 ] = static_cast< float > ( local_pt.z () );
                            pt._pad0 = 0.0f;
                            pt.color[ 0 ] = group.appearance_ptr->red;
                            pt.color[ 1 ] = group.appearance_ptr->green;
                            pt.color[ 2 ] = group.appearance_ptr->blue;
                            pt.color[ 3 ] = group.appearance_ptr->alpha;

                            constexpr size_t header_size = 64;
                            constexpr size_t payload_size = sizeof ( PointData );
                            constexpr size_t total_size = header_size + payload_size;

                            std::array< char, total_size > staging_data;
                            std::memcpy ( staging_data.data (), M_norm_model_float.data (), header_size );
                            std::memcpy ( staging_data.data () + header_size, &pt, payload_size );

                            VkDeviceAddress gpu_address = createAndUploadStagingBuffer (
                                allocator, device, total_size, staging_data.data () );

                            PointSDFConstants pc{};
                            std::memcpy ( pc.vp, VP_norm_float.data (), 16 * sizeof ( float ) );
                            pc.imageSize[ 0 ] = static_cast< float > ( width );
                            pc.imageSize[ 1 ] = static_cast< float > ( height );
                            pc.pointRadius = group.appearance_ptr->radius;
                            pc.cloud = gpu_address;

                            vkCmdPushConstants ( graphics, pipeline_layout,
                                                 static_cast< VkShaderStageFlags > ( VK_SHADER_STAGE_VERTEX_BIT |
                                                                                     VK_SHADER_STAGE_FRAGMENT_BIT ),
                                                 0, sizeof ( PointSDFConstants ), &pc );

                            vkCmdDraw ( graphics, 3, 1, 0, 0 );
                        }

                        vkCmdEndRendering ( graphics );
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    };
}   // namespace StuCanvas
