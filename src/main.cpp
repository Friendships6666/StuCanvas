#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <vector>
#include <Eigen/Dense>
#include <oneapi/tbb/concurrent_queue.h>
#include <gpu/context.hpp>
#include <gpu/camera.hpp>
#include <gpu/math_utils.hpp>
#include <gpu/geometry.hpp>


#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"

using namespace gpu;

// WGSL 顶点/片段着色器
const char* SHADER_CODE = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}
@vertex
fn vs_main(@location(0) pos_compressed: vec4<i32>) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4f(f32(pos_compressed.x)/32767.0, f32(pos_compressed.y)/32767.0, f32(pos_compressed.z)/32767.0, 1.0);
    out.color = vec4f(0.0, 0.6, 1.0, 1.0);
    return out;
}
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)";

struct AppState {
    SDL_Window* window = nullptr;
    gpu::GpuContext* gpu = nullptr;
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
};

void CreateRenderAttachments(AppState* as) {
    int pw, ph;
    SDL_GetWindowSizeInPixels(as->window, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    printf("[DEBUG] Creating Attachments: %dx%d\n", pw, ph);

    if (as->msaaView) wgpuTextureViewRelease(as->msaaView);
    if (as->depthView) wgpuTextureViewRelease(as->depthView);

    WGPUTextureDescriptor msaaDesc = {};
    msaaDesc.size = {(uint32_t)pw, (uint32_t)ph, 1};
    msaaDesc.sampleCount = 4;
    msaaDesc.format = as->gpu->surfaceFormat;
    msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
    msaaDesc.dimension = WGPUTextureDimension_2D;
    msaaDesc.mipLevelCount = 1;

    WGPUTexture msaaTex = wgpuDeviceCreateTexture(as->gpu->device, &msaaDesc);
    as->msaaView = wgpuTextureCreateView(msaaTex, nullptr);
    wgpuTextureRelease(msaaTex);

    WGPUTextureDescriptor depthDesc = msaaDesc;
    depthDesc.format = WGPUTextureFormat_Depth24Plus;
    WGPUTexture depthTex = wgpuDeviceCreateTexture(as->gpu->device, &depthDesc);
    as->depthView = wgpuTextureCreateView(depthTex, nullptr);
    wgpuTextureRelease(depthTex);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
#ifndef __EMSCRIPTEN__
    // 桌面端保持 X11 兼容性提示，但 Context.hpp 内部会自动处理 Wayland
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
#endif
    printf("[DEBUG] SDL_AppInit Starting...\n");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return SDL_APP_FAILURE;

    auto* as = new AppState();
    *appstate = as;

    as->window = SDL_CreateWindow("GeoEngine 3D Debug", 800, 500, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!as->window) return SDL_APP_FAILURE;

    // 默认开启鼠标锁定
    SDL_SetWindowRelativeMouseMode(as->window, true);

    as->gpu = new gpu::GpuContext();
    if (!as->gpu->init(as->window)) return SDL_APP_FAILURE;

    as->rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* ev) {
    auto* as = static_cast<AppState*>(appstate);
    switch (ev->type) {
        case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (as->isGpuResourcesInitialized) {
                as->gpu->configureSurface(as->window);
                CreateRenderAttachments(as);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (SDL_GetWindowRelativeMouseMode(as->window))
                as->camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev->key.key == SDLK_ESCAPE) SDL_SetWindowRelativeMouseMode(as->window, false);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!SDL_GetWindowRelativeMouseMode(as->window)) SDL_SetWindowRelativeMouseMode(as->window, true);
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* as = static_cast<AppState*>(appstate);

    // 驱动事件流
    as->gpu->update();

    if (!as->gpu->isReady) return SDL_APP_CONTINUE;

    if (!as->isGpuResourcesInitialized) {
        printf("[DEBUG] Step: Initializing GPU Resources\n");
        as->gpu->configureSurface(as->window);

        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = gpu::s(SHADER_CODE);
        WGPUShaderModuleDescriptor smDesc = {};
        smDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
        as->shaderModule = wgpuDeviceCreateShaderModule(as->gpu->device, &smDesc);

        // 1. 修正 VertexAttribute 字段顺序
        WGPUVertexAttribute attr = {};
        attr.nextInChain = nullptr;
        attr.format = WGPUVertexFormat_Sint16x4;
        attr.offset = 0;
        attr.shaderLocation = 0;

        // 2. 修正 VertexBufferLayout 字段顺序
        WGPUVertexBufferLayout vbLayout = {};
        vbLayout.nextInChain = nullptr;
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.arrayStride = 8;
        vbLayout.attributeCount = 1;
        vbLayout.attributes = &attr;

        WGPUPipelineLayoutDescriptor plDesc = {};
        plDesc.label = gpu::s("PL");

        WGPURenderPipelineDescriptor rpDesc = {};
        rpDesc.layout = wgpuDeviceCreatePipelineLayout(as->gpu->device, &plDesc);
        rpDesc.vertex.module = as->shaderModule;
        rpDesc.vertex.entryPoint = gpu::s("vs_main");
        rpDesc.vertex.bufferCount = 1;
        rpDesc.vertex.buffers = &vbLayout;
        rpDesc.primitive.topology = WGPUPrimitiveTopology_PointList;

        WGPUColorTargetState colorTarget = {nullptr, as->gpu->surfaceFormat, nullptr, WGPUColorWriteMask_All};
        WGPUFragmentState fragState = {nullptr, as->shaderModule, gpu::s("fs_main"), 0, nullptr, 1, &colorTarget};
        rpDesc.fragment = &fragState;

        WGPUDepthStencilState dsState = {};
        dsState.format = WGPUTextureFormat_Depth24Plus;
        dsState.depthWriteEnabled = WGPUOptionalBool_True;
        dsState.depthCompare = WGPUCompareFunction_Less;
        rpDesc.depthStencil = &dsState;
        rpDesc.multisample.count = 4;
        rpDesc.multisample.mask = 0xFFFFFFFF;

        as->pipeline = wgpuDeviceCreateRenderPipeline(as->gpu->device, &rpDesc);

        WGPUBufferDescriptor vBufDesc = {};
        vBufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vBufDesc.size = 1024 * 1024 * 16;
        as->vBuf = wgpuDeviceCreateBuffer(as->gpu->device, &vBufDesc);

        CreateRenderAttachments(as);
        as->isGpuResourcesInitialized = true;
        as->lastFrameTime = SDL_GetTicks();
        printf("[DEBUG] Step: Rendering Ready\n");
    }

    uint64_t now = SDL_GetTicks();
    as->deltaTime = (float)(now - as->lastFrameTime) / 1000.0f;
    as->lastFrameTime = now;

    // 交互输入：补全 Space(UP) 和 Shift(DOWN)
    const bool* kb = SDL_GetKeyboardState(NULL);
    if (kb[SDL_SCANCODE_W]) as->camera.processKeyboard(gpu::FORWARD, as->deltaTime);
    if (kb[SDL_SCANCODE_S]) as->camera.processKeyboard(gpu::BACKWARD, as->deltaTime);
    if (kb[SDL_SCANCODE_A]) as->camera.processKeyboard(gpu::LEFT, as->deltaTime);
    if (kb[SDL_SCANCODE_D]) as->camera.processKeyboard(gpu::RIGHT, as->deltaTime);
    if (kb[SDL_SCANCODE_SPACE]) as->camera.processKeyboard(gpu::UP, as->deltaTime);
    if (kb[SDL_SCANCODE_LSHIFT]) as->camera.processKeyboard(gpu::DOWN, as->deltaTime);

    int pw, ph;
    SDL_GetWindowSizeInPixels(as->window, &pw, &ph);
    if (pw <= 0 || ph <= 0) return SDL_APP_CONTINUE;

    as->viewState.screen_width = (double)pw;
    as->viewState.screen_height = (double)ph;
    as->viewState.UpdateMatrices(as->camera.getViewMatrix(), gpu::createPerspective(45.0f * 3.14159f / 180.0f, (float)pw/ph, 0.1f, 1000.0f), as->camera.position);

    // TBB 计算
    plotExplicit3D(as->rpnProg, as->resultsQueue, 0, as->viewState, true);
    std::vector<PointData3D> points;
    if (as->resultsQueue.try_pop(points)) {
        as->pointCount = (uint32_t)points.size();
        if (as->pointCount > 0) wgpuQueueWriteBuffer(as->gpu->queue, as->vBuf, 0, points.data(), as->pointCount * sizeof(PointData3D));
    }

    // 渲染提交
    WGPUSurfaceTexture surfTex;
    wgpuSurfaceGetCurrentTexture(as->gpu->surface, &surfTex);
    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return SDL_APP_CONTINUE;

    WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(as->gpu->device, nullptr);

    WGPURenderPassColorAttachment ca = {nullptr, as->msaaView, WGPU_DEPTH_SLICE_UNDEFINED, backBuffer, WGPULoadOp_Clear, WGPUStoreOp_Store, {0.02, 0.02, 0.02, 1.0}};
    WGPURenderPassDepthStencilAttachment dsa = {nullptr, as->depthView, WGPULoadOp_Clear, WGPUStoreOp_Store, 1.0f, WGPU_FALSE, WGPULoadOp_Undefined, WGPUStoreOp_Undefined, 0, WGPU_FALSE};
    WGPURenderPassDescriptor passDesc = {nullptr, gpu::s("RP"), 1, &ca, &dsa};

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (as->pointCount > 0) {
        wgpuRenderPassEncoderSetPipeline(pass, as->pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, as->vBuf, 0, as->pointCount * sizeof(PointData3D));
        wgpuRenderPassEncoderDraw(pass, as->pointCount, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(as->gpu->queue, 1, &cb);

    // 💡 仅在桌面端调用 Present，防止 WASM 报错
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(as->gpu->surface);
#endif

    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(backBuffer);
    wgpuTextureRelease(surfTex.texture);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* as = static_cast<AppState*>(appstate);
    if (as) {
        std::vector<PointData3D> d; while(as->resultsQueue.try_pop(d));
        if (as->msaaView) wgpuTextureViewRelease(as->msaaView);
        if (as->depthView) wgpuTextureViewRelease(as->depthView);
        if (as->vBuf) wgpuBufferRelease(as->vBuf);
        if (as->pipeline) wgpuRenderPipelineRelease(as->pipeline);
        if (as->shaderModule) wgpuShaderModuleRelease(as->shaderModule);
        delete as->gpu;
        SDL_DestroyWindow(as->window);
        delete as;
    }
    SDL_Quit();
}