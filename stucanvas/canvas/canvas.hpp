// stucanvas/canvas/canvas.hpp
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <utility>

#include "../types/point.hpp"         // Point3D_GPU
#include "../types/segment_strip.hpp"  // SegmentStrip3D_GPU
#include "../types/triangles.hpp"      // Triangles3D_GPU
#include "../types/path.hpp"           // Path3D_GPU
#include <vulkan/vulkan.h>
#include "../canvas/vulkan/swap_chains.hpp"
#include "../canvas/vulkan/renderpass.hpp"
#include "../canvas/vulkan/render_pipeline.hpp"
#include "../canvas/vulkan/shader_module.hpp"
#include "../canvas/vulkan/buffer.hpp"
#include "../canvas/vulkan/present.hpp"
#include "../utils/block_deque.hpp"
#include "../canvas/vulkan/offscreen_target.hpp"
#include "../utils/ivf_writer.hpp"
#include "../utils/av1_writer.hpp"
#include "../canvas/vulkan/compute_pipeline.hpp"
#include "../canvas/vulkan/nvenc_cuda_encoder.hpp"
#include "../canvas/vulkan/config.hpp"
#include "../external/nvidia/cuviddec.h"
#include "../external/nvidia/nvcuvid.h"
#include "../external/nvidia/nvEncodeAPI.h"

namespace StuCanvas
{


    namespace Vulkan
    {

        struct IVFHeader {
            char signature[4] = {'D', 'K', 'I', 'F'}; // signature
            uint16_t version = 0;
            uint16_t header_size = 32;
            char fourcc[4] = {'A', 'V', '0', '1'};   // AV1
            uint16_t width{};
            uint16_t height{};
            uint32_t frame_rate_num{};
            uint32_t frame_rate_den = 1;
            uint32_t total_frames{};
            uint32_t reserved = 0;
        };

        struct CanvasPushConstants {
            float mvp[16];         // Offset 0,   Size 64
            float camPos[3];       // Offset 64,  Size 12
            float _pad0;           // Offset 76,  Size 4  (显式对齐)
            float viewportSize[2]; // Offset 80,  Size 8
            float pointRadius;     // Offset 88,  Size 4
            float _pad1;           // Offset 92,  Size 4  (补齐至 96 字节，对齐 16)
        };
        struct CanvasShaderGroup
        {
            ShaderModule vertPoints, fragPoints;
            ShaderModule vertSegments, fragSegments;
            ShaderModule vertPaths, fragPaths;
            ShaderModule vertTriangles, fragTriangles;
        };



        struct CanvasPipelineGroup
        {
            std::unique_ptr<Pipeline> points;
            std::unique_ptr<Pipeline> segments;
            std::unique_ptr<Pipeline> paths;
            std::unique_ptr<Pipeline> triangles;
        };

        struct CanvasBufferGroup
        {
            Buffer points;
            Buffer segments;
            Buffer paths;
            Buffer triangleVertices;
            Buffer triangleIndices;
        };

        struct CanvasDescriptorGroup
        {
            VkDescriptorPool pool = VK_NULL_HANDLE;
            VkDescriptorSet points = VK_NULL_HANDLE;
            VkDescriptorSet segments = VK_NULL_HANDLE;
            VkDescriptorSet paths = VK_NULL_HANDLE;
            VkDescriptorSet triangles = VK_NULL_HANDLE;

            // 辅助：销毁池（集会自动释放）
            void cleanup(VkDevice device)
            {
                if (pool != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(device, pool, nullptr);
                    pool = VK_NULL_HANDLE;
                }
            }
        };
    }

    template <typename T>
    struct ProcessedFrameData
    {
        std::vector<PointDataGPU> allPoints;
        std::vector<SegmentGPU> allSegments;
        std::vector<PointDataGPU> allPathPoints;
        std::vector<PointDataGPU> allTriVertices;
        std::vector<uint32_t> allTriIndices;
        T centerX{}, centerY{}, centerZ{}, scale{};

        [[nodiscard]] bool isEmpty() const
        {
            return allPoints.empty() && allSegments.empty() && allPathPoints.empty() && allTriVertices.empty();
        }
    };



    struct CanvasSettings
    {
        // --- 全局渲染质量 ---
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        bool enableSampleShading = true;
        float minSampleShading = 0.5f;

        // --- 视频导出设置 ---
        Vulkan::ExportSettings exportConfigs;
    };

    /**
     * @brief NLE 片段，拥有自己的时间区间和图形数据。
     * @tparam T 世界坐标数值类型（float / double），目前未在数据结构中直接使用，
     *           但为将来扩展保留。
     */
    template <typename T>
    struct Clip
    {
        using UpdateFunc = std::function<void(Clip<T>&, uint64_t, double)>;

        uint64_t start_frame{}; ///< 起始绝对帧
        uint64_t end_frame{}; ///< 结束绝对帧（含）
        uint64_t render_order{};
        std::string name; ///< 可选的片段名称

        // ---- 四种矢量图形 ----
        std::vector<Point3D<T>> points; ///< 点云
        std::vector<SegmentStrip3D<T>> segments; ///< 线段带
        std::vector<Triangles3D<T>> triangles;
        std::vector<Path3D<T>> paths; ///< 贝塞尔路径


        UpdateFunc update_func; ///< 若设置，则每次 Update 时按相对帧调用

        Clip() = default;

        Clip(uint64_t start, uint64_t end, UpdateFunc func = nullptr)
            : start_frame(start), end_frame(end), update_func(std::move(func))
        {
        }

        /// 检查绝对帧是否在片段区间内
        [[nodiscard]] bool ContainsFrame(uint64_t global_frame) const
        {
            return global_frame >= start_frame && global_frame <= end_frame;
        }

        void Update(uint64_t global_frame, uint32_t fps)
        {
            if (update_func && ContainsFrame(global_frame))
            {
                uint64_t rel_f = global_frame - start_frame;

                // 计算相对毫秒: (帧数 * 1000) / fps
                // 使用 double 避免整数除法的精度损失
                double rel_ms = (static_cast<double>(rel_f) * 1000.0) / static_cast<double>(fps);

                update_func(*this, rel_f, rel_ms);
            }
        }
    };

    enum class ProjectionMode : uint32_t
    {
        Orthographic = 0,
        Perspective = 1
    };


    // stucanvas/canvas/canvas.hpp 内部

    template <typename T>
    struct CameraConfig
    {
        // 绝对世界空间位置
        T posX = 0, posY = 0, posZ = 10;

        // 绝对世界空间观察点 (LookAt Point)
        T lookX = 0, lookY = 0, lookZ = 0;

        // 方向向量 (归一化后方向不变，故用 float 即可)
        float upX = 0.0f, upY = 1.0f, upZ = 0.0f;

        ProjectionMode mode = ProjectionMode::Perspective;

        float fov = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        float orthoWidth = 10.0f;
        float orthoHeight = 0.0f;

        /**
         * @brief 根据点云的归一化参数，变换相机到 [-1, 1] 的 NDC 空间
         */
        [[nodiscard]] CameraConfig<float> GetNormalizedConfig(T centerX, T centerY, T centerZ, T scale) const
        {
            CameraConfig<float> normalized;

            // 1. 位置变换: (WorldPos - Center) / Scale
            normalized.posX = static_cast<float>((posX - centerX) / scale);
            normalized.posY = static_cast<float>((posY - centerY) / scale);
            normalized.posZ = static_cast<float>((posZ - centerZ) / scale); // 注意：此处应为 (posZ - centerZ) / scale

            // 2. 观察点变换
            normalized.lookX = static_cast<float>((lookX - centerX) / scale);
            normalized.lookY = static_cast<float>((lookY - centerY) / scale);
            normalized.lookZ = static_cast<float>((lookZ - centerZ) / scale);

            // 3. 方向向量保持不变
            normalized.upX = upX;
            normalized.upY = upY;
            normalized.upZ = upZ;

            // 4. 投影参数缩放: 距离和宽度都要除以 Scale
            float s = static_cast<float>(scale);
            normalized.mode = mode;
            normalized.fov = fov;
            normalized.nearPlane = nearPlane / s;
            normalized.farPlane = farPlane / s;
            normalized.orthoWidth = orthoWidth / s;
            normalized.orthoHeight = orthoHeight / s;

            return normalized;
        }


        /**
         * @brief 设置相机在世界空间的位置
         */
        void SetPosition(T x, T y, T z)
        {
            posX = x;
            posY = y;
            posZ = z;
        }

