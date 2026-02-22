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
#include "gui.hpp"  // 确保包含了主题函数

namespace gpu {

inline constexpr const char* INTERNAL_SHADER_CODE = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}
@vertex
fn vs_main(@location(0) p: vec4<i32>) -> VertexOutput {
    var o: VertexOutput;
    let x = f32(p.x)/32767.0;
    let y = f32(p.y)/32767.0;
    let z = f32(p.z)/32767.0;
    o.pos = vec4f(x, y, z, 1.0);
    o.color = vec4f(0.0, 1.0, 0.8, 1.0); // 亮青色
    return o;
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
    std::vector<PointData3D> currentPointsCache;

    uint64_t lastFrameTime = 0;
    float deltaTime = 0.0f;
    uint32_t pointCount = 0;

    bool isGpuResourcesInitialized = false;
    bool showTestWindow = true;
    bool isImGuiSdlInitialized = false;
    bool isImGuiWgpuInitialized = false;

    // 私有函数声明
private:
    inline void initGpuResources();
    inline void createAttachments();

public:
    inline bool init() {
#ifndef __EMSCRIPTEN__
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return false;

        window = SDL_CreateWindow("GeoEngine 3D", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) return false;

        SDL_SetWindowRelativeMouseMode(window, false);

        gpu = new GpuContext();
        if (!gpu->init(window)) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // 💡 恢复主题调用：先 Dark 再 CyberGlass
        ImGui::StyleColorsDark();
        gpu::ApplyCyberGlassTheme();

        io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/NotoSansSC-Regular.ttf",
            20.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
        );
        if (!font) io.FontGlobalScale = 1.5f;

        if (ImGui_ImplSDL3_InitForOther(window)) {
            isImGuiSdlInitialized = true;
        }

        rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };
        return true;
    }

    inline void handleEvent(SDL_Event* ev) {
        if (isImGuiSdlInitialized) ImGui_ImplSDL3_ProcessEvent(ev);

        ImGuiIO& io = ImGui::GetIO();
        bool is3DMode = SDL_GetWindowRelativeMouseMode(window);

        if (!is3DMode && isImGuiSdlInitialized) {
            ImGui_ImplSDL3_ProcessEvent(ev);
        }

        // 1. ESC 退出锁定
        if (is3DMode && ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(window, false);
            return;
        }

        // 2. 点击锁定逻辑 (修复：必须点在非 UI 区域)
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            // 只有当 ImGui 不想要捕获鼠标时，才允许锁定
            if (!is3DMode && !io.WantCaptureMouse) {
                SDL_SetWindowRelativeMouseMode(window, true);
            }
        }

        // 3. 全局阻断：如果 ImGui 正在操作，不再透传给 3D 逻辑
        if (io.WantCaptureMouse) return;

        if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && isGpuResourcesInitialized) {
            int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
            gpu->configureSurface(w, h); createAttachments();
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
        bool is3DMode = SDL_GetWindowRelativeMouseMode(window);
        bool cameraMoved = is3DMode;
        if (is3DMode) {
            // 告诉 ImGui：现在没有鼠标设备
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            // 双重保险：把鼠标逻辑位置移到无限远处
        } else {
            // 恢复鼠标功能
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

        if (is3DMode && !io.WantCaptureKeyboard) {
            const bool* kb = SDL_GetKeyboardState(NULL);
            if (kb[SDL_SCANCODE_W]) { camera.processKeyboard(FORWARD, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_S]) { camera.processKeyboard(BACKWARD, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_A]) { camera.processKeyboard(LEFT, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_D]) { camera.processKeyboard(RIGHT, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_SPACE]) { camera.processKeyboard(UP, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_LSHIFT]) { camera.processKeyboard(DOWN, deltaTime); cameraMoved = true; }
        }

        static bool first_run = true;
        if (cameraMoved || first_run) {
            int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
            if (pw > 0 && ph > 0) {
                viewState.screen_width = (double)pw; viewState.screen_height = (double)ph;
                viewState.UpdateMatrices(camera.getViewMatrix(), createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), camera.position);
                ViewState3D safeView = viewState;
                if (resultsQueue.empty()) {
                    plotExplicit3D(rpnProg, resultsQueue, 0, safeView, true);
                    first_run = false;
                }
            }
        }

        std::vector<PointData3D> new_points;
        if (resultsQueue.try_pop(new_points)) {
            pointCount = static_cast<uint32_t>(new_points.size());
            if (pointCount > 0) {
                currentPointsCache = std::move(new_points);
                wgpuQueueWriteBuffer(gpu->queue, vBuf, 0, currentPointsCache.data(), pointCount * sizeof(PointData3D));
                wgpuQueueSubmit(gpu->queue, 0, nullptr);
            }
        }

        if (isImGuiWgpuInitialized) {
            ImGui_ImplWGPU_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (showTestWindow) {
                ImGui::SetNextWindowSize(ImVec2(400, 240), ImGuiCond_FirstUseEver);

                ImGui::SetNextWindowBgAlpha(0.65f);

                if (ImGui::Begin("GeoEngine 系统终端", &showTestWindow, ImGuiWindowFlags_NoCollapse)) {
                    // 💡 恢复青色文字和律动效果
                    ImGui::TextColored(ImVec4(0, 1, 0.95f, 1), ">> 数学内核: 运行中");
                    ImGui::Separator();
                    ImGui::Text("活跃顶点数: %u", pointCount);

                    float wave = (sin(SDL_GetTicks() * 0.004f) * 0.5f) + 0.5f;
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.5f + wave * 0.5f), "● 渲染步长: %.1f ms", deltaTime * 1000.0f);

                    ImGui::Dummy(ImVec2(0, 10));
                    if (ImGui::Button("重置 3D 摄像机", ImVec2(-1, 42))) {
                        camera.position = {15.0f, 15.0f, 15.0f};
                        first_run = true;
                    }

                    if (is3DMode) ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "状态: 视角锁定中 (按ESC释放)");
                    else ImGui::TextDisabled("状态: 自由鼠标 (点击背景进入3D)");
                }
                ImGui::End();
            }
            ImGui::Render();
        }
    }

    inline void render() {
        if (!isGpuResourcesInitialized) return;
        WGPUSurfaceTexture surfTex;
        wgpuSurfaceGetCurrentTexture(gpu->surface, &surfTex);
        if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

        WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, nullptr);

        WGPURenderPassColorAttachment ca3d = { nullptr, msaaView, WGPU_DEPTH_SLICE_UNDEFINED, backBuffer, WGPULoadOp_Clear, WGPUStoreOp_Store, {0.05, 0.05, 0.05, 1.0} };
        WGPURenderPassDepthStencilAttachment dsa3d = { nullptr, depthView, WGPULoadOp_Clear, WGPUStoreOp_Store, 1.0f, WGPU_FALSE, WGPULoadOp_Undefined, WGPUStoreOp_Undefined, 0, WGPU_FALSE };
        WGPURenderPassDescriptor pass3d = { nullptr, s("3DPass"), 1, &ca3d, &dsa3d };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass3d);
        if (pointCount > 0) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vBuf, 0, pointCount * sizeof(PointData3D));
            wgpuRenderPassEncoderDraw(pass, pointCount, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);

        if (isImGuiWgpuInitialized && showTestWindow) {
            WGPURenderPassColorAttachment caUi = { nullptr, backBuffer, WGPU_DEPTH_SLICE_UNDEFINED, nullptr, WGPULoadOp_Load, WGPUStoreOp_Store, {0,0,0,0} };
            WGPURenderPassDescriptor passUi = { nullptr, s("UIPass"), 1, &caUi, nullptr };
            WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &passUi);
            ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), uiPass);
            wgpuRenderPassEncoderEnd(uiPass);
        }

        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu->queue, 1, &cb);
#ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(gpu->surface);
        SDL_Delay(3);
#endif
        wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(backBuffer); wgpuTextureRelease(surfTex.texture);
    }

    inline void cleanup() {
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
};

// 💡 私有函数实现
inline void GeoApp::initGpuResources() {
    int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
    gpu->configureSurface(w, h);
    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = s(INTERNAL_SHADER_CODE);
    WGPUShaderModuleDescriptor smDesc = {};
    smDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
    shaderModule = wgpuDeviceCreateShaderModule(gpu->device, &smDesc);
    WGPUVertexAttribute attr = {};
    attr.format = WGPUVertexFormat_Sint16x4;
    attr.offset = 0;
    attr.shaderLocation = 0;
    WGPUVertexBufferLayout vbLayout = {};
    vbLayout.stepMode = WGPUVertexStepMode_Vertex;
    vbLayout.arrayStride = 8;
    vbLayout.attributeCount = 1;
    vbLayout.attributes = &attr;
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.label = s("PL");
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
    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = gpu->device;
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = gpu->surfaceFormat;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    if (ImGui_ImplWGPU_Init(&init_info)) {
        isImGuiWgpuInitialized = true;
    }
    isGpuResourcesInitialized = true;
}

inline void GeoApp::createAttachments() {
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
    depthDesc.sampleCount = 4;
    WGPUTexture depthTex = wgpuDeviceCreateTexture(gpu->device, &depthDesc);
    depthView = wgpuTextureCreateView(depthTex, nullptr);
    wgpuTextureRelease(depthTex);
}

} // namespace gpu