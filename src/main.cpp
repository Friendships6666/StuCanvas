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
#include <gpu/utils.hpp>
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotExplicit3D.hpp"

using namespace gpu;

const char* SHADER_CODE = R"(
struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}

@vertex
fn vs_main(@location(0) pos_compressed: vec4<i32>) -> VertexOutput {
    var out: VertexOutput;

    // 解压坐标
    let x = f32(pos_compressed.x) / 32767.0;
    let y = f32(pos_compressed.y) / 32767.0;
    let z = f32(pos_compressed.z) / 32767.0;

    out.pos = vec4f(x, y, z, 1.0);

    // ==========================================
    // 修改为固定颜色 (R, G, B, A) 取值范围 0.0 ~ 1.0
    // 例如：vec4f(0.0, 0.6, 1.0, 1.0) 是一种明亮的浅蓝色
    // ==========================================
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
};

void CreateRenderAttachments(AppState* as) {
    int pw, ph;
    SDL_GetWindowSizeInPixels(as->window, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    if (as->msaaView) wgpuTextureViewRelease(as->msaaView);
    if (as->depthView) wgpuTextureViewRelease(as->depthView);

    WGPUTextureDescriptor msaaDesc = { .size = {(uint32_t)pw, (uint32_t)ph, 1}, .format = as->gpu->surfaceFormat, .usage = WGPUTextureUsage_RenderAttachment, .sampleCount = 4, .mipLevelCount = 1, .dimension = WGPUTextureDimension_2D };
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
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return SDL_APP_FAILURE;

    auto* as = new AppState();
    *appstate = as;

    as->window = SDL_CreateWindow("GeoEngine 3D", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!as->window) return SDL_APP_FAILURE;

    // 初始捕捉鼠标
    SDL_SetWindowRelativeMouseMode(as->window, true);

    as->gpu = new gpu::GpuContext();
    if (!as->gpu->init(as->window)) return SDL_APP_FAILURE;
    as->gpu->configureSurface(as->window);

    as->rpnProg = { {RPNTokenType::PUSH_X}, {RPNTokenType::PUSH_X}, {RPNTokenType::MUL}, {RPNTokenType::PUSH_Y}, {RPNTokenType::PUSH_Y}, {RPNTokenType::MUL}, {RPNTokenType::ADD}, {RPNTokenType::SQRT}, {RPNTokenType::SIN}, {RPNTokenType::STOP} };

    WGPUShaderSourceWGSL wgsl = { .chain = {.sType = WGPUSType_ShaderSourceWGSL}, .code = {SHADER_CODE, WGPU_STRLEN} };
    WGPUShaderModuleDescriptor smDesc = { .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl) };
    as->shaderModule = wgpuDeviceCreateShaderModule(as->gpu->device, &smDesc);

    WGPUVertexAttribute attr = { .format = WGPUVertexFormat_Sint16x4, .offset = 0, .shaderLocation = 0 };
    WGPUVertexBufferLayout vbLayout = { .arrayStride = 8, .stepMode = WGPUVertexStepMode_Vertex, .attributeCount = 1, .attributes = &attr };
    WGPURenderPipelineDescriptor rpDesc = {};
    WGPUPipelineLayoutDescriptor plDesc = { .label = {"PL", WGPU_STRLEN} };
    rpDesc.layout = wgpuDeviceCreatePipelineLayout(as->gpu->device, &plDesc);
    rpDesc.vertex = { .module = as->shaderModule, .entryPoint = {"vs_main", WGPU_STRLEN}, .bufferCount = 1, .buffers = &vbLayout };
    rpDesc.primitive = { .topology = WGPUPrimitiveTopology_PointList };
    WGPUColorTargetState colorTarget = { .format = as->gpu->surfaceFormat, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragState = { .module = as->shaderModule, .entryPoint = {"fs_main", WGPU_STRLEN}, .targetCount = 1, .targets = &colorTarget };
    rpDesc.fragment = &fragState;
    WGPUDepthStencilState dsState = { .format = WGPUTextureFormat_Depth24Plus, .depthWriteEnabled = WGPUOptionalBool_True, .depthCompare = WGPUCompareFunction_Less };
    rpDesc.depthStencil = &dsState;
    rpDesc.multisample = { .count = 4, .mask = ~0u };
    as->pipeline = wgpuDeviceCreateRenderPipeline(as->gpu->device, &rpDesc);

    WGPUBufferDescriptor vBufDesc = { .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, .size = 1024 * 1024 * 16 };
    as->vBuf = wgpuDeviceCreateBuffer(as->gpu->device, &vBufDesc);

    CreateRenderAttachments(as);
    as->lastFrameTime = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* ev) {
    auto* as = static_cast<AppState*>(appstate);
    switch (ev->type) {
        case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: SDL_SetWindowRelativeMouseMode(as->window, true); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: SDL_SetWindowRelativeMouseMode(as->window, false); break;
        case SDL_EVENT_MOUSE_MOTION:
            if (SDL_GetWindowRelativeMouseMode(as->window))
                as->camera.processMouseMovement(ev->motion.xrel, -ev->motion.yrel);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            as->gpu->configureSurface(as->window);
            CreateRenderAttachments(as);
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
    uint64_t now = SDL_GetTicks();
    as->deltaTime = (float)(now - as->lastFrameTime) / 1000.0f;
    as->lastFrameTime = now;

    const bool* kb = SDL_GetKeyboardState(NULL);
    if (kb[SDL_SCANCODE_W]) as->camera.processKeyboard(FORWARD, as->deltaTime);
    if (kb[SDL_SCANCODE_S]) as->camera.processKeyboard(BACKWARD, as->deltaTime);
    if (kb[SDL_SCANCODE_A]) as->camera.processKeyboard(LEFT, as->deltaTime);
    if (kb[SDL_SCANCODE_D]) as->camera.processKeyboard(RIGHT, as->deltaTime);
    if (kb[SDL_SCANCODE_SPACE]) as->camera.processKeyboard(UP, as->deltaTime);
    if (kb[SDL_SCANCODE_LSHIFT]) as->camera.processKeyboard(DOWN, as->deltaTime);

    int pw, ph;
    SDL_GetWindowSizeInPixels(as->window, &pw, &ph);
    if (pw <= 0 || ph <= 0) return SDL_APP_CONTINUE;
    as->viewState.screen_width = (double)pw; as->viewState.screen_height = (double)ph;
    as->viewState.UpdateMatrices(as->camera.getViewMatrix(), gpu::createPerspective(45.0f * M_PI / 180.0f, (float)pw/ph, 0.1f, 1000.0f), as->camera.position);

    plotExplicit3D(as->rpnProg, as->resultsQueue, 0, as->viewState, true);
    std::vector<PointData3D> points;
    if (as->resultsQueue.try_pop(points)) {
        as->pointCount = (uint32_t)points.size();
        if (as->pointCount > 0) wgpuQueueWriteBuffer(as->gpu->queue, as->vBuf, 0, points.data(), as->pointCount * 8);
    }

    WGPUSurfaceTexture surfTex;
    wgpuSurfaceGetCurrentTexture(as->gpu->surface, &surfTex);
    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) return SDL_APP_CONTINUE;
    WGPUTextureView backBuffer = wgpuTextureCreateView(surfTex.texture, nullptr);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(as->gpu->device, nullptr);

    WGPURenderPassColorAttachment ca = { .view = as->msaaView, .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED, .resolveTarget = backBuffer, .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store, .clearValue = {0,0,0,1} };
    WGPURenderPassDepthStencilAttachment dsa = { .view = as->depthView, .depthLoadOp = WGPULoadOp_Clear, .depthStoreOp = WGPUStoreOp_Store, .depthClearValue = 1.0f };
    WGPURenderPassDescriptor passDesc = { .colorAttachmentCount = 1, .colorAttachments = &ca, .depthStencilAttachment = &dsa };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (as->pointCount > 0) {
        wgpuRenderPassEncoderSetPipeline(pass, as->pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, as->vBuf, 0, as->pointCount * 8);
        wgpuRenderPassEncoderDraw(pass, as->pointCount, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(as->gpu->queue, 1, &cb);
    wgpuSurfacePresent(as->gpu->surface);

    wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(backBuffer); wgpuTextureRelease(surfTex.texture);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* as = static_cast<AppState*>(appstate);
    if (as) {
        // 1. 彻底排空后台线程数据
        std::vector<PointData3D> d; while(as->resultsQueue.try_pop(d));

        // 2. 显式释放鼠标模式
        SDL_SetWindowRelativeMouseMode(as->window, false);

        // 3. 按照“依赖者先死”原则，手动释放依赖于 Device 的资源
        if (as->msaaView) wgpuTextureViewRelease(as->msaaView);
        if (as->depthView) wgpuTextureViewRelease(as->depthView);
        if (as->vBuf) wgpuBufferRelease(as->vBuf);
        if (as->pipeline) wgpuRenderPipelineRelease(as->pipeline);
        if (as->shaderModule) wgpuShaderModuleRelease(as->shaderModule);

        // 4. 释放 GpuContext。
        // 注意：GpuContext 的析构函数中现在包含了 wgpuSurfaceUnconfigure
        delete as->gpu;

        // 5. 最后销毁 SDL 窗口
        // 顺序：Surface Release 必须发生在 SDL_DestroyWindow 之前
        SDL_DestroyWindow(as->window);
        delete as;
    }
    SDL_Quit();
}