        /**
         * @brief 设置相机看向的目标点
         */
        void SetLookAt(T x, T y, T z)
        {
            lookX = x;
            lookY = y;
            lookZ = z;
        }


        void SetRotation(float yaw_deg, float pitch_deg, float roll_deg = 0.0f)
        {
            // 转为弧度
            float yaw = yaw_deg * (M_PI / 180.0f);
            float pitch = pitch_deg * (M_PI / 180.0f);
            float roll = roll_deg * (M_PI / 180.0f);

            // 1. 计算前向向量 (Forward Vector)
            // 在右手系/Vulkan中，默认前向是 -Z
            float dx = std::cos(pitch) * std::sin(yaw);
            float dy = std::sin(pitch);
            float dz = -std::cos(pitch) * std::cos(yaw);

            // 更新 LookAt 点：基于当前位置 + 方向向量
            lookX = posX + static_cast<T>(dx);
            lookY = posY + static_cast<T>(dy);
            lookZ = posZ + static_cast<T>(dz);

            // 2. 计算上向量 (Up Vector) 考虑 Roll
            // 如果没有 Roll，Up 通常是 (0, 1, 0)
            // 如果有 Roll，我们需要在垂直于前向向量的平面内旋转 Up 向量
            if (std::abs(roll_deg) < 0.0001f)
            {
                upX = 0.0f;
                upY = 1.0f;
                upZ = 0.0f;
            }
            else
            {
                // 计算右向量 (Right = Forward x WorldUp)
                Eigen::Vector3f f(dx, dy, dz);
                f.normalize();
                Eigen::Vector3f worldUp(0, 1, 0);
                Eigen::Vector3f r = f.cross(worldUp).normalized();
                Eigen::Vector3f u = r.cross(f).normalized(); // 修正后的基准上向量

                // 应用 Roll：Up_final = u*cos(roll) - r*sin(roll)
                Eigen::Vector3f finalUp = u * std::cos(roll) - r * std::sin(roll);
                upX = finalUp.x();
                upY = finalUp.y();
                upZ = finalUp.z();
            }
        }

        [[nodiscard]] Eigen::Vector3f GetEulerAngles() const
        {
            float dx = static_cast<float>(lookX - posX);
            float dy = static_cast<float>(lookY - posY);
            float dz = static_cast<float>(lookZ - posZ);
            float pitch = std::asin(dy / std::sqrt(dx * dx + dy * dy + dz * dz));
            float yaw = std::atan2(dx, -dz);
            return Eigen::Vector3f(yaw * (180.0f / M_PI), pitch * (180.0f / M_PI), 0.0f);
        }
    };


    template <typename T>
    inline Eigen::Matrix4f ComputeViewMatrix(const CameraConfig<T>& cam)
    {
        using namespace Eigen;
        Vector3f eye((float)cam.posX, (float)cam.posY, (float)cam.posZ);
        Vector3f center((float)cam.lookX, (float)cam.lookY, (float)cam.lookZ);
        Vector3f up(cam.upX, cam.upY, cam.upZ);

        Vector3f f = (center - eye).normalized(); // 前向 (向里)
        Vector3f s = f.cross(up).normalized(); // 右向
        Vector3f u = s.cross(f); // 上向

        Matrix4f view = Matrix4f::Identity();
        view(0, 0) = s.x();
        view(0, 1) = s.y();
        view(0, 2) = s.z();
        view(0, 3) = -s.dot(eye);
        view(1, 0) = u.x();
        view(1, 1) = u.y();
        view(1, 2) = u.z();
        view(1, 3) = -u.dot(eye);
        // 右手系下，相机前方是 -Z 轴，所以这里使用 -f
        view(2, 0) = -f.x();
        view(2, 1) = -f.y();
        view(2, 2) = -f.z();
        view(2, 3) = f.dot(eye);
        view(3, 0) = 0.0f;
        view(3, 1) = 0.0f;
        view(3, 2) = 0.0f;
        view(3, 3) = 1.0f;
        return view;
    }

    template <typename T>
    inline Eigen::Matrix4f ComputeProjectionMatrix(const CameraConfig<T>& cam, float aspect)
    {
        using namespace Eigen;
        Matrix4f proj = Matrix4f::Zero();
        float n = (float)cam.nearPlane;
        float f = (float)cam.farPlane;

        if (cam.mode == ProjectionMode::Perspective)
        {
            float fovRad = cam.fov * (float)M_PI / 180.0f;
            float focal = 1.0f / std::tan(fovRad * 0.5f);
            proj(0, 0) = focal / aspect;
            proj(1, 1) = -focal; // 翻转Y以适应 Vulkan
            proj(2, 2) = f / (n - f); // 将 -Z 映射到 [0, 1]
            proj(2, 3) = (f * n) / (n - f);
            proj(3, 2) = -1.0f; // W = -Z_view (因为可见物体 Z_view 是负的)
        }
        else
        {
            float w = (float)cam.orthoWidth * 0.5f;
            float h = (cam.orthoHeight != 0.0f) ? (cam.orthoHeight * 0.5f) : (w / aspect);
            proj(0, 0) = 1.0f / w;
            proj(1, 1) = -1.0f / h;
            proj(2, 2) = 1.0f / (n - f);
            proj(2, 3) = n / (n - f);
            proj(3, 3) = 1.0f;
        }
        return proj;
    }


    template <typename T>
    class NLECanvas
    {
    public:
        using CanvasClip = Clip<T>;
        CanvasSettings& GetSettings() { return settings_; }

        void SetFPS(uint32_t FPS)
        {
            FPS_ = FPS;
        }


        CameraConfig<T>& GetCameraConfig() { return camera_; }
        [[nodiscard]] const CameraConfig<T>& GetCameraConfig() const { return camera_; }


        CanvasClip* AddClip(CanvasClip clip)
        {
            clips_.push_back(std::move(clip));
            size_t new_idx = clips_.size() - 1;

            // 更新排序索引：插入后保持按 start_frame 升序
            auto it = std::lower_bound(sorted_indices_.begin(), sorted_indices_.end(), new_idx,
                                       [this](size_t a, size_t b)
                                       {
                                           return clips_[a].start_frame < clips_[b].start_frame;
                                       });
            sorted_indices_.insert(it, new_idx);

            // 返回 vector 中最后一个元素的地址
            return &clips_.back();
        }

        /**
         * @brief 更加简洁的创建片段方式 (类似 emplace)
         */
        template <typename... Args>
        CanvasClip* CreateClip(Args&&... args)
        {
            // 1. 利用 BlockDeque 的 emplace_back 构造对象
            // 由于 BlockDeque 保证了内存地址稳定，这个返回的引用转指针是安全的
            CanvasClip& ref = clips_.emplace_back(std::forward<Args>(args)...);
            CanvasClip* new_clip_ptr = &ref;

            // 2. 维护排序索引 (保持按 start_frame 升序)
            size_t new_idx = clips_.size() - 1;
            auto it = std::lower_bound(sorted_indices_.begin(), sorted_indices_.end(), new_idx,
                                       [this](size_t a, size_t b)
                                       {
                                           return clips_[a].start_frame < clips_[b].start_frame;
                                       });
            sorted_indices_.insert(it, new_idx);

            return new_clip_ptr;
        }

        // 非 const 版本（用于需要修改 Clip 的场合）
        std::vector<CanvasClip*> GetClipsAtFrame(uint64_t global_frame)
        {
            std::vector<CanvasClip*> result;
            if (clips_.empty()) return result;
            auto it = std::upper_bound(sorted_indices_.begin(), sorted_indices_.end(), global_frame,
                                       [&](uint64_t val, size_t idx)
                                       {
                                           return val < clips_[idx].start_frame;
                                       });
            while (it != sorted_indices_.begin())
            {
                --it;
                size_t idx = *it;
                if (clips_[idx].ContainsFrame(global_frame))
                    result.push_back(&clips_[idx]); // 正确：非 const 指针
                else
                    break;
            }
            return result;
        }

        // const 版本（只读）
        std::vector<const CanvasClip*> GetClipsAtFrame(uint64_t global_frame) const
        {
            std::vector<const CanvasClip*> result; // 注意：const 指针
            if (clips_.empty()) return result;
            auto it = std::upper_bound(sorted_indices_.begin(), sorted_indices_.end(), global_frame,
                                       [&](uint64_t val, size_t idx)
                                       {
                                           return val < clips_[idx].start_frame;
                                       });
            while (it != sorted_indices_.begin())
            {
                --it;
                size_t idx = *it;
                if (clips_[idx].ContainsFrame(global_frame))
                    result.push_back(&clips_[idx]); // 正确：const 指针 → const 容器
                else
                    break;
            }
            return result;
        }

