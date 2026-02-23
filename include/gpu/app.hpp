#pragma once
#include <SDL3/SDL.h>
#include <gpu/context.hpp>
#include <gpu/camera.hpp>
#include <gpu/math_utils.hpp>
#include <gpu/gui.hpp>
#include <oneapi/tbb/concurrent_queue.h>
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"
#include <gpu/svg_loader.hpp>
// 💡 引入 ImGui
#include <../third_party/imgui/imgui.h>
#include <../third_party/imgui/backends/imgui_impl_sdl3.h>
#include <../third_party/imgui/backends/imgui_impl_wgpu.h>

namespace gpu {

// 3D 渲染 Shader 代码
inline constexpr const char* INTERNAL_SHADER_CODE = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}
@vertex
fn vs_main(@location(0) p: vec4<i32>) -> VertexOutput {
    var o: VertexOutput;
    // 还原压缩坐标 (-32767~32767 -> -1.0~1.0)
    o.pos = vec4f(f32(p.x)/32767.0, f32(p.y)/32767.0, f32(p.z)/32767.0, 1.0);
    o.color = vec4f(0.0, 1.0, 0.8, 1.0); // 电光青色点
    return o;
}
@fragment
fn fs_main(i: VertexOutput) -> @location(0) vec4f {
    return i.color;
}
)";

class GeoApp {
public:
    gpu::IconTexture iconTestPoint;
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


    std::vector<PointData3D> currentPointsCache;

    uint64_t lastFrameTime = 0;
    float deltaTime = 0.0f;
    uint32_t pointCount = 0;

    bool isGpuResourcesInitialized = false;
    bool isImGuiSdlInitialized = false;
    bool isImGuiWgpuInitialized = false;
    bool is3DMode = false;


