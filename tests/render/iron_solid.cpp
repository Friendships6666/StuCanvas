// iron_solid.cpp
#define VMA_IMPLEMENTATION // 必须放在最前面
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <Eigen/Dense>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <cmath>

#include "stucanvas/canvas/vulkan/vk_ctx.hpp"
#include "stucanvas/canvas/vulkan/buffer.hpp"
#include "stucanvas/canvas/vulkan/shader_module.hpp"
#include "stucanvas/canvas/vulkan/raytracing_pipeline.hpp"
#include "stucanvas/canvas/vulkan/geometry.hpp"
#include "stucanvas/canvas/vulkan/raytracing_pass.hpp"
#include "stucanvas/canvas/vulkan/rt_present.hpp"

using namespace StuCanvas::Vulkan;

// ─────────────────────────────────────────────────────────────
// 1. 建立 RAII 离屏 Storage 图像管理类
// ─────────────────────────────────────────────────────────────
class StorageImage {
public:
    StorageImage(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height,
                 VkCommandPool commandPool, VkQueue queue)
        : device_(device), allocator_(allocator), extent_({ width, height })
    {
        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent = { width, height, 1 };
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(allocator_, &imgInfo, &allocInfo, &image_, &allocation_, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Storage Image.");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        if (vkCreateImageView(device_, &viewInfo, nullptr, &view_) != VK_SUCCESS) {
            cleanup();
            throw std::runtime_error("Failed to create Storage Image View.");
        }

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = commandPool;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        transitionImageLayout(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        vkCreateFence(device_, &fenceInfo, nullptr, &fence);

        vkQueueSubmit(queue, 1, &submitInfo, fence);
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool, 1, &cmd);
    }

    ~StorageImage() { cleanup(); }

    StorageImage(const StorageImage&) = delete;
    StorageImage& operator=(const StorageImage&) = delete;

    StorageImage(StorageImage&& other) noexcept
        : device_(other.device_), allocator_(other.allocator_), extent_(other.extent_),
          image_(other.image_), allocation_(other.allocation_), view_(other.view_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
    }