        /// 直接访问所有片段（只读）
        const std::vector<CanvasClip>& GetClips() const { return clips_; }
        void render(uint64_t start_frame = 0);
        void exportVideo(std::optional<uint64_t> start_frame = std::nullopt, std::optional<uint64_t> end_frame = std::nullopt);

    private:
        utils::BlockDeque<CanvasClip, 256> clips_;
        std::vector<size_t> sorted_indices_; ///< 按 start_frame 升序的索引
        CameraConfig<T> camera_;
        T curCenterX_{0}, curCenterY_{0}, curCenterZ_{0}, curScale_{1};
        CanvasSettings settings_; // 全局配置成员
        uint32_t FPS_ = 60;

        Vulkan::CanvasShaderGroup LoadAllShaders(VkDevice device)
        {
            using namespace Vulkan;

            // 定义基础路径（方便后续修改）
            const std::string shaderRoot = "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/";

            CanvasShaderGroup group;

            // 1. 点云着色器
            group.vertPoints = ShaderModule::fromSlangFile(
                device, shaderRoot + "points.slang", "vertex", "vertexMain");
            group.fragPoints = ShaderModule::fromSlangFile(
                device, shaderRoot + "points.slang", "fragment", "fragmentMain");

            // 2. 线段着色器
            group.vertSegments = ShaderModule::fromSlangFile(
                device, shaderRoot + "segments.slang", "vertex", "vertexMain");
            group.fragSegments = ShaderModule::fromSlangFile(
                device, shaderRoot + "segments.slang", "fragment", "fragmentMain");

            // 3. 贝塞尔路径着色器
            group.vertPaths = ShaderModule::fromSlangFile(
                device, shaderRoot + "paths.slang", "vertex", "vertexMain");
            group.fragPaths = ShaderModule::fromSlangFile(
                device, shaderRoot + "paths.slang", "fragment", "fragmentMain");

            // 4. 三角面着色器
            group.vertTriangles = ShaderModule::fromSlangFile(
                device, shaderRoot + "triangles.slang", "vertex", "vertexMain");
            group.fragTriangles = ShaderModule::fromSlangFile(
                device, shaderRoot + "triangles.slang", "fragment", "fragmentMain");

            return group; // 利用移动语义返回
        }

        VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device)
        {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = 0;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layoutBinding.descriptorCount = 1;
            // 允许顶点和像素着色器同时访问
            layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &layoutBinding;

            VkDescriptorSetLayout descSetLayout;
            if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descSetLayout) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create descriptor set layout");
            }
            return descSetLayout;
        }

        Vulkan::CanvasPipelineGroup CreatePipelines(
            VkDevice device,
            VkRenderPass renderPass,
            VkDescriptorSetLayout descSetLayout,
            const Vulkan::CanvasShaderGroup& shaders)
        {
            using namespace Vulkan;
            CanvasPipelineGroup group;

            // --- 1. 通用基础配置 ---
            PipelineConfig config;
            config.vertEntry = "main";
            config.fragEntry = "main";
            config.descriptorSetLayouts.push_back(descSetLayout);

            // 混合模式 (支持抗锯齿和透明度)
            config.blendEnable = VK_TRUE;
            config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            config.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            config.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

            // 深度测试 (使用之前优化的 LESS_OR_EQUAL)
            config.depthTestEnable = VK_TRUE;
            config.depthWriteEnable = VK_TRUE;
            config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            // 多重采样与硬件级抗锯齿 (从全局 settings 读取)
            config.rasterizationSamples = settings_.msaaSamples;
            config.sampleShadingEnable = VK_TRUE;
            config.minSampleShading = 0.2f;

            // 推送常量 (Push Constants) - 匹配 96 字节
            VkPushConstantRange pcRange{};
            pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pcRange.offset = 0;
            pcRange.size = sizeof(float) * 24;
            config.pushConstantRanges.push_back(pcRange);

            // --- 2. 创建点云管线 ---
            config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            config.cullMode = VK_CULL_MODE_NONE;
            config.vertShaderModule = shaders.vertPoints.getModule();
            config.fragShaderModule = shaders.fragPoints.getModule();
            group.points = std::make_unique<Pipeline>(device, renderPass, config);

            // --- 3. 创建线段管线 ---
            config.vertShaderModule = shaders.vertSegments.getModule();
            config.fragShaderModule = shaders.fragSegments.getModule();
            group.segments = std::make_unique<Pipeline>(device, renderPass, config);

            // --- 4. 创建路径(贝塞尔)管线 ---
            config.vertShaderModule = shaders.vertPaths.getModule();
            config.fragShaderModule = shaders.fragPaths.getModule();
            group.paths = std::make_unique<Pipeline>(device, renderPass, config);

            // --- 5. 创建三角面管线 ---
            config.vertShaderModule = shaders.vertTriangles.getModule();
            config.fragShaderModule = shaders.fragTriangles.getModule();
            config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            config.cullMode = VK_CULL_MODE_NONE; // 支持双面光照
            group.triangles = std::make_unique<Pipeline>(device, renderPass, config);

            return group;
        }

        Vulkan::CanvasDescriptorGroup CreateDescriptors(
            VkDevice device,
            VkDescriptorSetLayout layout)
        {
            using namespace Vulkan;
            CanvasDescriptorGroup group;

            // 1. 创建描述符池
            VkDescriptorPoolSize poolSize{};
            poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            poolSize.descriptorCount = 4; // 对应 4 种几何类型

            VkDescriptorPoolCreateInfo descPoolInfo{};
            descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descPoolInfo.poolSizeCount = 1;
            descPoolInfo.pPoolSizes = &poolSize;
            descPoolInfo.maxSets = 4;

            if (vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &group.pool) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create descriptor pool");
            }

            // 2. 准备分配信息
            // 我们需要分配 4 个集，每个集都使用相同的布局（SSBO Binding 0）
            std::vector<VkDescriptorSetLayout> layouts(4, layout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = group.pool;
            allocInfo.descriptorSetCount = 4;
            allocInfo.pSetLayouts = layouts.data();

            VkDescriptorSet descSets[4];
            if (vkAllocateDescriptorSets(device, &allocInfo, descSets) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to allocate descriptor sets");
            }

            // 3. 映射到结构体成员
            group.points = descSets[0];
            group.segments = descSets[1];
            group.paths = descSets[2];
            group.triangles = descSets[3];

            return group;
        }


        VkCommandPool CreateCommandPool(VkDevice device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
        {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = flags;

            VkCommandPool pool;
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create command pool");
            }
            return pool;
        }


        ProcessedFrameData<double> PrepareFrameData(uint64_t current_frame)
        {
            ProcessedFrameData<double> data;

            // 1. 获取当前帧的所有片段并排序
            auto clips = GetClipsAtFrame(current_frame);
            std::sort(clips.begin(), clips.end(), [](const CanvasClip* a, const CanvasClip* b)
            {
                return a->render_order < b->render_order;
            });

            // 2. 执行动画逻辑 (Update 会清空并重新生成 clip 内部的几何数据)
            for (auto* clip : clips)
            {
                clip->points.clear();
                clip->paths.clear();
                clip->triangles.clear();
                clip->segments.clear();
                clip->Update(current_frame, FPS_);
            }

            // 3. 计算包围盒 (用于归一化)
            T minX = std::numeric_limits<T>::max(), minY = minX, minZ = minX;
            T maxX = std::numeric_limits<T>::lowest(), maxY = maxX, maxZ = maxX;

            for (const auto* clip : clips)
            {
                for (const auto& p : clip->points)
                {
                    minX = std::min(minX, p.x);
                    maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y);
                    maxY = std::max(maxY, p.y);
                    minZ = std::min(minZ, p.z);
                    maxZ = std::max(maxZ, p.z);
                }
                for (const auto& strip : clip->segments)
                {
                    for (const auto& p : strip.vertices)
                    {
                        minX = std::min(minX, p.x);
                        maxX = std::max(maxX, p.x);
                        minY = std::min(minY, p.y);
                        maxY = std::max(maxY, p.y);
                        minZ = std::min(minZ, p.z);
                        maxZ = std::max(maxZ, p.z);
                    }
                }
                for (const auto& path : clip->paths)
                {
                    for (const auto& cp : path.control_points)
                    {
                        minX = std::min(minX, cp.x);
                        maxX = std::max(maxX, cp.x);
                        minY = std::min(minY, cp.y);
                        maxY = std::max(maxY, cp.y);
                        minZ = std::min(minZ, cp.z);
                        maxZ = std::max(maxZ, cp.z);
                    }
                }
                for (const auto& tri : clip->triangles)
                {
                    for (const auto& p : tri.points)
                    {
                        minX = std::min(minX, p.x);
                        maxX = std::max(maxX, p.x);
                        minY = std::min(minY, p.y);
                        maxY = std::max(maxY, p.y);
                        minZ = std::min(minZ, p.z);
                        maxZ = std::max(maxZ, p.z);
                    }
                }
            }

            // 4. 执行坐标归一化并填充 GPU 容器

            data.centerX = (minX + maxX) * (T)0.5;
            data.centerY = (minY + maxY) * (T)0.5;
            data.centerZ = (minZ + maxZ) * (T)0.5;
            data.scale = std::max({maxX - data.centerX, maxY - data.centerY, maxZ - data.centerZ});
            if (data.scale <= (T)0) data.scale = (T)1;

            // 更新缓存的归一化参数供相机使用
            curCenterX_ = data.centerX;
            curCenterY_ = data.centerY;
            curCenterZ_ = data.centerZ;
            curScale_ = data.scale;

            for (const auto* clip : clips)
            {
                // 处理点云
                for (const auto& p : clip->points)
                {
                    PointDataGPU gpu;
                    gpu.x = (float)((p.x - data.centerX) / data.scale);
                    gpu.y = (float)((p.y - data.centerY) / data.scale);
                    gpu.z = (float)((p.z - data.centerZ) / data.scale);
                    gpu.r = p.r;
                    gpu.g = p.g;
                    gpu.b = p.b;
                    gpu.a = p.a;
                    data.allPoints.push_back(gpu);
                }
                // 处理线段
                for (const auto& strip : clip->segments)
                {
                    if (strip.vertices.size() < 2) continue;
                    for (size_t i = 0; i < strip.vertices.size() - 1; ++i)
                    {
                        const auto& p0 = strip.vertices[i];
                        const auto& p1 = strip.vertices[i + 1];
                        SegmentGPU seg;
                        seg.startX = (float)((p0.x - data.centerX) / data.scale);
                        seg.startY = (float)((p0.y - data.centerY) / data.scale);
                        seg.startZ = (float)((p0.z - data.centerZ) / data.scale);
                        seg.endX = (float)((p1.x - data.centerX) / data.scale);
                        seg.endY = (float)((p1.y - data.centerY) / data.scale);
                        seg.endZ = (float)((p1.z - data.centerZ) / data.scale);
                        seg.startR = p0.r;
                        seg.startG = p0.g;
                        seg.startB = p0.b;
                        seg.startA = p0.a;
                        seg.endR = p1.r;
                        seg.endG = p1.g;
                        seg.endB = p1.b;
                        seg.endA = p1.a;
                        data.allSegments.push_back(seg);
                    }
                }
                // 处理路径
                for (const auto& path : clip->paths)
                {
                    for (const auto& cp : path.control_points)
                    {
                        PointDataGPU gpu;
                        gpu.x = (float)((cp.x - data.centerX) / data.scale);
                        gpu.y = (float)((cp.y - data.centerY) / data.scale);
                        gpu.z = (float)((cp.z - data.centerZ) / data.scale);
                        gpu.r = cp.r;
                        gpu.g = cp.g;
                        gpu.b = cp.b;
                        gpu.a = cp.a;
                        data.allPathPoints.push_back(gpu);
                    }
                }
                // 处理三角面
                for (const auto& tri : clip->triangles)
                {
                    uint32_t base = (uint32_t)data.allTriVertices.size();
                    for (const auto& p : tri.points)
                    {
                        PointDataGPU gpu;
                        gpu.x = (float)((p.x - data.centerX) / data.scale);
                        gpu.y = (float)((p.y - data.centerY) / data.scale);
                        gpu.z = (float)((p.z - data.centerZ) / data.scale);
                        gpu.r = p.r;
                        gpu.g = p.g;
                        gpu.b = p.b;
                        gpu.a = p.a;
                        data.allTriVertices.push_back(gpu);
                    }
                    for (auto idx : tri.indices) data.allTriIndices.push_back(base + idx);
                }
            }

            return data;
        }


        void UploadFrameData(
            VkDevice device,
            VkPhysicalDevice physicalDevice,
            VkCommandPool uploadPool,
            VkQueue graphicsQueue,
            const ProcessedFrameData<T>& frameData,
            Vulkan::CanvasDescriptorGroup& descriptors,
            Vulkan::CanvasBufferGroup& buffers)
        {
            using namespace Vulkan;

            if (frameData.isEmpty()) return;

            // 1. 等待 GPU 空闲，确保之前的缓冲区不再被使用
            vkDeviceWaitIdle(device);

            // --- 内部辅助：更新描述符集的逻辑 ---
            auto UpdateDescriptor = [&](VkDescriptorSet dstSet, VkBuffer buffer, VkDeviceSize range)
            {
                VkDescriptorBufferInfo bufferInfo{buffer, 0, range};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = dstSet;
                write.dstBinding = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = &bufferInfo;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            };

            // --------------------------
            // 1. 上传点云数据
            // --------------------------
            if (!frameData.allPoints.empty())
            {
                size_t size = frameData.allPoints.size() * sizeof(PointDataGPU);
                buffers.points = Buffer::CreateAndUpload(
                    device, physicalDevice, uploadPool, graphicsQueue,
                    frameData.allPoints.data(), size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                UpdateDescriptor(descriptors.points, buffers.points.getBuffer(), size);
            }

            // --------------------------
            // 2. 上传线段数据
            // --------------------------
            if (!frameData.allSegments.empty())
            {
                size_t size = frameData.allSegments.size() * sizeof(SegmentGPU);
                buffers.segments = Buffer::CreateAndUpload(
                    device, physicalDevice, uploadPool, graphicsQueue,
                    frameData.allSegments.data(), size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                UpdateDescriptor(descriptors.segments, buffers.segments.getBuffer(), size);
            }

            // --------------------------
            // 3. 上传路径数据
            // --------------------------
            if (!frameData.allPathPoints.empty())
            {
                size_t size = frameData.allPathPoints.size() * sizeof(PointDataGPU);
                buffers.paths = Buffer::CreateAndUpload(
                    device, physicalDevice, uploadPool, graphicsQueue,
                    frameData.allPathPoints.data(), size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                UpdateDescriptor(descriptors.paths, buffers.paths.getBuffer(), size);
            }

            // --------------------------
            // 4. 上传三角面数据 (顶点 + 索引)
            // --------------------------
            if (!frameData.allTriVertices.empty())
            {
                // 顶点 (SSBO)
                size_t vSize = frameData.allTriVertices.size() * sizeof(PointDataGPU);
                buffers.triangleVertices = Buffer::CreateAndUpload(
                    device, physicalDevice, uploadPool, graphicsQueue,
                    frameData.allTriVertices.data(), vSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                UpdateDescriptor(descriptors.triangles, buffers.triangleVertices.getBuffer(), vSize);

                // 索引 (Index Buffer)
                size_t iSize = frameData.allTriIndices.size() * sizeof(uint32_t);
                buffers.triangleIndices = Buffer::CreateAndUpload(
                    device, physicalDevice, uploadPool, graphicsQueue,
                    frameData.allTriIndices.data(), iSize,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            }
        }


        Vulkan::CanvasPushConstants ComputePushConstants(
            VkExtent2D extent,
            const ProcessedFrameData<T>& frameData)
        {
            using namespace Vulkan;
            CanvasPushConstants pc{};

            // 1. 获取归一化后的相机配置
            // 使用 frameData 中计算出的 centerX, centerY, centerZ, scale
            CameraConfig<float> transformedCam = camera_.GetNormalizedConfig(
                frameData.centerX, frameData.centerY, frameData.centerZ, frameData.scale);

            float viewW = static_cast<float>(extent.width);
            float viewH = static_cast<float>(extent.height);
            float aspect = viewW / viewH;

            // 2. 计算矩阵 (Eigen 默认是 Column-Major)
            Eigen::Matrix4f viewMat = ComputeViewMatrix(transformedCam);
            Eigen::Matrix4f projMat = ComputeProjectionMatrix(transformedCam, aspect);
            Eigen::Matrix4f mvpMat = projMat * viewMat;

            // 3. 填充结构体
            pc.camPos[0] = transformedCam.posX;
            pc.camPos[1] = transformedCam.posY;
            pc.camPos[2] = transformedCam.posZ;
            pc._pad0 = 0.0f;

            // 4. 将 Eigen (Col-Major) 转换为 Row-Major 拷贝到数组
            // 这是为了适配 Slang/HLSL 中 mul(Matrix, Vector) 的默认约定
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    pc.mvp[row * 4 + col] = mvpMat(row, col);
                }
            }

            pc.viewportSize[0] = viewW;
            pc.viewportSize[1] = viewH;
            pc.pointRadius = 5.0f; // 默认值，可在绘制不同物体前微调
            pc._pad1 = 0.0f;

            return pc;
        }


        void CmdBeginRenderPass(
            VkCommandBuffer cmd,
            VkRenderPass renderPass,
            VkFramebuffer framebuffer,
            VkExtent2D extent,
            VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}})
        {
            // 1. 配置清除值 (颜色 + 深度)
            std::array<VkClearValue, 2> clearValues{};
            clearValues[0].color = clearColor;
            clearValues[1].depthStencil = {1.0f, 0}; // 深度清空为最远

            // 2. 配置开始信息
            VkRenderPassBeginInfo rpBegin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            rpBegin.renderPass = renderPass;
            rpBegin.framebuffer = framebuffer;
            rpBegin.renderArea.offset = {0, 0};
            rpBegin.renderArea.extent = extent;
            rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rpBegin.pClearValues = clearValues.data();

            // 3. 开启渲染通路
            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            // 4. 顺便设置动态视口和剪裁 (几乎所有绘制都需要)
            VkViewport viewport{0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{{0, 0}, extent};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }




    void RecordGraphicsCommands(
        VkCommandBuffer cmd,
        const ProcessedFrameData<T>& frameData,
        const Vulkan::CanvasPipelineGroup& pipelines,
        const Vulkan::CanvasDescriptorGroup& descriptors,
        const Vulkan::CanvasBufferGroup& buffers,
        Vulkan::CanvasPushConstants pc) // 值传递，允许内部修改 radius
    {
        if (frameData.isEmpty()) return;

        // --------------------------
        // 1. 绘制线段 (Segments)
        // --------------------------
        if (!frameData.allSegments.empty())
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.segments->get());

            pc.pointRadius = 3.0f; // 线宽 (lineWidth)
            vkCmdPushConstants(cmd, pipelines.segments->getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelines.segments->getLayout(), 0, 1, &descriptors.segments, 0, nullptr);

            // 线段实例化渲染 (每个线段 6 个顶点组成 Quad)
            vkCmdDraw(cmd, 6, static_cast<uint32_t>(frameData.allSegments.size()), 0, 0);
        }

        // --------------------------
        // 2. 绘制点云 (Points)
        // --------------------------
        if (!frameData.allPoints.empty())
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.points->get());

            pc.pointRadius = 5.0f; // 恢复点半径
            vkCmdPushConstants(cmd, pipelines.points->getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelines.points->getLayout(), 0, 1, &descriptors.points, 0, nullptr);

            // 点云实例化渲染 (每个点 3 个顶点组成包裹圆的三角形)
            vkCmdDraw(cmd, 3, static_cast<uint32_t>(frameData.allPoints.size()), 0, 0);
        }

        // --------------------------
        // 3. 绘制贝塞尔路径 (Paths)
        // --------------------------
        if (frameData.allPathPoints.size() >= 4)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.paths->get());

            pc.pointRadius = 4.0f; // 路径宽度 (strokeWidth)
            vkCmdPushConstants(cmd, pipelines.paths->getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelines.paths->getLayout(), 0, 1, &descriptors.paths, 0, nullptr);

            // 每 4 个点构成一段，计算段数: (N-1)/3
            uint32_t segmentCount = (static_cast<uint32_t>(frameData.allPathPoints.size()) - 1) / 3;
            vkCmdDraw(cmd, 6, segmentCount, 0, 0);
        }

        // --------------------------
        // 4. 绘制三角面网格 (Triangles)
        // --------------------------
        if (!frameData.allTriVertices.empty())
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangles->get());

            vkCmdPushConstants(cmd, pipelines.triangles->getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelines.triangles->getLayout(), 0, 1, &descriptors.triangles, 0, nullptr);

            // 绑定索引缓冲并执行索引绘制
            vkCmdBindIndexBuffer(cmd, buffers.triangleIndices.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(frameData.allTriIndices.size()), 1, 0, 0, 0);
        }
    }

        void CleanupVulkanResources(
            VkDevice device,
            VkDescriptorSetLayout descSetLayout,
            VkCommandPool uploadPool,
            Vulkan::CanvasDescriptorGroup& descriptors,
            Vulkan::CanvasPipelineGroup& pipelines)
        {
            // 1. 核心：首先确保 GPU 已经完全停止工作，不再引用任何资源
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);

                // 2. 销毁管线对象 (Pipeline 依赖于 PipelineLayout 和 DescriptorSetLayout)
                // 由于使用了 unique_ptr，调用 reset() 会显式触发销毁
                pipelines.points.reset();
                pipelines.segments.reset();
                pipelines.paths.reset();
                pipelines.triangles.reset();

                // 3. 销毁描述符池 (这会自动释放从该池分配的所有 Descriptor Sets)
                if (descriptors.pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device, descriptors.pool, nullptr);
                    descriptors.pool = VK_NULL_HANDLE;
                }

                // 4. 销毁上传专用的指令池
                if (uploadPool != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(device, uploadPool, nullptr);
                }

                // 5. 最后销毁布局 (Layout) 资源
                // 它是基础定义，必须在所有依赖它的实例（Pipeline, Pool）消失后销毁
                if (descSetLayout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
                }
            }
        }



        [[nodiscard]] std::pair<uint64_t, uint64_t> GetGlobalFrameRange() const {
            if (sorted_indices_.empty()) return {0, 0};
            uint64_t min_s = std::numeric_limits<uint64_t>::max();
            uint64_t max_e = 0;
            for (const auto& clip : clips_) {
                min_s = std::min(min_s, clip.start_frame);
                max_e = std::max(max_e, clip.end_frame);
            }
            return {min_s, max_e};
        }
    };