    void HandleMenuAction(const char* category, const char* item) {

        printf("[APP ACTION] Category: %s | Item: %s\n", category, item);

        if (strcmp(category, "System") == 0 && strcmp(item, "Exit") == 0) {
            SDL_Event quit_event;
            quit_event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_event);
        }
        else if (strcmp(category, "View") == 0 && strcmp(item, "ResetCamera") == 0) {
            camera.position = {15.0f, 15.0f, 15.0f};
            camera.yaw = -135.0f; camera.pitch = -35.0f;
        }
        // 未来在这里添加具体创建点、线的逻辑调用
    }

    inline bool init() {
#ifndef __EMSCRIPTEN__
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return false;

        window = SDL_CreateWindow("GeoEngine 3D - Editor Mode", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) return false;

        SDL_SetWindowRelativeMouseMode(window, false);

        gpu = new GpuContext();
        if (!gpu->init(window)) return false;

        // 初始化 ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ApplyLightTheme();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // 加载字体
        io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
        ImFont* font = io.Fonts->AddFontFromFileTTF("assets/fonts/NotoSansSC-Regular.ttf", 30.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (!font) io.FontGlobalScale = 1.5f;

        if (ImGui_ImplSDL3_InitForOther(window)) isImGuiSdlInitialized = true;

        rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };
        return true;
    }

    inline void handleEvent(SDL_Event* ev) {
        if (isImGuiSdlInitialized) ImGui_ImplSDL3_ProcessEvent(ev);

        is3DMode = SDL_GetWindowRelativeMouseMode(window);

        // 退出 3D 模式
        if (is3DMode && ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(window, false);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            return;
        }

        ImGuiIO& io = ImGui::GetIO();

        // 点击锁定逻辑：只有当鼠标没点在 UI 上时才锁定 3D
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (!is3DMode && !io.WantCaptureMouse) {
                SDL_SetWindowRelativeMouseMode(window, true);
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            }
        }

        if (io.WantCaptureMouse) return;


        if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && isGpuResourcesInitialized) {
            int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
            if (w > 0 && h > 0) {
                gpu->configureSurface(w, h);
                createAttachments();
            }
        }
        else if (is3DMode && ev->type == SDL_EVENT_MOUSE_MOTION) {
            camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
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

        // 3D 交互控制
        if (is3DMode && !io.WantCaptureKeyboard) {
            const bool* kb = SDL_GetKeyboardState(NULL);
            if (kb[SDL_SCANCODE_W]) camera.processKeyboard(FORWARD, deltaTime);
            if (kb[SDL_SCANCODE_S]) camera.processKeyboard(BACKWARD, deltaTime);
            if (kb[SDL_SCANCODE_A]) camera.processKeyboard(LEFT, deltaTime);
            if (kb[SDL_SCANCODE_D]) camera.processKeyboard(RIGHT, deltaTime);
            if (kb[SDL_SCANCODE_SPACE]) camera.processKeyboard(UP, deltaTime);
            if (kb[SDL_SCANCODE_LSHIFT]) camera.processKeyboard(DOWN, deltaTime);
        }

        // 持续后台计算
        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw > 0 && ph > 0) {
            viewState.screen_width = (double)pw; viewState.screen_height = (double)ph;
            viewState.UpdateMatrices(camera.getViewMatrix(), createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), camera.position);

            // 只要 TBB 算完了，就立刻派发下一个任务
            if (resultsQueue.empty()) {
                ViewState3D safeView = viewState;
                plotExplicit3D(rpnProg, resultsQueue, 0, safeView, true);
            }
        }

        // 非阻塞取点
        std::vector<PointData3D> new_points;
        if (resultsQueue.try_pop(new_points)) {
            pointCount = static_cast<uint32_t>(new_points.size());
            if (pointCount > 0) {
                currentPointsCache = std::move(new_points);
                wgpuQueueWriteBuffer(gpu->queue, vBuf, 0, currentPointsCache.data(), pointCount * sizeof(PointData3D));
                wgpuQueueSubmit(gpu->queue, 0, nullptr);
            }
        }

        // UI 逻辑
        if (isImGuiWgpuInitialized) {
            ImGui_ImplWGPU_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // 调用由 gui.hpp 提供的菜单栏渲染函数
            RenderCadRibbon(
                [this](const char* cat, const char* item) { this->HandleMenuAction(cat, item); },
                is3DMode,
                ImGui::GetIO().Framerate,
                (ImTextureID)iconTestPoint.view // 传递图标指针
            );

            ImGui::Render();
        }
    }

    inline void render() {
        if (!isGpuResourcesInitialized) return;

        // 自动对齐 Surface 大小
        int curW, curH; SDL_GetWindowSizeInPixels(window, &curW, &curH);
        if (curW <= 0 || curH <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
        if (static_cast<uint32_t>(curW) != gpu->lastWidth || static_cast<uint32_t>(curH) != gpu->lastHeight) {
            gpu->configureSurface(curW, curH); createAttachments();
        }

        WGPUSurfaceTexture surfTex; wgpuSurfaceGetCurrentTexture(gpu->surface, &surfTex);
        if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

        WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, nullptr);

        // --- 3D Pass (带MSAA) ---
        WGPURenderPassColorAttachment ca3d = { nullptr, msaaView, WGPU_DEPTH_SLICE_UNDEFINED, backBuffer, WGPULoadOp_Clear, WGPUStoreOp_Store, {0.05, 0.05, 0.06, 1.0} };
        WGPURenderPassDepthStencilAttachment dsa3d = { nullptr, depthView, WGPULoadOp_Clear, WGPUStoreOp_Store, 1.0f, WGPU_FALSE, WGPULoadOp_Undefined, WGPUStoreOp_Undefined, 0, WGPU_FALSE };
        WGPURenderPassDescriptor pass3d = { nullptr, s("MainScene"), 1, &ca3d, &dsa3d };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass3d);
        if (pointCount > 0) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vBuf, 0, pointCount * sizeof(PointData3D));
            wgpuRenderPassEncoderDraw(pass, pointCount, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);

        // --- UI Pass (直接覆盖) ---
        if (isImGuiWgpuInitialized) {
            WGPURenderPassColorAttachment caUi = { nullptr, backBuffer, WGPU_DEPTH_SLICE_UNDEFINED, nullptr, WGPULoadOp_Load, WGPUStoreOp_Store, {0,0,0,0} };
            WGPURenderPassDescriptor passUi = { nullptr, s("OverlayUI"), 1, &caUi, nullptr };
            WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &passUi);
            ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), uiPass);
            wgpuRenderPassEncoderEnd(uiPass);
        }

        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu->queue, 1, &cb);
#ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(gpu->surface);
        SDL_Delay(2);
