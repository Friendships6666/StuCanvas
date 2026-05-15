// stucanvas/canvas/canvas.hpp
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

#include "../types/point.hpp"         // Point3D_GPU
#include "../types/segment_strip.hpp"  // SegmentStrip3D_GPU
#include "../types/triangles.hpp"      // Triangles3D_GPU
#include "../types/path.hpp"           // Path3D_GPU
#include <vulkan/vulkan.h>
#include "../canvas/vulkan/init.hpp"
#include "../canvas/vulkan/swap_chains.hpp"
#include "../canvas/vulkan/renderpass.hpp"
#include "../canvas/vulkan/pipeline.hpp"
#include "../canvas/vulkan/shader_module.hpp"
#include "../canvas/vulkan/buffer.hpp"
#include "../canvas/vulkan/present.hpp"
#include "../utils/block_deque.hpp"

namespace StuCanvas
{
    namespace Vulkan {
        struct CanvasShaderGroup {
            ShaderModule vertPoints, fragPoints;
            ShaderModule vertSegments, fragSegments;
            ShaderModule vertPaths, fragPaths;
            ShaderModule vertTriangles, fragTriangles;
        };
        struct CanvasPipelineGroup {
            std::unique_ptr<Pipeline> points;
            std::unique_ptr<Pipeline> segments;
            std::unique_ptr<Pipeline> paths;
            std::unique_ptr<Pipeline> triangles;
        };
        struct CanvasBufferGroup {
            Buffer points;
            Buffer segments;
            Buffer paths;
            Buffer triangleVertices;
            Buffer triangleIndices;
        };
        struct CanvasDescriptorGroup {
            VkDescriptorPool pool = VK_NULL_HANDLE;
            VkDescriptorSet points = VK_NULL_HANDLE;
            VkDescriptorSet segments = VK_NULL_HANDLE;
            VkDescriptorSet paths = VK_NULL_HANDLE;
            VkDescriptorSet triangles = VK_NULL_HANDLE;