template <typename T>
void NLECanvas<T>::render(uint64_t start_frame)
{
    using namespace Vulkan;

    // 1. 初始化 SDL 环境
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error("SDL: Failed to initialize SDL video subsystem: " + std::string(SDL_GetError()));
    }

    SDL_Window* window = SDL_CreateWindow("StuCanvas Render", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Quit();
        throw std::runtime_error("SDL: Failed to create Window: " + std::string(SDL_GetError()));
    }

    // ========================================================================
    // 【核心修复】：使用嵌套作用域隔离所有 Vulkan 局部变量的生命周期
    // ========================================================================
    {
        VulkanContext vkCtx;

        VulkanContextConfig config{};
        config.appName = "StuCanvas Render";
        config.enableValidation = true;

        // 动态获取当前平台下 SDL3 正常呈现所需的 Instance 扩展列表
        uint32_t sdlExtCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        if (sdlExts) {
            for (uint32_t i = 0; i < sdlExtCount; ++i) {
                config.requiredInstanceExtensions.push_back(sdlExts[i]);
            }
        }

        // 显式添加设备属性及校验扩展
        config.requiredInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        // 配置呈现显示必需的逻辑设备交换链扩展
        config.requiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        // 初始化 Vulkan 实例与表面（Surface）
        vkCtx.initInstance(config);
        vkCtx.initSurface(window);

        // 自定义刷选物理显卡并创建逻辑设备 (支持核显与独显多卡环境)
        vkCtx.filterPhysicalDevices([&](VkPhysicalDevice dev) -> bool {
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> available(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

            bool supportsSwapchain = false;
            for (const auto& ext : available) {
                if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                    supportsSwapchain = true;
                    break;
                }
            }
            return supportsSwapchain;
        });

        vkCtx.createLogicalDevice(0, config.requiredDeviceExtensions);

        const auto& primaryDev = vkCtx.getPrimaryDevice();
        VkDevice device = primaryDev.get();

        // 构建临时交换链以查询最兼容的颜色附件格式
        VkFormat swapchainFormat;
        {
            SwapChain tempSC(primaryDev.getPhysicalDevice(), device,
                             vkCtx.getSurface(), VK_NULL_HANDLE,
                             primaryDev.getGraphicsQueue().familyIndex,
                             primaryDev.getPresentQueue().familyIndex,
                             window, settings_.msaaSamples);
            swapchainFormat = tempSC.getImageFormat();
        }

        // 初始化着色器、管线以及描述符集
        RenderPass renderPass(device, swapchainFormat, settings_.msaaSamples);
        auto shaders = LoadAllShaders(device);

        auto descSetLayout = CreateDescriptorSetLayout(device);
        auto pipelines = CreatePipelines(device, renderPass.get(), descSetLayout, shaders);
        auto descriptors = CreateDescriptors(device, descSetLayout);

        // 初始化呈现同步控制器
        Presenter presenter(
            primaryDev.getPhysicalDevice(), device,
            vkCtx.getSurface(),
            primaryDev.getGraphicsQueue().familyIndex,
            primaryDev.getPresentQueue().familyIndex,
            primaryDev.getGraphicsQueue().handle,
            primaryDev.getPresentQueue().handle,
            renderPass.get(), window, settings_.msaaSamples);

        uint64_t current_frame = start_frame;
        bool is_paused = false;
        uint64_t last_time = SDL_GetTicks();
        bool frame_dirty = true;

        CameraConfig<T>& cam = camera_;
        bool running = true;
        SDL_Event event;

        ProcessedFrameData<double> frameData;
        Vulkan::CanvasBufferGroup buffers;
        VkCommandPool uploadPool = CreateCommandPool(device, primaryDev.getGraphicsQueue().familyIndex,
                                                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

        // 主渲染展示循环
        while (running)
        {
            // --- A. 事件处理 ---
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                    presenter.markResized();
                }
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    auto key = event.key.key;
                    if (key == SDLK_ESCAPE) running = false;
                    else if (key == SDLK_SPACE) {
                        is_paused = !is_paused;
                        last_time = SDL_GetTicks();
                        frame_dirty = true;
                    }
                    else if (key == SDLK_LEFT) { if (current_frame > 0) current_frame--; frame_dirty = true; }
                    else if (key == SDLK_RIGHT) { current_frame++; frame_dirty = true; }
                    else if (key == SDLK_R) { current_frame = 0; frame_dirty = true; }
                }
            }

            // --- B. 自动播放帧时间计算 ---
            uint64_t current_time = SDL_GetTicks();
            if (!is_paused && FPS_ > 0)
            {
                uint64_t frame_duration_ms = 1000ULL / FPS_;
                if (current_time - last_time >= frame_duration_ms)
                {
                    current_frame++;
                    frame_dirty = true;
                    last_time = current_time;
                }
            }

            // --- C. 数据准备逻辑：仅在帧改变时执行 ---
            if (frame_dirty)
            {
                std::string title = "StuCanvas Render - Frame: " + std::to_string(current_frame) +
                    (is_paused ? " (Paused)" : " (Playing)");
                SDL_SetWindowTitle(window, title.c_str());

                frameData = PrepareFrameData(current_frame);

                UploadFrameData(device, primaryDev.getPhysicalDevice(), uploadPool,
                                primaryDev.getGraphicsQueue().handle, frameData, descriptors, buffers);

                frame_dirty = false;
            }

            // --- D. 画面呈现逻辑：每一帧安全循环，保障窗口缩放与重绘 ---
            uint32_t imageIndex;
            VkCommandBuffer cmd = presenter.beginFrame(imageIndex);

            if (cmd == VK_NULL_HANDLE) continue;

            auto extent = presenter.getExtent();
            auto pc = ComputePushConstants(extent, frameData);

            CmdBeginRenderPass(cmd, renderPass.get(), presenter.getFramebuffer(imageIndex), extent);
            RecordGraphicsCommands(cmd, frameData, pipelines, descriptors, buffers, pc);
            vkCmdEndRenderPass(cmd);

            presenter.endFrame(cmd, imageIndex);
        }

        // 确保 GPU 闲置并手动清理普通资源
        vkDeviceWaitIdle(device);
        CleanupVulkanResources(device, descSetLayout, uploadPool, descriptors, pipelines);

    } // <--- 所有 Vulkan RAII 资源（包括 vkCtx, presenter 等）在此处均已安全、完美析构！

    // ========================================================================
    // 此时 Vulkan 的所有操作已彻底结束，可以安全销毁窗口并关闭 SDL 系统
    // ========================================================================
    SDL_DestroyWindow(window);
    SDL_Quit();
}