    StorageImage& operator=(StorageImage&& other) noexcept {
        if (this != &other) {
            cleanup();
            device_ = other.device_;
            allocator_ = other.allocator_;
            extent_ = other.extent_;
            image_ = other.image_;
            allocation_ = other.allocation_;
            view_ = other.view_;
            other.image_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.view_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkImage     getImage()  const { return image_; }
    [[nodiscard]] VkImageView getView()   const { return view_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;

    void cleanup() {
        if (allocator_ != VK_NULL_HANDLE) {
            if (view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view_, nullptr);
            if (image_ != VK_NULL_HANDLE) vmaDestroyImage(allocator_, image_, allocation_);
            view_ = VK_NULL_HANDLE;
            image_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        }
    }
};

// ─────────────────────────────────────────────────────────────
// 2. 组装锋利的钢铁正方体 (使用已在 geometry.hpp 声明的 48 字节对齐 Vertex)
// ─────────────────────────────────────────────────────────────
inline std::vector<Vertex> buildSteelCube() {
    std::vector<Vertex> vertices = {
        // Front Face (Normal: 0, 0, 1)
        { {-0.5f, -0.5f,  0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f,  0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f,  0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },
        { {-0.5f,  0.5f,  0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} },

        // Back Face (Normal: 0, 0, -1)
        { {-0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },
        { {-0.5f,  0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },

        // Top Face (Normal: 0, 1, 0)
        { {-0.5f,  0.5f, -0.5f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} },
        { {-0.5f,  0.5f,  0.5f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f,  0.5f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f, -0.5f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },

        // Bottom Face (Normal: 0, -1, 0)
        { {-0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f, -0.5f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f,  0.5f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },
        { {-0.5f, -0.5f,  0.5f, 1.0f}, {0.0f, -1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },

        // Right Face (Normal: 1, 0, 0)
        { { 0.5f, -0.5f, -0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f, -0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f,  0.5f,  0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f,  0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },

        // Left Face (Normal: -1, 0, 0)
        { {-0.5f, -0.5f, -0.5f, 1.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} },
        { {-0.5f, -0.5f,  0.5f, 1.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f} },
        { {-0.5f,  0.5f,  0.5f, 1.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f} },
        { {-0.5f,  0.5f, -0.5f, 1.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f} }
    };
    return vertices;
}

inline std::vector<uint32_t> buildSteelCubeIndices() {
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t offset = i * 4;
        indices.push_back(offset + 0);
        indices.push_back(offset + 1);
        indices.push_back(offset + 2);
        indices.push_back(offset + 2);
        indices.push_back(offset + 3);
        indices.push_back(offset + 0);
    }
    return indices;
}

// ─────────────────────────────────────────────────────────────
// 3. 主程序入口 (main)
// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL3" << std::endl;
        return -1;
    }

    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;

    SDL_Window* window = SDL_CreateWindow(
        "StuCanvas - Hardware Ray Traced Steel Cube (OpenPBR + Slang)",
        windowWidth, windowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Failed to create SDL Window" << std::endl;
        return -1;
    }

    // 默认开启相对鼠标模式以便进行 FPS 视角控制 (可通过 ESC 键进行释放/重新捕获)
    SDL_SetWindowRelativeMouseMode(window, true);

    try {
        VulkanContextConfig contextConfig{};
        contextConfig.appName = "StuCanvasRayTracing";
        contextConfig.apiVersion = VK_API_VERSION_1_3;
        contextConfig.enableValidation = true;

        uint32_t sdlExtCount = 0;
        const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        for (uint32_t i = 0; i < sdlExtCount; ++i) {
            contextConfig.requiredInstanceExtensions.push_back(sdlExtensions[i]);
        }

        VulkanContext context;
        context.initInstance(contextConfig);
        context.initSurface(window);

        VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
        bdaFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bdaFeatures.bufferDeviceAddress = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeatures.accelerationStructure = VK_TRUE;
        asFeatures.pNext = &bdaFeatures;

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
        rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
        rtPipelineFeatures.pNext = &asFeatures;

        std::vector<const char*> requiredDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
        };

        context.createLogicalDevice(0, requiredDeviceExtensions, &rtPipelineFeatures);

        const auto& primaryDev = context.getPrimaryDevice();
        VkDevice device = primaryDev.get();
        VkPhysicalDevice physDev = primaryDev.getPhysicalDevice();
        VmaAllocator allocator = primaryDev.getAllocator();

        VkQueue graphicsQueue = primaryDev.getGraphicsQueue().handle;
        uint32_t graphicsFamily = primaryDev.getGraphicsQueue().familyIndex;
        VkQueue presentQueue = primaryDev.getPresentQueue().handle;
        uint32_t presentFamily = primaryDev.getPresentQueue().familyIndex;

        RTPresenter presenter(physDev, device, context.getSurface(),
                               graphicsFamily, presentFamily,
                               graphicsQueue, presentQueue, window);

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = graphicsFamily;
        VkCommandPool commandPool;
        vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

        auto cubeVertices = buildSteelCube();
        auto cubeIndices = buildSteelCubeIndices();

        GPUMesh mesh = GPUMesh::upload(device, allocator, commandPool, graphicsQueue, cubeVertices, cubeIndices);

        RayTracingBuilder builder(device, allocator);

        auto geom = mesh.getASGeometry();
        auto range = mesh.getASBuildRange();
        AccelerationStructure blas = builder.buildBLAS({ geom }, { range.primitiveCount }, { range }, commandPool, graphicsQueue);

        // ─────────────────────────────────────────────────────────────
        // 使用 Eigen 构建 TLAS 实例的仿射变换矩阵 (3x4 RowMajor)
        // ─────────────────────────────────────────────────────────────
        Eigen::Matrix<float, 3, 4, Eigen::RowMajor> tlasTransformMatrix;
        tlasTransformMatrix.setIdentity(); // 默认为单位变换

        VkAccelerationStructureInstanceKHR instance{};
        std::memcpy(&instance.transform, tlasTransformMatrix.data(), sizeof(VkTransformMatrixKHR));

        instance.instanceCustomIndex = 0u;
        instance.mask = 0xFFu;
        instance.instanceShaderBindingTableRecordOffset = 0u;
        instance.flags = static_cast<int>(VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR);
        instance.accelerationStructureReference = blas.getDeviceAddress();

        Buffer instanceBuffer = Buffer::CreateAndUpload(
            device, allocator, commandPool, graphicsQueue,
            &instance, sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        );
        VkBufferDeviceAddressInfo instAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        instAddrInfo.buffer = instanceBuffer.get();
        VkDeviceAddress instAddress = vkGetBufferDeviceAddress(device, &instAddrInfo);

        AccelerationStructure tlas = builder.buildTLAS(instAddress, 1, commandPool, graphicsQueue);

        StorageImage storageImage(device, allocator, presenter.getExtent().width, presenter.getExtent().height, commandPool, graphicsQueue);

        // 1. 定义头文件搜索路径列表
        std::vector<std::string> includeDirs = { "shaders/" };

        // 2. 传入 includeDirs 作为第 5 个参数
        ShaderModule rgenShader = ShaderModule::fromSlangFile( // 兼容 Slang 动态加载
            device, "shaders/raytracing.slang", "raygeneration", "rayGenMain", includeDirs);

        ShaderModule missShader = ShaderModule::fromSlangFile(
            device, "shaders/raytracing.slang", "miss", "missMain", includeDirs);

        ShaderModule closestHitShader = ShaderModule::fromSlangFile(
            device, "shaders/raytracing.slang", "closesthit", "closestHitMain", includeDirs);
        RayTracingPass rtPass(physDev, device, allocator,
                               std::move(rgenShader), std::move(missShader), std::move(closestHitShader));
        rtPass.buildShaderBindingTable(commandPool, graphicsQueue);
        rtPass.updateDescriptorSet(tlas.get(), storageImage.getView(), mesh);

        bool running = true;
        uint32_t frameCounter = 0;

        // ── 初始化相机变量（WASD 飞行 + FPS 视角控制） ──
        Eigen::Vector3f cameraPos(0.0f, 0.0f, 2.5f);
        float yaw = -90.0f;  // 默认为偏航角 -90 度，朝向 -Z 方向
        float pitch = 0.0f;  // 俯仰角，默认为 0 度
        float mouseSensitivity = 0.1f;
        float moveSpeed = 3.0f; // 飞行移动速度（单位 / 秒）

        auto lastFrameTime = std::chrono::high_resolution_clock::now();

        while (running) {
            // 计算两帧之间的 delta time
            auto currentFrameTime = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameTime - lastFrameTime).count();
            lastFrameTime = currentFrameTime;

            // 限制极端情况下的帧率跳变 (例如调试或卡顿)
            if (dt < 0.0f) dt = 0.0f;
            if (dt > 0.1f) dt = 0.1f;

            float mouseDX = 0.0f;
            float mouseDY = 0.0f;

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    // 仅在锁定鼠标焦点（Relative Mode）下累计视角偏移，允许用户按 ESC 临时释放鼠标
                    if (SDL_GetWindowRelativeMouseMode(window)) {
                        mouseDX += event.motion.xrel;
                        mouseDY += event.motion.yrel;
                    }
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        // 按 ESC 键切回/切出 相对鼠标状态
                        bool relativeMode = SDL_GetWindowRelativeMouseMode(window);
                        SDL_SetWindowRelativeMouseMode(window, !relativeMode);
                    }
                }
            }

            auto tempExtent = presenter.getExtent();
            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            if (static_cast<uint32_t>(w) != tempExtent.width || static_cast<uint32_t>(h) != tempExtent.height) {
                presenter.markResized();
                tempExtent = presenter.getExtent();
                storageImage = StorageImage(device, allocator, tempExtent.width, tempExtent.height, commandPool, graphicsQueue);
                rtPass.updateDescriptorSet(tlas.get(), storageImage.getView(), mesh);
                frameCounter = 0;
            }

            // ─────────────────────────────────────────────────────────────
            // 计算相机方向向量 (Euler 角度结合 Eigen 数学计算)
            // ─────────────────────────────────────────────────────────────
            float yawRad = yaw * 3.14159265f / 180.0f;
            float pitchRad = pitch * 3.14159265f / 180.0f;

            Eigen::Vector3f front;
            front.x() = std::cos(pitchRad) * std::cos(yawRad);
            front.y() = std::sin(pitchRad);
            front.z() = std::cos(pitchRad) * std::sin(yawRad);
            front.normalize();

            Eigen::Vector3f worldUp(0.0f, 1.0f, 0.0f);
            // 解决视线贴近世界天顶时的奇异值退化问题
            if (std::abs(front.dot(worldUp)) > 0.99f) {
                worldUp = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
            }

            Eigen::Vector3f right = front.cross(worldUp).normalized();
            Eigen::Vector3f cameraUpReal = right.cross(front).normalized();

            // ─────────────────────────────────────────────────────────────
            // 处理 WASD 键盘和鼠标 FPS 移动控制
            // ─────────────────────────────────────────────────────────────
            const bool* keyStates = SDL_GetKeyboardState(nullptr);
            Eigen::Vector3f moveDir(0.0f, 0.0f, 0.0f);

            if (keyStates[SDL_SCANCODE_W]) moveDir += front;
            if (keyStates[SDL_SCANCODE_S]) moveDir -= front;
            if (keyStates[SDL_SCANCODE_A]) moveDir -= right;
            if (keyStates[SDL_SCANCODE_D]) moveDir += right;

            // 垂直升降飞行支持 (Space/E 上升，LCTRL/Q 下降)
            if (keyStates[SDL_SCANCODE_SPACE] || keyStates[SDL_SCANCODE_E]) moveDir += cameraUpReal;
            if (keyStates[SDL_SCANCODE_LCTRL] || keyStates[SDL_SCANCODE_Q]) moveDir -= cameraUpReal;

            bool posMoved = false;
            if (moveDir.squaredNorm() > 1e-6f) {
                cameraPos += moveDir.normalized() * moveSpeed * dt;
                posMoved = true;
            }

            bool rotMoved = (std::abs(mouseDX) > 1e-3f || std::abs(mouseDY) > 1e-3f);
            if (rotMoved) {
                yaw += mouseDX * mouseSensitivity;
                pitch -= mouseDY * mouseSensitivity;

                // 限制仰俯角防止视线翻转 (Gimbal Lock)
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
            }

            // ─────────────────────────────────────────────────────────────
            // 时域累积运动状态检测
            // ─────────────────────────────────────────────────────────────
            bool cameraMoved = posMoved || rotMoved;
            if (cameraMoved) {
                frameCounter = 1; // 运动中：重置累积，消除历史残影
            } else {
                frameCounter++;   // 静止时：进行渐进式抗锯齿累积降噪
            }

            // 依据视场计算物理缩放后的视口基底向量
            float aspect = static_cast<float>(tempExtent.width) / static_cast<float>(tempExtent.height);
            float fov = 45.0f * 3.14159265f / 180.0f;
            float halfFovTan = std::tan(fov / 2.0f);

            Eigen::Vector3f rightScaled = right * halfFovTan * aspect;
            Eigen::Vector3f upScaled = cameraUpReal * halfFovTan;

            // ─────────────────────────────────────────────────────────────
            // 传递对准 Slang 参数的物理 UBO
            // ─────────────────────────────────────────────────────────────
            CameraUniforms cameraUBO{};
            cameraUBO.cameraPos[0] = cameraPos.x();
            cameraUBO.cameraPos[1] = cameraPos.y();
            cameraUBO.cameraPos[2] = cameraPos.z();
            cameraUBO.pad0 = 0.0f;

            cameraUBO.cameraFront[0] = front.x();
            cameraUBO.cameraFront[1] = front.y();
            cameraUBO.cameraFront[2] = front.z();
            cameraUBO.pad1 = 0.0f;

            cameraUBO.cameraRight[0] = rightScaled.x();
            cameraUBO.cameraRight[1] = rightScaled.y();
            cameraUBO.cameraRight[2] = rightScaled.z();
            cameraUBO.pad2 = 0.0f;

            cameraUBO.cameraUp[0] = upScaled.x();
            cameraUBO.cameraUp[1] = upScaled.y();
            cameraUBO.cameraUp[2] = upScaled.z();
            cameraUBO.frameCount = frameCounter;

            rtPass.updateCamera(cameraUBO);

            uint32_t imageIndex = 0;
            VkCommandBuffer cmd = presenter.beginFrame(imageIndex);
            if (cmd != VK_NULL_HANDLE) {
                rtPass.cmdTraceRays(cmd, tempExtent.width, tempExtent.height);
                presenter.endFrame(cmd, imageIndex, storageImage.getImage(), tempExtent);
            }
        }

        vkDeviceWaitIdle(device);
        vkDestroyCommandPool(device, commandPool, nullptr);

    } catch (const std::exception& e) {
        std::cerr << "Application Exception: " << e.what() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}