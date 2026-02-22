#pragma once
#include <SDL3/SDL.h>
#include <gpu/context.hpp>
#include <gpu/camera.hpp>
#include <gpu/math_utils.hpp>
#include <gpu/gui.hpp>
#include <oneapi/tbb/concurrent_queue.h>
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"

namespace gpu {

// 💡 修复链接错误：使用 inline constexpr 直接定义，不再使用 extern
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
    out.color = vec4f(0.0, 1.0, 0.8, 1.0);
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
    GuiManager* gui = nullptr;

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

        gui = new GuiManager();
        gui->initSdl(window);

        rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };
        return true;
    }

    inline void handleEvent(SDL_Event* ev) {
        if (gui->isSdlReady) ImGui_ImplSDL3_ProcessEvent(ev);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse && (ev->type == SDL_EVENT_MOUSE_MOTION || ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN)) return;

        if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && isGpuResourcesInitialized) {
            int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
            gpu->configureSurface(w, h); createAttachments();
        } else if (ev->type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
            camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
        } else if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(window, false);
        } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (!io.WantCaptureMouse) SDL_SetWindowRelativeMouseMode(window, true);
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
        bool cameraMoved = false;
        if (!io.WantCaptureKeyboard) {
            const bool* kb = SDL_GetKeyboardState(NULL);
            if (kb[SDL_SCANCODE_W]) { camera.processKeyboard(FORWARD, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_S]) { camera.processKeyboard(BACKWARD, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_A]) { camera.processKeyboard(LEFT, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_D]) { camera.processKeyboard(RIGHT, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_SPACE]) { camera.processKeyboard(UP, deltaTime); cameraMoved = true; }
            if (kb[SDL_SCANCODE_LSHIFT]) { camera.processKeyboard(DOWN, deltaTime); cameraMoved = true; }
        }
        if (!io.WantCaptureMouse && SDL_GetWindowRelativeMouseMode(window)) cameraMoved = true;

        static bool first_run = true;
        if (cameraMoved || first_run) {
            int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
            viewState.screen_width = (double)pw; viewState.screen_height = (double)ph;
            viewState.UpdateMatrices(camera.getViewMatrix(), createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), camera.position);
            ViewState3D safeView = viewState;
            if (resultsQueue.empty()) {
                plotExplicit3D(rpnProg, resultsQueue, 0, safeView, true);
                first_run = false;
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
        gui->beginFrame();
        gui->drawTerminal(pointCount, deltaTime * 1000.0f);
    }

    inline void render() const {
        if (!isGpuResourcesInitialized) return;
        WGPUSurfaceTexture surfTex; wgpuSurfaceGetCurrentTexture(gpu->surface, &surfTex);
        if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

        WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, nullptr);

        // 3D Pass
        WGPURenderPassColorAttachment ca3d = {};
        ca3d.view = msaaView;
        ca3d.resolveTarget = backBuffer;
        ca3d.loadOp = WGPULoadOp_Clear;
        ca3d.storeOp = WGPUStoreOp_Store;
        ca3d.clearValue = {0.05, 0.05, 0.05, 1.0};
        ca3d.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDepthStencilAttachment dsa3d = {};
        dsa3d.view = depthView;
        dsa3d.depthLoadOp = WGPULoadOp_Clear;
        dsa3d.depthStoreOp = WGPUStoreOp_Store;
        dsa3d.depthClearValue = 1.0f;
        dsa3d.stencilLoadOp = WGPULoadOp_Undefined;
        dsa3d.stencilStoreOp = WGPUStoreOp_Undefined;

        WGPURenderPassDescriptor pass3d = { nullptr, s("3DPass"), 1, &ca3d, &dsa3d };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass3d);
        if (pointCount > 0) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vBuf, 0, pointCount * sizeof(PointData3D));
            wgpuRenderPassEncoderDraw(pass, pointCount, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);

        // UI Pass
        WGPURenderPassColorAttachment caUi = {};
        caUi.view = backBuffer;
        caUi.loadOp = WGPULoadOp_Load;
        caUi.storeOp = WGPUStoreOp_Store;
        caUi.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        WGPURenderPassDescriptor passUi = { nullptr, s("UIPass"), 1, &caUi, nullptr };
        WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &passUi);
        gui->endFrame(uiPass);
        wgpuRenderPassEncoderEnd(uiPass);

        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu->queue, 1, &cb);
#ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(gpu->surface);
        SDL_Delay(5);
#endif
        wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(backBuffer); wgpuTextureRelease(surfTex.texture);
    }

    inline void cleanup() {
        gui->cleanup();
        std::vector<PointData3D> d; while(resultsQueue.try_pop(d));
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);
        if (vBuf) wgpuBufferRelease(vBuf);
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
        if (shaderModule) wgpuShaderModuleRelease(shaderModule);
        delete gui; delete gpu;
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

        WGPUVertexAttribute attr = { nullptr, WGPUVertexFormat_Sint16x4, 0, 0 };
        WGPUVertexBufferLayout vbLayout = { nullptr, WGPUVertexStepMode_Vertex, 8, 1, &attr };

        WGPUPipelineLayoutDescriptor plDesc = { nullptr, s("PL") };
        WGPURenderPipelineDescriptor rpDesc = {};
        rpDesc.layout = wgpuDeviceCreatePipelineLayout(gpu->device, &plDesc);
        rpDesc.vertex.module = shaderModule;
        rpDesc.vertex.entryPoint = s("vs_main");
        rpDesc.vertex.bufferCount = 1;
        rpDesc.vertex.buffers = &vbLayout;
        rpDesc.primitive.topology = WGPUPrimitiveTopology_PointList;

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
        gui->initWgpu(gpu->device, gpu->surfaceFormat);
        isGpuResourcesInitialized = true;
    }

    inline void createAttachments() {
        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) return;
        if (msaaView) wgpuTextureViewRelease(msaaView);
        if (depthView) wgpuTextureViewRelease(depthView);
        WGPUTextureDescriptor msaaDesc = { nullptr, s("MSAA"), WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, {(uint32_t)pw, (uint32_t)ph, 1}, gpu->surfaceFormat, 1, 4 };
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