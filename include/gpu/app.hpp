#pragma once
#include <SDL3/SDL.h>
#include <gpu/context.hpp>
#include <gpu/camera.hpp>
#include <gpu/math_utils.hpp>
#include <oneapi/tbb/concurrent_queue.h>
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"

namespace gpu {

inline constexpr const char* INTERNAL_SHADER_CODE = R"(
struct VertexOutput { @builtin(position) pos: vec4f, @location(0) color: vec4f, }
@vertex fn vs_main(@location(0) p: vec4<i32>) -> VertexOutput {
    var o: VertexOutput;
    o.pos = vec4f(f32(p.x)/32767.0, f32(p.y)/32767.0, f32(p.z)/32767.0, 1.0);
    o.color = vec4f(0.0, 0.6, 1.0, 1.0); return o;
}
@fragment fn fs_main(i: VertexOutput) -> @location(0) vec4f { return i.color; }
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

    uint64_t lastFrameTime = 0;
    float deltaTime = 0.0f;
    uint32_t pointCount = 0;
    bool isGpuResourcesInitialized = false;

    inline bool init() {
#ifndef __EMSCRIPTEN__
        // 💡 强制使用 X11 驱动，解决 Wayland 黑屏问题
        printf("[APP] Linux: Forcing X11 Video Driver\n");
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return false;

        window = SDL_CreateWindow("GeoEngine 3D Debugger", 800, 500, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) return false;

        SDL_SetWindowRelativeMouseMode(window, true);

        gpu = new GpuContext();
        if (!gpu->init(window)) return false;

        rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };
        return true;
    }

    inline void handleEvent(SDL_Event* ev) {
        if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && isGpuResourcesInitialized) {
            int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
            gpu->configureSurface(w, h); createAttachments();
        } else if (ev->type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(window)) {
            camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
        } else if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            SDL_SetWindowRelativeMouseMode(window, false);
        } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (!SDL_GetWindowRelativeMouseMode(window)) SDL_SetWindowRelativeMouseMode(window, true);
        }
    }

    inline void update() {
        gpu->update();
        if (!gpu->isReady) return;
        if (!isGpuResourcesInitialized) initGpuResources();

        uint64_t now = SDL_GetTicks();
        deltaTime = (float)(now - lastFrameTime) / 1000.0f; lastFrameTime = now;

        const bool* kb = SDL_GetKeyboardState(NULL);
        if (kb[SDL_SCANCODE_W]) camera.processKeyboard(FORWARD, deltaTime);
        if (kb[SDL_SCANCODE_S]) camera.processKeyboard(BACKWARD, deltaTime);
        if (kb[SDL_SCANCODE_A]) camera.processKeyboard(LEFT, deltaTime);
        if (kb[SDL_SCANCODE_D]) camera.processKeyboard(RIGHT, deltaTime);
        if (kb[SDL_SCANCODE_SPACE]) camera.processKeyboard(UP, deltaTime);
        if (kb[SDL_SCANCODE_LSHIFT]) camera.processKeyboard(DOWN, deltaTime);

        int pw, ph; SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) return;
        viewState.screen_width = (double)pw; viewState.screen_height = (double)ph;
        viewState.UpdateMatrices(camera.getViewMatrix(), createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), camera.position);

        plotExplicit3D(rpnProg, resultsQueue, 0, viewState, true);
        std::vector<PointData3D> points;
        if (resultsQueue.try_pop(points)) {
            pointCount = static_cast<uint32_t>(points.size());
            if (pointCount > 0) wgpuQueueWriteBuffer(gpu->queue, vBuf, 0, points.data(), pointCount * 8);
        }
    }

    inline void render()
    {
        if (!isGpuResourcesInitialized) return;
        WGPUSurfaceTexture surfTex;
        wgpuSurfaceGetCurrentTexture(gpu->surface, &surfTex);
        if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surfTex.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

        WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, nullptr);

        WGPURenderPassColorAttachment ca = {
            nullptr, msaaView, WGPU_DEPTH_SLICE_UNDEFINED, backBuffer, WGPULoadOp_Clear, WGPUStoreOp_Store,
            {0.02, 0.02, 0.02, 1.0}
        };
        WGPURenderPassDepthStencilAttachment dsa = {
            nullptr, depthView, WGPULoadOp_Clear, WGPUStoreOp_Store, 1.0f, WGPU_FALSE, WGPULoadOp_Undefined,
            WGPUStoreOp_Undefined, 0, WGPU_FALSE
        };
        WGPURenderPassDescriptor passDesc = {nullptr, s("RP"), 1, &ca, &dsa};

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (pointCount > 0)
        {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vBuf, 0, pointCount * 8);
            wgpuRenderPassEncoderDraw(pass, pointCount, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
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

        WGPUShaderSourceWGSL wgsl = { {nullptr, WGPUSType_ShaderSourceWGSL}, s(INTERNAL_SHADER_CODE) };
        WGPUShaderModuleDescriptor smDesc = { reinterpret_cast<WGPUChainedStruct*>(&wgsl) };
        shaderModule = wgpuDeviceCreateShaderModule(gpu->device, &smDesc);

        WGPUVertexAttribute attr = { nullptr, WGPUVertexFormat_Sint16x4, 0, 0 };
        WGPUVertexBufferLayout vbLayout = { nullptr, WGPUVertexStepMode_Vertex, 8, 1, &attr };
        WGPUPipelineLayoutDescriptor plDesc = { nullptr, s("PL") };

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

        WGPUTextureDescriptor depthDesc = msaaDesc;
        depthDesc.format = WGPUTextureFormat_Depth24Plus;
        WGPUTexture depthTex = wgpuDeviceCreateTexture(gpu->device, &depthDesc);
        depthView = wgpuTextureCreateView(depthTex, nullptr);
        wgpuTextureRelease(depthTex);
    }
};

} // namespace gpu