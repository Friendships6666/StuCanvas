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

namespace StuCanvas
{
    /**
     * @brief NLE 片段，拥有自己的时间区间和图形数据。
     * @tparam T 世界坐标数值类型（float / double），目前未在数据结构中直接使用，
     *           但为将来扩展保留。
     */
    template <typename T>
    struct Clip
    {
        using UpdateFunc = std::function<void(uint64_t)>; ///< 每相对帧调用的更新函数

        uint64_t start_frame{}; ///< 起始绝对帧
        uint64_t end_frame{}; ///< 结束绝对帧（含）
        uint64_t render_order{};
        std::string name; ///< 可选的片段名称

        // ---- 四种矢量图形 ----
        std::vector<Point3D<T>> points; ///< 点云
        std::vector<SegmentStrip3D<T>> segments; ///< 线段带

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

        /// 调用更新函数，传入相对帧 t = global_frame - start_frame
        void Update(uint64_t global_frame)
        {
            if (update_func && ContainsFrame(global_frame))
            {
                uint64_t rel_t = global_frame - start_frame;
                update_func(rel_t);
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


        CameraConfig<T>& GetCameraConfig() { return camera_; }
        [[nodiscard]] const CameraConfig<T>& GetCameraConfig() const { return camera_; }

        /**
         * @brief 添加一个片段。
         */
        void AddClip(const CanvasClip& clip)
        {
            clips_.push_back(clip);
            // 更新排序索引：插入后保持按 start_frame 升序
            size_t idx = clips_.size() - 1;
            auto it = std::lower_bound(sorted_indices_.begin(), sorted_indices_.end(), idx,
                                       [this](size_t a, size_t b)
                                       {
                                           return clips_[a].start_frame < clips_[b].start_frame;
                                       });
            sorted_indices_.insert(it, idx);
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
        void render(uint64_t start_frame = 0, uint32_t fps = 60);

    private:
        std::vector<CanvasClip> clips_; ///< 所有片段
        std::vector<size_t> sorted_indices_; ///< 按 start_frame 升序的索引
        CameraConfig<T> camera_;
        // 缓存当前的归一化参数，用于矩阵计算
        T curCenterX_{0}, curCenterY_{0}, curCenterZ_{0}, curScale_{1};
    };

    template <typename T>
    void NLECanvas<T>::render(uint64_t start_frame, uint32_t fps)
    {
        using namespace Vulkan;

        // 1. Vulkan 初始化
        VulkanInit vkInit("StuCanvas Point Render", 800, 600, true);

        // 获取交换链格式（用于创建 RenderPass）
        VkFormat swapchainFormat;
        {
            SwapChain tempSC(vkInit.getPhysicalDevice(), vkInit.getDevice(),
                             vkInit.getSurface(), VK_NULL_HANDLE,
                             vkInit.getGraphicsFamily(), vkInit.getPresentFamily(),
                             vkInit.getWindow());
            swapchainFormat = tempSC.getImageFormat();
        }

        RenderPass renderPass(vkInit.getDevice(), swapchainFormat);

        // 2. 加载着色器
        auto vertModulePoints = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/points.slang", "vertex",
            "vertexMain");
        auto fragModulePoints = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/points.slang", "fragment",
            "fragmentMain");


        auto vertModuleSegments = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/segments.slang", "vertex",
            "vertexMain");
        auto fragModuleSegments = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/segments.slang", "fragment",
            "fragmentMain");