#endif
        wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(backBuffer); wgpuTextureRelease(surfTex.texture);
    }

    inline void cleanup() {
        gpu::DestroyIconTexture(iconTestPoint);
        if (isImGuiWgpuInitialized) ImGui_ImplWGPU_Shutdown();
        if (isImGuiSdlInitialized) { ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); }
        std::vector<PointData3D> d; while(resultsQueue.try_pop(d));
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);
        if (vBuf) wgpuBufferRelease(vBuf);
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
        if (shaderModule) wgpuShaderModuleRelease(shaderModule);
        delete gpu; if (window) SDL_DestroyWindow(window);
    }

private:
    // 私有初始化函数实现
    void initGpuResources() {
        int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
        gpu->configureSurface(w, h);
        WGPUShaderSourceWGSL wgsl = { {nullptr, WGPUSType_ShaderSourceWGSL}, s(INTERNAL_SHADER_CODE) };
        WGPUShaderModuleDescriptor smDesc = { reinterpret_cast<WGPUChainedStruct*>(&wgsl) };
        shaderModule = wgpuDeviceCreateShaderModule(gpu->device, &smDesc);

        WGPUVertexAttribute attr = { nullptr, WGPUVertexFormat_Sint16x4, 0, 0 };
        WGPUVertexBufferLayout vbLayout = { nullptr, WGPUVertexStepMode_Vertex, 8, 1, &attr };
        WGPUPipelineLayoutDescriptor plDesc = { nullptr, s("PipeLayout") };
        WGPURenderPipelineDescriptor rpDesc = {};
        rpDesc.layout = wgpuDeviceCreatePipelineLayout(gpu->device, &plDesc);
        rpDesc.vertex = { nullptr, shaderModule, s("vs_main"), 0, nullptr, 1, &vbLayout };
        rpDesc.primitive = { nullptr, WGPUPrimitiveTopology_PointList };
        WGPUColorTargetState colorTarget = { nullptr, gpu->surfaceFormat, nullptr, WGPUColorWriteMask_All };
        WGPUFragmentState fragState = { nullptr, shaderModule, s("fs_main"), 0, nullptr, 1, &colorTarget };
        rpDesc.fragment = &fragState;
        WGPUDepthStencilState dsState = { nullptr, WGPUTextureFormat_Depth24Plus, WGPUOptionalBool_True, WGPUCompareFunction_Less };
        rpDesc.depthStencil = &dsState;
        rpDesc.multisample = { nullptr, 4, 0xFFFFFFFF, WGPU_FALSE };
        pipeline = wgpuDeviceCreateRenderPipeline(gpu->device, &rpDesc);

        WGPUBufferDescriptor vBufDesc = { nullptr, s("VBuf"), WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, 1024*1024*16, false };
        vBuf = wgpuDeviceCreateBuffer(gpu->device, &vBufDesc);
        createAttachments();

        ImGui_ImplWGPU_InitInfo init_info = {};
        init_info.Device = gpu->device;
        init_info.NumFramesInFlight = 3;
        init_info.RenderTargetFormat = gpu->surfaceFormat;
        init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
        if (ImGui_ImplWGPU_Init(&init_info)) {
            ImGui_ImplWGPU_CreateDeviceObjects();
            isImGuiWgpuInitialized = true;
        }
        iconTestPoint = gpu::LoadSvgToWebGPU(gpu->device, gpu->queue, "assets/icons/test.svg");
        isGpuResourcesInitialized = true;
    }

    void createAttachments() {
        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) return;
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);
        WGPUTextureDescriptor msaaDesc = { nullptr, s("MSAA_Tex"), WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, {(uint32_t)pw, (uint32_t)ph, 1}, gpu->surfaceFormat, 1, 4 };
        WGPUTexture msaaTex = wgpuDeviceCreateTexture(gpu->device, &msaaDesc);
        msaaView = wgpuTextureCreateView(msaaTex, nullptr);
        wgpuTextureRelease(msaaTex);
        WGPUTextureDescriptor depthDesc = msaaDesc; depthDesc.format = WGPUTextureFormat_Depth24Plus;
        WGPUTexture depthTex = wgpuDeviceCreateTexture(gpu->device, &depthDesc);
        depthView = wgpuTextureCreateView(depthTex, nullptr);
        wgpuTextureRelease(depthTex);
    }
};

} // namespace gpu