            // 辅助：销毁池（集会自动释放）
            void cleanup(VkDevice device) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device, pool, nullptr);
                    pool = VK_NULL_HANDLE;
                }
            }
        };
    }

    template <typename T>
    struct ProcessedFrameData {
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

    enum class VideoCodec {
        AV1,    // 推荐用于 8K，极致压缩
        HEVC,   // 即 H.265，平衡兼容性与画质
        H264    // 即 AVC，最大兼容性（通常限制在 4K）
    };

    struct ExportSettings {
        // --- 基础配置 ---
        VideoCodec codec = VideoCodec::AV1;
        uint32_t width = 7680;  // 默认 8K
        uint32_t height = 4320;
        std::string outputPath = "output.ivf"; // 原生编码通常输出 IVF 容器

        // --- 编码质量配置 ---
        uint32_t bitRateKbps = 50000; // 50 Mbps，对于 8K AV1 较合适
        uint32_t maxBitRateKbps = 80000;
        uint32_t gopSize = 60;        // 关键帧间隔（通常设为与 FPS 一致）

        // 编码速度/质量平衡 (1-7): 1 最快, 7 质量最好 (对应 NVENC 预设)
        uint32_t tuningPreset = 7;
    };
    struct CanvasSettings {
        // --- 全局渲染质量 ---
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        bool enableSampleShading = true;
        float minSampleShading = 0.5f;

        // --- 视频导出设置 ---
        ExportSettings exportConfigs;
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

    private:
        utils::BlockDeque<CanvasClip, 256> clips_;
        std::vector<size_t> sorted_indices_; ///< 按 start_frame 升序的索引
        CameraConfig<T> camera_;
        T curCenterX_{0}, curCenterY_{0}, curCenterZ_{0}, curScale_{1};
        CanvasSettings settings_; // 全局配置成员
        uint32_t FPS_ = 60;

        Vulkan::CanvasShaderGroup LoadAllShaders(VkDevice device) {
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
        VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device) {
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
            if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descSetLayout) != VK_SUCCESS) {
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

            if (vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &group.pool) != VK_SUCCESS) {
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
            if (vkAllocateDescriptorSets(device, &allocInfo, descSets) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate descriptor sets");
            }

            // 3. 映射到结构体成员
            group.points    = descSets[0];
            group.segments  = descSets[1];
            group.paths     = descSets[2];
            group.triangles = descSets[3];

            return group;
        }


        VkCommandPool CreateCommandPool(VkDevice device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = flags;

            VkCommandPool pool;
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create command pool");
            }
            return pool;
        }



    ProcessedFrameData<double> PrepareFrameData(uint64_t current_frame) {
        ProcessedFrameData<double> data;

        // 1. 获取当前帧的所有片段并排序
        auto clips = GetClipsAtFrame(current_frame);
        std::sort(clips.begin(), clips.end(), [](const CanvasClip* a, const CanvasClip* b) {
            return a->render_order < b->render_order;
        });

        // 2. 执行动画逻辑 (Update 会清空并重新生成 clip 内部的几何数据)
        for (auto* clip : clips) {
            clip->points.clear();
            clip->paths.clear();
            clip->triangles.clear();
            clip->segments.clear();
            clip->Update(current_frame, FPS_);
        }

        // 3. 计算包围盒 (用于归一化)
        T minX = std::numeric_limits<T>::max(), minY = minX, minZ = minX;
        T maxX = std::numeric_limits<T>::lowest(), maxY = maxX, maxZ = maxX;

        for (const auto* clip : clips) {
            for (const auto& p : clip->points) {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);

            }
            for (const auto& strip : clip->segments) {
                for (const auto& p : strip.vertices) {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                    minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);

                }
            }
            for (const auto& path : clip->paths) {
                for (const auto& cp : path.control_points) {
                    minX = std::min(minX, cp.x); maxX = std::max(maxX, cp.x);
                    minY = std::min(minY, cp.y); maxY = std::max(maxY, cp.y);
                    minZ = std::min(minZ, cp.z); maxZ = std::max(maxZ, cp.z);

                }
            }
            for (const auto& tri : clip->triangles) {
                for (const auto& p : tri.points) {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                    minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);

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
            curCenterX_ = data.centerX; curCenterY_ = data.centerY;
            curCenterZ_ = data.centerZ; curScale_ = data.scale;

            for (const auto* clip : clips) {
                // 处理点云
                for (const auto& p : clip->points) {
                    PointDataGPU gpu;
                    gpu.x = (float)((p.x - data.centerX) / data.scale);
                    gpu.y = (float)((p.y - data.centerY) / data.scale);
                    gpu.z = (float)((p.z - data.centerZ) / data.scale);
                    gpu.r = p.r; gpu.g = p.g; gpu.b = p.b; gpu.a = p.a;
                    data.allPoints.push_back(gpu);
                }
                // 处理线段
                for (const auto& strip : clip->segments) {
                    if (strip.vertices.size() < 2) continue;
                    for (size_t i = 0; i < strip.vertices.size() - 1; ++i) {
                        const auto& p0 = strip.vertices[i];
                        const auto& p1 = strip.vertices[i + 1];
                        SegmentGPU seg;
                        seg.startX = (float)((p0.x - data.centerX) / data.scale);
                        seg.startY = (float)((p0.y - data.centerY) / data.scale);
                        seg.startZ = (float)((p0.z - data.centerZ) / data.scale);
                        seg.endX = (float)((p1.x - data.centerX) / data.scale);
                        seg.endY = (float)((p1.y - data.centerY) / data.scale);
                        seg.endZ = (float)((p1.z - data.centerZ) / data.scale);
                        seg.startR = p0.r; seg.startG = p0.g; seg.startB = p0.b; seg.startA = p0.a;
                        seg.endR = p1.r; seg.endG = p1.g; seg.endB = p1.b; seg.endA = p1.a;
                        data.allSegments.push_back(seg);
                    }
                }
                // 处理路径
                for (const auto& path : clip->paths) {
                    for (const auto& cp : path.control_points) {
                        PointDataGPU gpu;
                        gpu.x = (float)((cp.x - data.centerX) / data.scale);
                        gpu.y = (float)((cp.y - data.centerY) / data.scale);
                        gpu.z = (float)((cp.z - data.centerZ) / data.scale);
                        gpu.r = cp.r; gpu.g = cp.g; gpu.b = cp.b; gpu.a = cp.a;
                        data.allPathPoints.push_back(gpu);
                    }
                }
                // 处理三角面
                for (const auto& tri : clip->triangles) {
                    uint32_t base = (uint32_t)data.allTriVertices.size();
                    for (const auto& p : tri.points) {
                        PointDataGPU gpu;
                        gpu.x = (float)((p.x - data.centerX) / data.scale);
                        gpu.y = (float)((p.y - data.centerY) / data.scale);
                        gpu.z = (float)((p.z - data.centerZ) / data.scale);
                        gpu.r = p.r; gpu.g = p.g; gpu.b = p.b; gpu.a = p.a;
                        data.allTriVertices.push_back(gpu);
                    }
                    for (auto idx : tri.indices) data.allTriIndices.push_back(base + idx);
                }
            }

        return data;
    }
    };

    template <typename T>
    void NLECanvas<T>::render(uint64_t start_frame)
    {
        using namespace Vulkan;

        // 1. Vulkan 初始化
        VulkanInit vkInit("StuCanvas Point Render", 800, 600, true);
        VkDevice device = vkInit.getDevice();

        VkFormat swapchainFormat;
        {
            SwapChain tempSC(vkInit.getPhysicalDevice(), device,
                             vkInit.getSurface(), VK_NULL_HANDLE,
                             vkInit.getGraphicsFamily(), vkInit.getPresentFamily(),
                             vkInit.getWindow(),settings_.msaaSamples);
            swapchainFormat = tempSC.getImageFormat();
        }

        RenderPass renderPass(device, swapchainFormat,settings_.msaaSamples);

        auto shaders = LoadAllShaders(device);


        auto descSetLayout = CreateDescriptorSetLayout(device);
        auto pipelines = CreatePipelines(device, renderPass.get(), descSetLayout, shaders);


        auto descriptors = CreateDescriptors(device, descSetLayout);


        Presenter presenter(
            vkInit.getPhysicalDevice(), vkInit.getDevice(),
            vkInit.getSurface(), vkInit.getGraphicsFamily(),
            vkInit.getPresentFamily(), vkInit.getGraphicsQueue(),
            vkInit.getPresentQueue(), renderPass.get(), vkInit.getWindow(),settings_.msaaSamples);

        uint64_t current_frame = start_frame;
        bool is_paused = false;
        uint64_t last_time = SDL_GetTicks();
        bool frame_dirty = true;

        CameraConfig<T>& cam = camera_;
        bool running = true;
        SDL_Event event;

        Vulkan::Buffer pointBuffer; // 动态复用的点数据缓冲
        Vulkan::Buffer segmentBuffer;
        Vulkan::Buffer pathBuffer;
        Vulkan::Buffer triangleVertexBuffer;
        Vulkan::Buffer triangleIndexBuffer;
        // --- C. 当帧发生改变时，重新收集数据并构建缓冲 ---
        ProcessedFrameData<double> frameData;

        VkCommandPool uploadPool = CreateCommandPool(device, vkInit.getGraphicsFamily(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

        // 6. 主渲染循环
        while (running)
        {
            // --- A. 事件处理与按键交互 ---
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_RESIZED) presenter.markResized();
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    auto key = event.key.key;
                    if (key == SDLK_ESCAPE)
                    {
                        running = false;
                    }
                    else if (key == SDLK_SPACE)
                    {
                        is_paused = !is_paused;
                        last_time = SDL_GetTicks(); // 恢复播放时重置时间戳，防止跳帧
                        frame_dirty = true;
                    }
                    else if (key == SDLK_LEFT)
                    {
                        if (current_frame > 0) current_frame--;
                        frame_dirty = true;
                    }
                    else if (key == SDLK_RIGHT)
                    {
                        current_frame++;
                        frame_dirty = true;
                    }
                    else if (key == SDLK_R)
                    {
                        current_frame = 0;
                        frame_dirty = true;
                    }
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


            if (frame_dirty)
            {
                // 动态更新窗口标题反馈当前状态
                std::string title = "StuCanvas Render - Frame: " + std::to_string(current_frame) +
                    (is_paused ? " (Paused)" : " (Playing)");
                SDL_SetWindowTitle(vkInit.getWindow(), title.c_str());

                auto clips = GetClipsAtFrame(current_frame);
                std::sort(clips.begin(), clips.end(), [](const CanvasClip* a, const CanvasClip* b)
                {
                    return a->render_order < b->render_order;
                });

                frameData = PrepareFrameData(current_frame);

                // 重新上传缓冲区并关联描述符集
                if (!frameData.isEmpty())
                {
                    // 等待之前的渲染操作结束才能替换缓冲区（简易同步手段）
                    vkDeviceWaitIdle(device);



                    // --------------------------
                    // 1. 上传点云数据
                    // --------------------------
                    if (!frameData.allPoints.empty())
                    {
                        size_t pointsSize = frameData.allPoints.size() * sizeof(PointDataGPU);

                        // 利用 C++ 移动语义安全释放上一个缓冲对象并建立新缓冲
                        pointBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), frameData.allPoints.data(), pointsSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // 绑定新的 Buffer 到管线描述符集上
                        VkDescriptorBufferInfo bufferInfo{};
                        bufferInfo.buffer = pointBuffer.getBuffer();
                        bufferInfo.offset = 0;
                        bufferInfo.range = pointsSize;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descriptors.points; // <--- 修复：这里使用确切的点云描述符集
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    // --------------------------
                    // 2. 上传线段数据
                    // --------------------------
                    if (!frameData.allSegments.empty())
                    {
                        size_t segmentsSize = frameData.allSegments.size() * sizeof(SegmentGPU);

                        // 建立线段新缓冲
                        segmentBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), frameData.allSegments.data(), segmentsSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // 绑定新的 Buffer 到管线描述符集上
                        VkDescriptorBufferInfo bufferInfo{};
                        bufferInfo.buffer = segmentBuffer.getBuffer();
                        bufferInfo.offset = 0;
                        bufferInfo.range = segmentsSize;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descriptors.segments; // <--- 修复：这里使用确切的线段描述符集
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    if (!frameData.allPoints.empty())
                    {
                        size_t size = frameData.allPathPoints.size() * sizeof(PointDataGPU);
                        pathBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), frameData.allPathPoints.data(), size,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        VkDescriptorBufferInfo bInfo{pathBuffer.getBuffer(), 0, size};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descriptors.paths;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    // --------------------------
                    // 4. 上传三角面数据 (Vertices & Indices)
                    // --------------------------
                    if (!frameData.allTriVertices.empty())
                    {
                        // A. 上传顶点到 SSBO (供 Shader Binding 0 使用)
                        size_t vBufferSize = frameData.allTriVertices.size() * sizeof(PointDataGPU);
                        triangleVertexBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), frameData.allTriVertices.data(), vBufferSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // B. 上传索引到 Index Buffer (供 vkCmdBindIndexBuffer 使用)
                        size_t iBufferSize = frameData.allTriIndices.size() * sizeof(uint32_t);
                        triangleIndexBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), frameData.allTriIndices.data(), iBufferSize,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // C. 更新描述符集 (将顶点 Buffer 绑定到三角形 Shader 的 Binding 0)
                        VkDescriptorBufferInfo triBufferInfo{};
                        triBufferInfo.buffer = triangleVertexBuffer.getBuffer();
                        triBufferInfo.offset = 0;
                        triBufferInfo.range = vBufferSize;

                        VkWriteDescriptorSet triWrite{};
                        triWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        triWrite.dstSet = descriptors.triangles; // 确保这是分配的第4个描述符集
                        triWrite.dstBinding = 0;
                        triWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        triWrite.descriptorCount = 1;
                        triWrite.pBufferInfo = &triBufferInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &triWrite, 0, nullptr);
                    }

                    // 销毁临时的指令池

                }

                frame_dirty = false;
            }

            // --- D. 实际画面呈现 (不论帧变没变，屏幕需要保持渲染状态) ---
            uint32_t imageIndex;
            VkCommandBuffer cmd = presenter.beginFrame(imageIndex);
            if (cmd == VK_NULL_HANDLE) continue;

            auto extent = presenter.getExtent();
            CameraConfig<float> transformedCam = camera_.GetNormalizedConfig(
                curCenterX_, curCenterY_, curCenterZ_, curScale_);
            float viewW = static_cast<float>(extent.width);
            float viewH = static_cast<float>(extent.height);
            float aspect = (float)extent.width / (float)extent.height;

            Eigen::Matrix4f viewMat = ComputeViewMatrix(transformedCam);
            Eigen::Matrix4f projMat = ComputeProjectionMatrix(transformedCam, aspect);
            Eigen::Matrix4f mvpMat = projMat * viewMat;

            struct PushConstants
            {
                float mvp[16]; // 64 bytes
                float camPos[3]; // 12 bytes
                float _pad0; // 4 bytes (显式对齐)
                float viewportSize[2]; // 8 bytes
                float pointRadius; // 4 bytes (在三角形中可忽略或复用)
                float _pad1; // 4 bytes (补齐至 96 字节，对齐 16)
            } pc;
            pc.camPos[0] = transformedCam.posX;
            pc.camPos[1] = transformedCam.posY;
            pc.camPos[2] = transformedCam.posZ;
            // 将 Column-Major 改为 Row-Major 拷贝，适配 Slang 的 mul(M, v)
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    pc.mvp[row * 4 + col] = mvpMat(row, col);
                }
            }
            pc.viewportSize[0] = viewW;
            pc.viewportSize[1] = viewH;
            pc.pointRadius = 5.0f;

            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = renderPass.get();
            rpBegin.framebuffer = presenter.getFramebuffer(imageIndex);
            rpBegin.renderArea.offset = {0, 0};
            rpBegin.renderArea.extent = extent;
            std::array<VkClearValue, 2> clearValues{};
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // 背景色
            clearValues[1].depthStencil = {1.0f, 0}; // 深度清空为 1.0 (最远)
            rpBegin.clearValueCount = 2;
            rpBegin.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            if (!frameData.isEmpty())
            {
                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = viewW;
                viewport.height = viewH;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{{0, 0}, extent};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                // --------------------------
                // 绘制 1：线段
                // --------------------------
                if (!frameData.allSegments.empty())
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.segments->get());

                    pc.pointRadius = 3.0f; // 复用同一个内存位：这是线宽 (lineWidth)
                    vkCmdPushConstants(cmd, pipelines.segments->getLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(PushConstants), &pc);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelines.segments->getLayout(), 0, 1, &descriptors.segments, 0, nullptr);

                    // 线段实例化渲染 (每个线段 6 个顶点组成 Quad)
                    vkCmdDraw(cmd, 6, static_cast<uint32_t>(frameData.allSegments.size()), 0, 0);
                }

                // --------------------------
                // 绘制 2：点云
                // --------------------------
                if (!frameData.allPoints.empty())
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.points->get());

                    pc.pointRadius = 5.0f; // 恢复点半径
                    vkCmdPushConstants(cmd, pipelines.points->getLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(PushConstants), &pc);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelines.points->getLayout(), 0, 1, &descriptors.points, 0, nullptr);

                    // 点云实例化渲染 (每个点 3 个顶点组成超大三角形包裹圆)
                    vkCmdDraw(cmd, 3, static_cast<uint32_t>(frameData.allPoints.size()), 0, 0);
                }
            }

            if (!frameData.allPathPoints.empty())
            {
                // 至少有 4 个点才能构成一段三次贝塞尔
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.paths->get());

                // 设置路径宽度 (利用 pc.pointRadius 对应的位置，Shader 里叫 strokeWidth)
                pc.pointRadius = 4.0f;
                vkCmdPushConstants(cmd, pipelines.paths->getLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelines.paths->getLayout(), 0, 1, &descriptors.paths, 0, nullptr);

                // 计算总段数：(总点数 - 1) / 3
                // 注意：如果存在多条独立的 Path，建议在 CPU 端循环调用，或在 Buffer 中处理间隙
                uint32_t segmentCount = (static_cast<uint32_t>(frameData.allPathPoints.size()) - 1) / 3;

                // 绘制：每个 Instance 是一个四边形(6顶点)，覆盖一段曲线
                vkCmdDraw(cmd, 6, segmentCount, 0, 0);
            }

            // --------------------------
            // 绘制 4：三角面网格 (Triangle Mesh)
            // --------------------------
            if (!frameData.allTriVertices.empty())
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangles->get());

                // 更新包含 camPos 的 PushConstants (确保 pc 结构体为 96 字节)
                vkCmdPushConstants(cmd, pipelines.triangles->getLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);

                // 绑定描述符集 (这会自动绑定顶点 SSBO)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelines.triangles->getLayout(), 0, 1, &descriptors.triangles, 0, nullptr);

                // 绑定物理索引缓冲
                vkCmdBindIndexBuffer(cmd, triangleIndexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);

                // 执行索引绘制：索引总数，1个实例，起始索引0，顶点偏移0，起始实例0
                vkCmdDrawIndexed(cmd, static_cast<uint32_t>(frameData.allTriIndices.size()), 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(cmd);
            presenter.endFrame(cmd, imageIndex);
        }


        vkDeviceWaitIdle(vkInit.getDevice());
        vkDestroyDescriptorSetLayout(vkInit.getDevice(), descSetLayout, nullptr);
        vkDestroyCommandPool(vkInit.getDevice(), uploadPool, nullptr);
        vkDestroyDescriptorPool(vkInit.getDevice(), descriptors.pool, nullptr);

    }
} // namespace StuCanvas
