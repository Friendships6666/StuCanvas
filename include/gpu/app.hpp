#pragma once
#include <SDL3/SDL.h>
#include <gpu/context.hpp>
#include <gpu/camera.hpp>
#include <gpu/math_utils.hpp>
#include <oneapi/tbb/concurrent_queue.h>
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"

// 💡 引入 ImGui
#include <../third_party/imgui/imgui.h>
#include <../third_party/imgui/backends/imgui_impl_sdl3.h>
#include <../third_party/imgui/backends/imgui_impl_wgpu.h>

namespace gpu {

inline constexpr const char* INTERNAL_SHADER_CODE = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}
@vertex
fn vs_main(@location(0) pos_compressed: vec4<i32>) -> VertexOutput {
    var out: VertexOutput;
    let x = f32(pos_compressed.x) / 32767.0;
    let y = f32(pos_compressed.y) / 32767.0;
    let z = f32(pos_compressed.z) / 32767.0;
    out.pos = vec4f(x, y, z, 1.0);
    out.color = vec4f(0.0, 1.0, 0.8, 1.0); // 亮青色
    return out;
}
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)";

class GeoApp {
public:
    SDL_Window* window = nullptr;
    GpuContext* gpu = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer vBuf = nullptr;
    WGPUTextureView msaaView = nullptr;
    WGPUTextureView depthView = nullptr;
    WGPUShaderModule shaderModule = nullptr;

    Camera camera{Eigen::Vector3f(15.0f, 15.0f, 15.0f)};
    ViewState3D viewState;
    AlignedVector<RPNToken> rpnProg;
    oneapi::tbb::concurrent_bounded_queue<std::vector<PointData3D>> resultsQueue;

    // 💡 显存缓存保护：确保异步写入时指针存活
    std::vector<PointData3D> currentPointsCache;

    uint64_t lastFrameTime = 0;
    float deltaTime = 0.0f;
    uint32_t pointCount = 0;

    bool isGpuResourcesInitialized = false;
    bool showTestWindow = true;
    bool isImGuiSdlInitialized = false;
    bool isImGuiWgpuInitialized = false;