template <typename T>
void NLECanvas<T>::exportVideo(std::optional<uint64_t> start_frame, std::optional<uint64_t> end_frame) {
    using namespace Vulkan;

    // 1. 确定导出区间与分辨率
    auto [auto_start, auto_end] = GetGlobalFrameRange();
    uint64_t start_f = start_frame.value_or(auto_start);
    uint64_t end_f = end_frame.value_or(auto_end);
    if (end_f < start_f) return;
    uint32_t total_frames = static_cast<uint32_t>(end_f - start_f + 1);

    const auto& exportCfg = settings_.exportConfigs;
    uint32_t exWidth = exportCfg.width;
    uint32_t exHeight = exportCfg.height;

    // NV12 线性大小计算: Y (W*H) + UV (W*H/2) = 1.5 * W * H
    VkDeviceSize yuvSize = static_cast<VkDeviceSize>(exWidth * exHeight * 1.5);

    // 2. 将项目配置转换为 NVENC 高级配置结构体
    NvencEncoderConfig nvConfig;
    if (exportCfg.codec == VideoCodec::AV1) {
        nvConfig.codec = NvencCodec::AV1;
    } else if (exportCfg.codec == VideoCodec::HEVC) {
        nvConfig.codec = NvencCodec::H265;
    } else {
        nvConfig.codec = NvencCodec::H264;
    }
    nvConfig.width = exWidth;
    nvConfig.height = exHeight;
    nvConfig.fps = FPS_;
    nvConfig.bitrate_bps = exportCfg.bitRateKbps * 1000;
    nvConfig.max_bitrate_bps = exportCfg.maxBitRateKbps * 1000;
    nvConfig.gop_size = exportCfg.gopSize;
    nvConfig.quality_preset = exportCfg.tuningPreset;

    // 3. 声明并建立无头模式（Headless）下的 Vulkan 运行上下文 (RAII)
    VulkanContext vkCtx;

    VulkanContextConfig config{};
    config.appName = "StuCanvas Headless Exporter";
    config.enableValidation = true;

    // 实例级扩展：共享物理属性与外部同步能力查询
    config.requiredInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    config.requiredInstanceExtensions.push_back("VK_KHR_external_memory_capabilities");
    config.requiredInstanceExtensions.push_back("VK_KHR_external_semaphore_capabilities");

    // 设备级扩展：跨平台共享内存与信号量同步设备扩展
    config.requiredDeviceExtensions.push_back("VK_KHR_external_memory");
    config.requiredDeviceExtensions.push_back("VK_KHR_external_semaphore");
#ifdef _WIN32
    config.requiredDeviceExtensions.push_back("VK_KHR_external_memory_win32");
    config.requiredDeviceExtensions.push_back("VK_KHR_external_semaphore_win32");
#else
    config.requiredDeviceExtensions.push_back("VK_KHR_external_memory_fd");
    config.requiredDeviceExtensions.push_back("VK_KHR_external_semaphore_fd");
#endif

    // 初始化 Vulkan 实例 (自动追加所需的调试扩展等)
    vkCtx.initInstance(config);

    // 4. 自定义过滤并筛选物理设备（必须支持所要求的外部内存/信号量扩展）
    vkCtx.filterPhysicalDevices([&](VkPhysicalDevice dev) -> bool {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

        for (const auto& reqExt : config.requiredDeviceExtensions) {
            bool found = false;
            for (const auto& ext : available) {
                if (std::strcmp(ext.extensionName, reqExt) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    });

    // 5. 自动算力加权：选出性能最强的物理显卡（独显优先，并以 maxImageDimension2D 平局打破）
    uint32_t bestDeviceIndex = 0;
    int bestScore = -1;
    const auto& physDevices = vkCtx.getPhysicalDevices();
    for (uint32_t i = 0; i < physDevices.size(); ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDevices[i], &props);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score = 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score = 500;
        }
        score += static_cast<int>(props.limits.maxImageDimension2D / 100);

        if (score > bestScore) {
            bestScore = score;
            bestDeviceIndex = i;
        }
    }

    // 在选定最强卡上创建逻辑设备（内部默认自动托管并使能 shaderDrawParameters 与 synchronization2 特性）
    vkCtx.createLogicalDevice(bestDeviceIndex, config.requiredDeviceExtensions);

    // 6. 定义好输出路径与临时磁盘暂存文件路径
    const std::string finalPath = exportCfg.outputPath; // 最终导出的目标 MP4 路径
    std::string tempPath;                              // 临时裸流文件路径
    if (exportCfg.codec == VideoCodec::AV1) {
        tempPath = finalPath + ".temp.ivf";
    } else if (exportCfg.codec == VideoCodec::HEVC) {
        tempPath = finalPath + ".temp.h265";
    } else {
        tempPath = finalPath + ".temp.h264";
    }

    // ========================================================================
    // 【生命周期安全围栏】：使用嵌套大括号包含所有依赖该 Device 的具体 Vulkan 资源
    // 确保这些资源在外部大括号结束、vkCtx 开始析构前，全部安全、完美地释放干净
    // ========================================================================
    {
        const auto& primaryDev = vkCtx.getPrimaryDevice();
        VkDevice device = primaryDev.get();
        VkPhysicalDevice physDev = primaryDev.getPhysicalDevice();
        VkQueue graphicsQueue = primaryDev.getGraphicsQueue().handle;
        VkQueue computeQueue  = primaryDev.getComputeQueue().handle;

        // 获取队列提交 2.0 函数指针
        auto pfnQueueSubmit2 = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(device, "vkQueueSubmit2");
        if (!pfnQueueSubmit2) throw std::runtime_error("Vulkan: Failed to load vkQueueSubmit2");

        // 创建离屏渲染环境
        RenderPass offscreenPass(device, VK_FORMAT_R8G8B8A8_UNORM, settings_.msaaSamples, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto shaders = LoadAllShaders(device);
        auto descSetLayout = CreateDescriptorSetLayout(device);
        auto descriptors = CreateDescriptors(device, descSetLayout);
        auto pipelines = CreatePipelines(device, offscreenPass.get(), descSetLayout, shaders);

        OffscreenTarget target(device, physDev, exWidth, exHeight, settings_.msaaSamples, offscreenPass.get());

        // 初始化异步 CUDA 硬件视频编码器
        NvencCudaEncoder encoder(nvConfig);

        // 构建两个具有外部导入（Export）共享显存属性的线性双缓冲 VkBuffer 资源
        struct ExportableBuffer {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
        };
        std::array<ExportableBuffer, 2> exportStagingBuffers;

        for (int i = 0; i < 2; ++i) {
            VkExternalMemoryBufferCreateInfo extBufferInfo{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
#ifdef _WIN32
            extBufferInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
            extBufferInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

            VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.pNext = &extBufferInfo;
            bufferInfo.size = yuvSize;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bufferInfo, nullptr, &exportStagingBuffers[i].buffer) != VK_SUCCESS) {
                throw std::runtime_error("Vulkan: Failed to create exportable buffer.");
            }

            VkMemoryRequirements memReq;
            vkGetBufferMemoryRequirements(device, exportStagingBuffers[i].buffer, &memReq);
            exportStagingBuffers[i].size = memReq.size;

            VkExportMemoryAllocateInfo exportAllocInfo{ VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
#ifdef _WIN32
            exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
            exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

            VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocInfo.pNext = &exportAllocInfo;
            allocInfo.allocationSize = memReq.size;

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
            uint32_t memoryTypeIndex = 0xFFFFFFFF;

            for (uint32_t k = 0; k < memProps.memoryTypeCount; ++k) {
                if ((memReq.memoryTypeBits & (1 << k)) &&
                    (memProps.memoryTypes[k].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    memoryTypeIndex = k;
                    break;
                }
            }
            if (memoryTypeIndex == 0xFFFFFFFF) {
                for (uint32_t k = 0; k < memProps.memoryTypeCount; ++k) {
                    if (memReq.memoryTypeBits & (1 << k)) {
                        memoryTypeIndex = k;
                        break;
                    }
                }
            }
            allocInfo.memoryTypeIndex = memoryTypeIndex;

            if (vkAllocateMemory(device, &allocInfo, nullptr, &exportStagingBuffers[i].memory) != VK_SUCCESS) {
                throw std::runtime_error("Vulkan: Failed to allocate exportable buffer memory.");
            }

            vkBindBufferMemory(device, exportStagingBuffers[i].buffer, exportStagingBuffers[i].memory, 0);
        }

        // Compute 转换环境准备
        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        VkSampler computeSampler;
        vkCreateSampler(device, &samplerInfo, nullptr, &computeSampler);

        std::array<VkDescriptorSetLayoutBinding, 3> compBindings{};
        compBindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        compBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        compBindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

        VkDescriptorSetLayout computeLayout;
        VkDescriptorSetLayoutCreateInfo compLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, compBindings.data() };
        vkCreateDescriptorSetLayout(device, &compLayoutInfo, nullptr, &computeLayout);

        auto compShader = ShaderModule::fromSlangFile(device, "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/rgb_to_nv12.slang", "compute", "computeMain");
        ComputePipeline yuvConverter(device, compShader.getModule(), {computeLayout});

        std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}
        }};
        VkDescriptorPoolCreateInfo cpInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 2, poolSizes.data() };
        VkDescriptorPool compPool;
        vkCreateDescriptorPool(device, &cpInfo, nullptr, &compPool);

        VkDescriptorSet computeSet;
        VkDescriptorSetAllocateInfo cai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, compPool, 1, &computeLayout };
        vkAllocateDescriptorSets(device, &cai, &computeSet);

        VkCommandPool gfxPool = CreateCommandPool(device, primaryDev.getGraphicsQueue().familyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        VkCommandPool cmtPool = CreateCommandPool(device, primaryDev.getComputeQueue().familyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        VkCommandBuffer gfxCmd, cmtCmd;
        auto Alloc = [&](VkCommandPool p, VkCommandBuffer* c) {
            VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, p, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
            vkAllocateCommandBuffers(device, &ai, c);
        };
        Alloc(gfxPool, &gfxCmd); Alloc(cmtPool, &cmtCmd);

        VkExportSemaphoreCreateInfo exportSemInfo{ VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
#ifdef _WIN32
        exportSemInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        exportSemInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkSemaphoreCreateInfo semCi{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semCi.pNext = &exportSemInfo;

        VkSemaphore semRenderDone, semComputeDone;
        if (vkCreateSemaphore(device, &semCi, nullptr, &semRenderDone) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semCi, nullptr, &semComputeDone) != VK_SUCCESS) {
            throw std::runtime_error("Vulkan: Failed to create exportable semaphores.");
        }

        // 导出信号量并导入至 CUDA 中进行流级等待
        CUexternalSemaphore cudaWaitSem = nullptr;
#ifdef _WIN32
        auto fpGetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR");
        if (!fpGetSemaphoreWin32HandleKHR) throw std::runtime_error("Vulkan: Failed to load vkGetSemaphoreWin32HandleKHR");
        VkSemaphoreGetWin32HandleInfoKHR getSemWin32Info{
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
            nullptr,
            semComputeDone,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
        };
        HANDLE semHandle = nullptr;
        fpGetSemaphoreWin32HandleKHR(device, &getSemWin32Info, &semHandle);
        cudaWaitSem = encoder.ImportSemaphore(semHandle, false);
#else
        auto fpGetSemaphoreFdKHR = (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR");
        if (!fpGetSemaphoreFdKHR) throw std::runtime_error("Vulkan: Failed to load vkGetSemaphoreFdKHR");
        VkSemaphoreGetFdInfoKHR getSemFdInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            nullptr,
            semComputeDone,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT
        };
        int semFd = -1;
        fpGetSemaphoreFdKHR(device, &getSemFdInfo, &semFd);
        cudaWaitSem = encoder.ImportSemaphore(semFd, false);
#endif

        // 开启临时流式暂存文件
        std::ofstream tempFile(tempPath, std::ios::binary);
        if (!tempFile.is_open()) {
            throw std::runtime_error("Vulkan/Exporter: Failed to open temporary raw stream file: " + tempPath);
        }

        const bool isAV1 = (exportCfg.codec == VideoCodec::AV1);
        if (isAV1) {
            IVFWriter::WriteHeader(tempFile, exWidth, exHeight, FPS_, total_frames);
        }

        Vulkan::CanvasBufferGroup buffers;

        // 零拷贝主硬件转码循环
        for (uint64_t current_f = start_f; current_f <= end_f; ++current_f) {
            uint32_t frameIdx = (uint32_t)(current_f - start_f);
            uint32_t currPingPong = frameIdx % 2;

            auto frameData = PrepareFrameData(current_f);
            UploadFrameData(device, physDev, gfxPool, graphicsQueue, frameData, descriptors, buffers);

            VkDescriptorImageInfo infoRGB{ computeSampler, target.getResolveRGBView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo infoY  { VK_NULL_HANDLE, target.getYPlaneView(currPingPong), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo infoUV { VK_NULL_HANDLE, target.getUVPlaneView(currPingPong), VK_IMAGE_LAYOUT_GENERAL };
            std::array<VkWriteDescriptorSet, 3> compWrites{};
            compWrites[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, computeSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infoRGB };
            compWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, computeSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &infoY };
            compWrites[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, computeSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &infoUV };
            vkUpdateDescriptorSets(device, 3, compWrites.data(), 0, nullptr);

            // --- Step 1: Graphics (Render & Resolve) ---
            vkResetCommandBuffer(gfxCmd, 0);
            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            vkBeginCommandBuffer(gfxCmd, &bi);
            auto pc = ComputePushConstants({exWidth, exHeight}, frameData);
            CmdBeginRenderPass(gfxCmd, offscreenPass.get(), target.getFramebuffer(), {exWidth, exHeight});
            RecordGraphicsCommands(gfxCmd, frameData, pipelines, descriptors, buffers, pc);
            vkCmdEndRenderPass(gfxCmd);

            VkImageMemoryBarrier2 b1{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, target.getResolveRGBImage(), {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            VkDependencyInfo d1{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &b1 };
            vkCmdPipelineBarrier2(gfxCmd, &d1);
            vkEndCommandBuffer(gfxCmd);

            // --- Step 2: Compute (RGBA to NV12) ---
            vkResetCommandBuffer(cmtCmd, 0);
            vkBeginCommandBuffer(cmtCmd, &bi);

            VkImageMemoryBarrier2 b2{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
                VK_PIPELINE_STAGE_2_NONE, 0,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, target.getYuvImage(currPingPong),
                {VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1} };
            VkDependencyInfo d2{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &b2 };
            vkCmdPipelineBarrier2(cmtCmd, &d2);

            vkCmdBindPipeline(cmtCmd, VK_PIPELINE_BIND_POINT_COMPUTE, yuvConverter.get());
            vkCmdBindDescriptorSets(cmtCmd, VK_PIPELINE_BIND_POINT_COMPUTE, yuvConverter.getLayout(), 0, 1, &computeSet, 0, nullptr);
            vkCmdDispatch(cmtCmd, (exWidth + 15) / 16, (exHeight + 15) / 16, 1);

            // Barrier: 转换后的 NV12 图像 -> 传输源格式
            VkImageMemoryBarrier2 b3{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, target.getYuvImage(currPingPong),
                {VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1} };
            VkDependencyInfo d3{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &b3 };
            vkCmdPipelineBarrier2(cmtCmd, &d3);

            // --- Step 3: GPU Copy (Tiled Image to Linear VkBuffer) ---
            std::array<VkBufferImageCopy, 2> copyRegions{};
            copyRegions[0].bufferOffset = 0;
            copyRegions[0].imageSubresource = { VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1 };
            copyRegions[0].imageExtent = { exWidth, exHeight, 1 };
            copyRegions[1].bufferOffset = exWidth * exHeight;
            copyRegions[1].imageSubresource = { VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1 };
            copyRegions[1].imageExtent = { exWidth / 2, exHeight / 2, 1 };

            vkCmdCopyImageToBuffer(cmtCmd, target.getYuvImage(currPingPong), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   exportStagingBuffers[currPingPong].buffer, 2, copyRegions.data());

            vkEndCommandBuffer(cmtCmd);

            // --- Step 4: Vulkan 队列提交 ---
            VkSemaphoreSubmitInfo sigGfx{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, semRenderDone, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
            VkCommandBufferSubmitInfo subGfx{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, gfxCmd, 0 };
            VkSubmitInfo2 s1{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &subGfx, 1, &sigGfx };
            pfnQueueSubmit2(graphicsQueue, 1, &s1, VK_NULL_HANDLE);

            VkSemaphoreSubmitInfo waitGfx{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, semRenderDone, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT };
            VkSemaphoreSubmitInfo sigCmt{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, semComputeDone, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT };
            VkCommandBufferSubmitInfo subCmt{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmtCmd, 0 };
            VkSubmitInfo2 s2{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 1, &waitGfx, 1, &subCmt, 1, &sigCmt };
            pfnQueueSubmit2(computeQueue, 1, &s2, VK_NULL_HANDLE);

            // --- Step 5: CUDA & NVENC 异步流编码同步 ---
            encoder.WaitSemaphore(cudaWaitSem, 0);

            std::vector<uint8_t> out_bitstream;
            encoder.EncodeVulkanFrame(
                device,
                VK_NULL_HANDLE,
                exportStagingBuffers[currPingPong].memory,
                exportStagingBuffers[currPingPong].size,
                exWidth,
                frameIdx,
                out_bitstream,
                false
            );

            // 写入临时暂存文件
            if (!out_bitstream.empty()) {
                if (isAV1) {
                    IVFWriter::WriteFrame(tempFile, out_bitstream.data(), out_bitstream.size(), frameIdx);
                } else {
                    tempFile.write(reinterpret_cast<const char*>(out_bitstream.data()), out_bitstream.size());
                }
            }

            if (frameIdx % 10 == 0) {
                std::cout << "\rTranscoding Frame: " << (frameIdx + 1) << "/" << total_frames << std::flush;
            }
        }

        // 14. 视频写入完成，关闭临时流文件
        vkDeviceWaitIdle(device);
        tempFile.close();

        // 15. 清理局部显卡与编码资源
        for (int i = 0; i < 2; ++i) {
            vkDestroyBuffer(device, exportStagingBuffers[i].buffer, nullptr);
            vkFreeMemory(device, exportStagingBuffers[i].memory, nullptr);
        }

        encoder.DestroySemaphore(cudaWaitSem);
        vkDestroySampler(device, computeSampler, nullptr);
        vkDestroyDescriptorPool(device, compPool, nullptr);
        vkDestroyDescriptorSetLayout(device, computeLayout, nullptr);
        vkDestroyCommandPool(device, gfxPool, nullptr);
        vkDestroyCommandPool(device, cmtPool, nullptr);
        vkDestroySemaphore(device, semRenderDone, nullptr);
        vkDestroySemaphore(device, semComputeDone, nullptr);

        CleanupVulkanResources(device, descSetLayout, VK_NULL_HANDLE, descriptors, pipelines);
    } // <--- 作用域结束，vkCtx 自适应析构，保证所有 Vulkan 核心设备完全安全销毁

    // ========================================================================
    // 16. 调用封装器，将临时裸流极速、无损地打包（Mux）为标准的 MP4 容器格式 [1]
    // ========================================================================
    std::cout << "\n[Exporter] Re-muxing temporary stream to standards-compliant MP4..." << std::endl;
    bool mux_success = false;

    if (exportCfg.codec == VideoCodec::AV1) {
        mux_success = ConvertAv1ToMp4(tempPath, finalPath, exWidth, exHeight, FPS_);
    } else if (exportCfg.codec == VideoCodec::HEVC) {
        mux_success = ConvertH265ToMp4(tempPath, finalPath, exWidth, exHeight, FPS_);
    } else {
        mux_success = ConvertH264ToMp4(tempPath, finalPath, exWidth, exHeight, FPS_);
    }

    // 17. 删除磁盘上的临时缓存文件
    std::remove(tempPath.c_str());

    if (mux_success) {
        std::cout << "[Exporter] SUCCESS: Video remuxed and saved to " << finalPath << std::endl;
    } else {
        std::cerr << "[Exporter] ERROR: MP4 multiplexing failed. Raw stream preserved in " << tempPath << std::endl;
    }
}
} // namespace StuCanvas