        auto vertModulePaths = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/paths.slang", "vertex",
            "vertexMain");
        auto fragModulePaths = ShaderModule::fromSlangFile(
            vkInit.getDevice(), "/home/friendships666/Projects/StuCanvas/stucanvas/shaders/paths.slang", "fragment",
            "fragmentMain");


        // 3. 描述符集布局
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = 0;
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &layoutBinding;
        VkDescriptorSetLayout descSetLayout;
        vkCreateDescriptorSetLayout(vkInit.getDevice(), &layoutInfo, nullptr, &descSetLayout);

        // 4. 管线配置
        PipelineConfig config;
        config.vertShaderModule = vertModulePoints.getModule();
        config.fragShaderModule = fragModulePoints.getModule();
        config.vertEntry = "main";
        config.fragEntry = "main";
        config.vertexBindingCount = 0;
        config.pVertexBindings = nullptr;
        config.vertexAttributeCount = 0;
        config.pVertexAttributes = nullptr;
        config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        config.cullMode = VK_CULL_MODE_NONE;
        config.frontFace = VK_FRONT_FACE_CLOCKWISE;
        config.cullMode = VK_CULL_MODE_NONE; // 关键：关闭背向剔除
        config.descriptorSetLayouts.push_back(descSetLayout);

        // 开启混合，支持抗锯齿圆
        config.blendEnable = VK_TRUE;
        config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        config.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        config.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(float) * 16 + sizeof(float) * 2 + sizeof(float);
        config.pushConstantRanges.push_back(pcRange);

        Pipeline pipelinePoints(vkInit.getDevice(), renderPass.get(), config);


        config.vertShaderModule = vertModuleSegments.getModule();
        config.fragShaderModule = fragModuleSegments.getModule();
        Pipeline pipelineSegments(vkInit.getDevice(), renderPass.get(), config);

        // 5. 描述符池及分配
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 3;
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        descPoolInfo.maxSets = 3;
        VkDescriptorPool descPool;
        vkCreateDescriptorPool(vkInit.getDevice(), &descPoolInfo, nullptr, &descPool);
        std::vector<VkDescriptorSetLayout> layouts = {descSetLayout, descSetLayout, descSetLayout};

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descPool;
        allocInfo.descriptorSetCount = 3;
        allocInfo.pSetLayouts = &descSetLayout;
        allocInfo.pSetLayouts = layouts.data();
        VkDescriptorSet descSets[3]; // descSets[0]给点云，descSets[1]给线段
        vkAllocateDescriptorSets(vkInit.getDevice(), &allocInfo, descSets);
        VkDescriptorSet descSetPoints = descSets[0];
        VkDescriptorSet descSetSegments = descSets[1];
        VkDescriptorSet descSetPaths = descSets[2];
        config.vertShaderModule = vertModulePaths.getModule();
        config.fragShaderModule = fragModulePaths.getModule();
        Pipeline pipelinePaths(vkInit.getDevice(), renderPass.get(), config);
        Presenter presenter(
            vkInit.getPhysicalDevice(), vkInit.getDevice(),
            vkInit.getSurface(), vkInit.getGraphicsFamily(),
            vkInit.getPresentFamily(), vkInit.getGraphicsQueue(),
            vkInit.getPresentQueue(), renderPass.get(), vkInit.getWindow());

        // ----------------- 播放与动画控制变量 -----------------
        uint64_t current_frame = start_frame;
        bool is_paused = false;
        uint64_t last_time = SDL_GetTicks();
        bool frame_dirty = true;

        CameraConfig<T>& cam = camera_;
        bool running = true;
        SDL_Event event;

        Vulkan::Buffer pointBuffer; // 动态复用的点数据缓冲
        size_t numPointsToDraw = 0; // 当前帧实际需要绘制的点数
        Vulkan::Buffer segmentBuffer;
        size_t numSegmentsToDraw = 0;
        Vulkan::Buffer pathBuffer;
        size_t totalPathPoints = 0;

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
            if (!is_paused && fps > 0)
            {
                uint64_t frame_duration_ms = 1000ULL / fps;
                if (current_time - last_time >= frame_duration_ms)
                {
                    current_frame++;
                    frame_dirty = true;
                    last_time = current_time;
                }
            }

            // --- C. 当帧发生改变时，重新收集数据并构建缓冲 ---
            if (frame_dirty)
            {
                // 动态更新窗口标题反馈当前状态
                std::string title = "StuCanvas Point Render - Frame: " + std::to_string(current_frame) +
                    (is_paused ? " (Paused)" : " (Playing)");
                SDL_SetWindowTitle(vkInit.getWindow(), title.c_str());

                auto clips = GetClipsAtFrame(current_frame);
                std::sort(clips.begin(), clips.end(), [](const CanvasClip* a, const CanvasClip* b)
                {
                    return a->render_order < b->render_order;
                });

                // 执行当前帧的动画逻辑
                for (auto* clip : clips)
                {
                    clip->Update(current_frame);
                }

                // 收集包围盒用于局部归一化
                T minX = std::numeric_limits<T>::max();
                T minY = std::numeric_limits<T>::max();
                T minZ = std::numeric_limits<T>::max();
                T maxX = std::numeric_limits<T>::lowest();
                T maxY = std::numeric_limits<T>::lowest();
                T maxZ = std::numeric_limits<T>::lowest();

                bool hasData = false;
                for (const auto* clip : clips)
                {
                    // 收集点云包围盒
                    for (const auto& p : clip->points)
                    {
                        minX = std::min(minX, p.x);
                        maxX = std::max(maxX, p.x);
                        minY = std::min(minY, p.y);
                        maxY = std::max(maxY, p.y);
                        minZ = std::min(minZ, p.z);
                        maxZ = std::max(maxZ, p.z);
                        hasData = true;
                    }
                    // 收集线段带包围盒
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
                            hasData = true;
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
                            hasData = true;
                        }
                    }
                }


                std::vector<PointDataGPU> allPoints;
                std::vector<SegmentGPU> allSegments;
                std::vector<PointDataGPU> allPathPoints;
                if (hasData)
                {
                    T centerX = (minX + maxX) * static_cast<T>(0.5);
                    T centerY = (minY + maxY) * static_cast<T>(0.5);
                    T centerZ = (minZ + maxZ) * static_cast<T>(0.5);
                    T scale = std::max({maxX - centerX, maxY - centerY, maxZ - centerZ});
                    if (scale <= static_cast<T>(0)) scale = static_cast<T>(1);
                    curCenterX_ = centerX;
                    curCenterY_ = centerY;
                    curCenterZ_ = centerZ;
                    curScale_ = scale;

                    for (const auto* clip : clips)
                    {
                        for (const auto& p : clip->points)
                        {
                            PointDataGPU gpu{};
                            double nx = static_cast<double>(p.x - centerX) / static_cast<double>(scale);
                            double ny = static_cast<double>(p.y - centerY) / static_cast<double>(scale);
                            double nz = static_cast<double>(p.z - centerZ) / static_cast<double>(scale);
                            gpu.x = static_cast<float>(nx);
                            gpu.y = static_cast<float>(ny);
                            gpu.z = static_cast<float>(nz);
                            gpu._pad0 = 0.0f; // 对齐位补位
                            gpu.r = p.r;
                            gpu.g = p.g;
                            gpu.b = p.b;
                            gpu.a = p.a;
                            allPoints.push_back(gpu);
                        }
                        for (const auto& strip : clip->segments)
                        {
                            size_t vCount = strip.vertices.size();
                            if (vCount < 2) continue;

                            for (size_t i = 0; i < vCount - 1; ++i)
                            {
                                // 强行忽略闭合
                                const auto& p0 = strip.vertices[i];
                                const auto& p1 = strip.vertices[i + 1];

                                SegmentGPU seg{};
                                seg.startX = static_cast<float>((p0.x - curCenterX_) / curScale_);
                                seg.startY = static_cast<float>((p0.y - curCenterY_) / curScale_);
                                seg.startZ = static_cast<float>((p0.z - curCenterZ_) / curScale_);

                                seg.endX = static_cast<float>((p1.x - curCenterX_) / curScale_);
                                seg.endY = static_cast<float>((p1.y - curCenterY_) / curScale_);
                                seg.endZ = static_cast<float>((p1.z - curCenterZ_) / curScale_);

                                // 端点颜色
                                seg.startR = p0.r;
                                seg.startG = p0.g;
                                seg.startB = p0.b;
                                seg.startA = p0.a;
                                seg.endR = p1.r;
                                seg.endG = p1.g;
                                seg.endB = p1.b;
                                seg.endA = p1.a;

                                allSegments.push_back(seg);
                            }
                        }
                        for (const auto& path : clip->paths)
                        {
                            // 将 Path 的点转换为 GPU 格式并归一化
                            for (const auto& cp : path.control_points)
                            {
                                PointDataGPU gpu;
                                gpu.x = static_cast<float>((cp.x - curCenterX_) / curScale_);
                                gpu.y = static_cast<float>((cp.y - curCenterY_) / curScale_);
                                gpu.z = static_cast<float>((cp.z - curCenterZ_) / curScale_);
                                gpu._pad0 = 0.0f;
                                gpu.r = cp.r;
                                gpu.g = cp.g;
                                gpu.b = cp.b;
                                gpu.a = cp.a;
                                allPathPoints.push_back(gpu);
                            }
                        }
                    }
                }

                numPointsToDraw = allPoints.size();
                numSegmentsToDraw = allSegments.size();
                totalPathPoints = allPathPoints.size();

                // 重新上传缓冲区并关联描述符集
                if (numPointsToDraw > 0 || numSegmentsToDraw > 0 || totalPathPoints > 0)
                {
                    // 等待之前的渲染操作结束才能替换缓冲区（简易同步手段）
                    vkDeviceWaitIdle(vkInit.getDevice());

                    VkCommandPool uploadPool;
                    VkCommandPoolCreateInfo poolInfo{};
                    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    poolInfo.queueFamilyIndex = vkInit.getGraphicsFamily();
                    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                    vkCreateCommandPool(vkInit.getDevice(), &poolInfo, nullptr, &uploadPool);

                    // --------------------------
                    // 1. 上传点云数据
                    // --------------------------
                    if (numPointsToDraw > 0)
                    {
                        size_t pointsSize = allPoints.size() * sizeof(PointDataGPU);

                        // 利用 C++ 移动语义安全释放上一个缓冲对象并建立新缓冲
                        pointBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), allPoints.data(), pointsSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // 绑定新的 Buffer 到管线描述符集上
                        VkDescriptorBufferInfo bufferInfo{};
                        bufferInfo.buffer = pointBuffer.getBuffer();
                        bufferInfo.offset = 0;
                        bufferInfo.range = pointsSize;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descSetPoints; // <--- 修复：这里使用确切的点云描述符集
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    // --------------------------
                    // 2. 上传线段数据
                    // --------------------------
                    if (numSegmentsToDraw > 0)
                    {
                        size_t segmentsSize = allSegments.size() * sizeof(SegmentGPU);

                        // 建立线段新缓冲
                        segmentBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), allSegments.data(), segmentsSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        // 绑定新的 Buffer 到管线描述符集上
                        VkDescriptorBufferInfo bufferInfo{};
                        bufferInfo.buffer = segmentBuffer.getBuffer();
                        bufferInfo.offset = 0;
                        bufferInfo.range = segmentsSize;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descSetSegments; // <--- 修复：这里使用确切的线段描述符集
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    if (totalPathPoints > 0)
                    {
                        size_t size = allPathPoints.size() * sizeof(PointDataGPU);
                        pathBuffer = Buffer::CreateAndUpload(
                            vkInit.getDevice(), vkInit.getPhysicalDevice(), uploadPool,
                            vkInit.getGraphicsQueue(), allPathPoints.data(), size,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

                        VkDescriptorBufferInfo bInfo{pathBuffer.getBuffer(), 0, size};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = descSetPaths;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bInfo;
                        vkUpdateDescriptorSets(vkInit.getDevice(), 1, &write, 0, nullptr);
                    }

                    // 销毁临时的指令池
                    vkDestroyCommandPool(vkInit.getDevice(), uploadPool, nullptr);
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
                float mvp[16];
                float viewportSize[2];
                float pointRadius;
            } pc;
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
            VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            if (numPointsToDraw > 0 || numSegmentsToDraw > 0 || totalPathPoints > 0)
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
                if (numSegmentsToDraw > 0)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineSegments.get());

                    pc.pointRadius = 3.0f; // 复用同一个内存位：这是线宽 (lineWidth)
                    vkCmdPushConstants(cmd, pipelineSegments.getLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(PushConstants), &pc);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelineSegments.getLayout(), 0, 1, &descSetSegments, 0, nullptr);

                    // 线段实例化渲染 (每个线段 6 个顶点组成 Quad)
                    vkCmdDraw(cmd, 6, static_cast<uint32_t>(numSegmentsToDraw), 0, 0);
                }

                // --------------------------
                // 绘制 2：点云
                // --------------------------
                if (numPointsToDraw > 0)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinePoints.get());

                    pc.pointRadius = 5.0f; // 恢复点半径
                    vkCmdPushConstants(cmd, pipelinePoints.getLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(PushConstants), &pc);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelinePoints.getLayout(), 0, 1, &descSetPoints, 0, nullptr);

                    // 点云实例化渲染 (每个点 3 个顶点组成超大三角形包裹圆)
                    vkCmdDraw(cmd, 3, static_cast<uint32_t>(numPointsToDraw), 0, 0);
                }
            }

            if (totalPathPoints >= 4)
            {
                // 至少有 4 个点才能构成一段三次贝塞尔
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinePaths.get());

                // 设置路径宽度 (利用 pc.pointRadius 对应的位置，Shader 里叫 strokeWidth)
                pc.pointRadius = 4.0f;
                vkCmdPushConstants(cmd, pipelinePaths.getLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstants), &pc);

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelinePaths.getLayout(), 0, 1, &descSetPaths, 0, nullptr);

                // 计算总段数：(总点数 - 1) / 3
                // 注意：如果存在多条独立的 Path，建议在 CPU 端循环调用，或在 Buffer 中处理间隙
                uint32_t segmentCount = (static_cast<uint32_t>(totalPathPoints) - 1) / 3;

                // 绘制：每个 Instance 是一个四边形(6顶点)，覆盖一段曲线
                vkCmdDraw(cmd, 6, segmentCount, 0, 0);
            }

            vkCmdEndRenderPass(cmd);
            presenter.endFrame(cmd, imageIndex);
        }

        vkDeviceWaitIdle(vkInit.getDevice());
        vkDestroyDescriptorSetLayout(vkInit.getDevice(), descSetLayout, nullptr);
        vkDestroyDescriptorPool(vkInit.getDevice(), descPool, nullptr);
    }
} // namespace StuCanvas