    inline bool init() {
#ifndef __EMSCRIPTEN__
        // 强制 X11，规避 Linux Wayland 兼容性问题
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return false;

        window = SDL_CreateWindow("GeoEngine 3D", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) return false;

        // 默认不锁死鼠标，方便用户点击 UI
        SDL_SetWindowRelativeMouseMode(window, false);

        gpu = new GpuContext();
        if (!gpu->init(window)) return false;

        // 初始化 ImGui 上下文
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = 1.5f; // 高分屏字体放大
        ImGui::StyleColorsDark();

        if (ImGui_ImplSDL3_InitForOther(window)) {
            isImGuiSdlInitialized = true;
        }

        rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };
        return true;
    }

    inline void handleEvent(SDL_Event* ev) {
        if (isImGuiSdlInitialized) ImGui_ImplSDL3_ProcessEvent(ev);

        ImGuiIO& io = ImGui::GetIO();
        // 如果鼠标在 UI 上，屏蔽 3D 场景的点击和拖拽
        if (io.WantCaptureMouse && (ev->type == SDL_EVENT_MOUSE_MOTION || ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN)) return;

        if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && isGpuResourcesInitialized) {
            int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
            gpu->configureSurface(w, h); createAttachments();
        } else if (ev->type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
            camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
        } else if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(window, false); // ESC 解锁鼠标
        } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (!io.WantCaptureMouse) SDL_SetWindowRelativeMouseMode(window, true); // 点击背景锁定鼠标
        }
    }

    inline void update() {
        gpu->update();
        if (!gpu->isReady) return;
        if (!isGpuResourcesInitialized) initGpuResources();

        uint64_t now = SDL_GetTicks();
        deltaTime = (float)(now - lastFrameTime) / 1000.0f;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        lastFrameTime = now;

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard) {
            const bool* kb = SDL_GetKeyboardState(NULL);
            if (kb[SDL_SCANCODE_W]) camera.processKeyboard(FORWARD, deltaTime);
            if (kb[SDL_SCANCODE_S]) camera.processKeyboard(BACKWARD, deltaTime);
            if (kb[SDL_SCANCODE_A]) camera.processKeyboard(LEFT, deltaTime);
            if (kb[SDL_SCANCODE_D]) camera.processKeyboard(RIGHT, deltaTime);
            if (kb[SDL_SCANCODE_SPACE]) camera.processKeyboard(UP, deltaTime);
            if (kb[SDL_SCANCODE_LSHIFT]) camera.processKeyboard(DOWN, deltaTime);
        }

        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) return;
        viewState.screen_width = (double)pw; viewState.screen_height = (double)ph;
        viewState.UpdateMatrices(camera.getViewMatrix(), createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), camera.position);

        // 拷贝 ViewState 防并发竞争
        ViewState3D threadSafeViewState = viewState;
        plotExplicit3D(rpnProg, resultsQueue, 0, viewState, true);
        std::vector<PointData3D> points;
        if (resultsQueue.try_pop(points)) {
            pointCount = static_cast<uint32_t>(points.size());
            if (pointCount > 0) wgpuQueueWriteBuffer(gpu->queue, vBuf, 0, points.data(), pointCount * 8);
        }

        // 构建 UI 界面
        if (isImGuiWgpuInitialized) {
            ImGui_ImplWGPU_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (showTestWindow) {
                ImGui::SetNextWindowSize(ImVec2(350, 180), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("GeoEngine 系统终端", &showTestWindow)) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "系统状态: 稳定运行");
                    ImGui::Text("活跃顶点数: %u", pointCount);
                    ImGui::Separator();
                    ImGui::BulletText("点击深色背景: 锁定相机");
                    ImGui::BulletText("按 ESC 键: 释放鼠标");
                    if (ImGui::Button("隐藏终端", ImVec2(-1, 35))) {
                        showTestWindow = false;
                    }
                }
                ImGui::End();
            }
            ImGui::Render();
        }
    }

    inline void render()
    {
        if (!isGpuResourcesInitialized) return;
        WGPUSurfaceTexture surfTex;
        wgpuSurfaceGetCurrentTexture(gpu->surface, &surfTex);

        if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

        WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, nullptr);

        // ==========================================
        // 阶段 1：3D 渲染 Pass (带 4x MSAA 和深度测试)
        // ==========================================
        WGPURenderPassColorAttachment mainColorAttachment = {};
        mainColorAttachment.view = msaaView;
        mainColorAttachment.resolveTarget = backBuffer;
        mainColorAttachment.loadOp = WGPULoadOp_Clear;
        mainColorAttachment.storeOp = WGPUStoreOp_Store;
        mainColorAttachment.clearValue = {0.05, 0.05, 0.05, 1.0}; // 深灰色背景
        mainColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDepthStencilAttachment mainDepthAttachment = {};
        mainDepthAttachment.view = depthView;
        mainDepthAttachment.depthLoadOp = WGPULoadOp_Clear;
        mainDepthAttachment.depthStoreOp = WGPUStoreOp_Store;
        mainDepthAttachment.depthClearValue = 1.0f;
        mainDepthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
        mainDepthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

        WGPURenderPassDescriptor mainPassDesc = {};
        mainPassDesc.colorAttachmentCount = 1;
        mainPassDesc.colorAttachments = &mainColorAttachment;
        mainPassDesc.depthStencilAttachment = &mainDepthAttachment;

        WGPURenderPassEncoder mainPass = wgpuCommandEncoderBeginRenderPass(encoder, &mainPassDesc);
        if (pointCount > 0) {
            wgpuRenderPassEncoderSetPipeline(mainPass, pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(mainPass, 0, vBuf, 0, pointCount * sizeof(PointData3D));
            wgpuRenderPassEncoderDraw(mainPass, pointCount, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(mainPass);

        // ==========================================
        // 阶段 2：UI 渲染 Pass (无 MSAA，无深度，覆盖在最上层)
        // ==========================================
        if (isImGuiWgpuInitialized) {
            WGPURenderPassColorAttachment uiColorAttachment = {};
            uiColorAttachment.view = backBuffer;            // 💡 直接画在屏幕缓冲区
            uiColorAttachment.resolveTarget = nullptr;      // 不需要 Resolve
            uiColorAttachment.loadOp = WGPULoadOp_Load;     // 💡 必须是 Load，否则 3D 背景会被清空
            uiColorAttachment.storeOp = WGPUStoreOp_Store;
            uiColorAttachment.clearValue = {0.0, 0.0, 0.0, 0.0};
            uiColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

            WGPURenderPassDescriptor uiPassDesc = {};
            uiPassDesc.colorAttachmentCount = 1;
            uiPassDesc.colorAttachments = &uiColorAttachment;
            uiPassDesc.depthStencilAttachment = nullptr;    // 💡 UI 不需要深度附件

            WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &uiPassDesc);
            ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), uiPass);
            wgpuRenderPassEncoderEnd(uiPass);
        }

        // ==========================================
        // 提交与收尾
        // ==========================================
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu->queue, 1, &cb);

#ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(gpu->surface);
#endif

        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(backBuffer);
        wgpuTextureRelease(surfTex.texture);
    }

    inline void cleanup() {
        if (isImGuiWgpuInitialized) ImGui_ImplWGPU_Shutdown();
        if (isImGuiSdlInitialized) ImGui_ImplSDL3_Shutdown();
        if (isImGuiSdlInitialized) ImGui::DestroyContext();

        std::vector<PointData3D> d; while(resultsQueue.try_pop(d));
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);
        if (vBuf) wgpuBufferRelease(vBuf);
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
        if (shaderModule) wgpuShaderModuleRelease(shaderModule);
        delete gpu;
        if (window) SDL_DestroyWindow(window);
    }

private:
    inline void initGpuResources() {
        int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
        gpu->configureSurface(w, h);

        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = s(INTERNAL_SHADER_CODE);
        WGPUShaderModuleDescriptor smDesc = { reinterpret_cast<WGPUChainedStruct*>(&wgsl) };
        shaderModule = wgpuDeviceCreateShaderModule(gpu->device, &smDesc);

        WGPUVertexAttribute attr = {};
        attr.nextInChain = nullptr;
        attr.format = WGPUVertexFormat_Sint16x4;
        attr.offset = 0;
        attr.shaderLocation = 0;

        WGPUVertexBufferLayout vbLayout = {};
        vbLayout.nextInChain = nullptr;
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.arrayStride = 8;
        vbLayout.attributeCount = 1;
        vbLayout.attributes = &attr;

        WGPUPipelineLayoutDescriptor plDesc = { nullptr, s("PL") };
        WGPURenderPipelineDescriptor rpDesc = {};
        rpDesc.layout = wgpuDeviceCreatePipelineLayout(gpu->device, &plDesc);
        rpDesc.vertex.module = shaderModule;
        rpDesc.vertex.entryPoint = s("vs_main");
        rpDesc.vertex.bufferCount = 1;
        rpDesc.vertex.buffers = &vbLayout;
        rpDesc.primitive.topology = WGPUPrimitiveTopology_PointList;

        WGPUColorTargetState colorTarget = {};
        colorTarget.format = gpu->surfaceFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragState = {};
        fragState.module = shaderModule;
        fragState.entryPoint = s("fs_main");
        fragState.targetCount = 1;
        fragState.targets = &colorTarget;
        rpDesc.fragment = &fragState;

        WGPUDepthStencilState dsState = {};
        dsState.format = WGPUTextureFormat_Depth24Plus;
        dsState.depthWriteEnabled = WGPUOptionalBool_True;
        dsState.depthCompare = WGPUCompareFunction_Less;
        rpDesc.depthStencil = &dsState;

        rpDesc.multisample.count = 4;
        rpDesc.multisample.mask = 0xFFFFFFFF;

        pipeline = wgpuDeviceCreateRenderPipeline(gpu->device, &rpDesc);

        WGPUBufferDescriptor vBufDesc = {};
        vBufDesc.label = s("VBuf");
        vBufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vBufDesc.size = 1024 * 1024 * 16;
        vBuf = wgpuDeviceCreateBuffer(gpu->device, &vBufDesc);

        createAttachments();

        // 💡 修复：ImGui 只需要知道最终要画在什么格式的 Surface 上
        // 绝对不能传 DepthFormat，因为我们的 UI Pass 根本没有挂载 Depth
        ImGui_ImplWGPU_InitInfo init_info = {};
        init_info.Device = gpu->device;
        init_info.NumFramesInFlight = 3;
        init_info.RenderTargetFormat = gpu->surfaceFormat;
        init_info.DepthStencilFormat = WGPUTextureFormat_Undefined; // 🚨 核心修复

        if (ImGui_ImplWGPU_Init(&init_info)) {
            ImGui_ImplWGPU_CreateDeviceObjects();
            isImGuiWgpuInitialized = true;
        }

        isGpuResourcesInitialized = true;
    }

    inline void createAttachments() {
        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) return;
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);

        WGPUTextureDescriptor msaaDesc = {};
        msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
        msaaDesc.dimension = WGPUTextureDimension_2D;
        msaaDesc.size = {(uint32_t)pw, (uint32_t)ph, 1};
        msaaDesc.format = gpu->surfaceFormat;
        msaaDesc.mipLevelCount = 1;
        msaaDesc.sampleCount = 4;
        WGPUTexture msaaTex = wgpuDeviceCreateTexture(gpu->device, &msaaDesc);
        msaaView = wgpuTextureCreateView(msaaTex, nullptr);
        wgpuTextureRelease(msaaTex);

        WGPUTextureDescriptor depthDesc = msaaDesc;
        depthDesc.format = WGPUTextureFormat_Depth24Plus;
        WGPUTexture depthTex = wgpuDeviceCreateTexture(gpu->device, &depthDesc);
        depthView = wgpuTextureCreateView(depthTex, nullptr);
        wgpuTextureRelease(depthTex);
    }
};

} // namespace gpu