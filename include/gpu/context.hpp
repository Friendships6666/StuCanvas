#pragma once
#include <webgpu/webgpu.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>


namespace gpu {

inline WGPUStringView s(const char* str) { return { str, WGPU_STRLEN }; }

class GpuContext {
public:
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    std::atomic<bool> isReady = false;

    inline void update() const {
        // 驱动异步回调（无论是 Dawn 还是 WASM 均需要 ProcessEvents）
        if (instance) wgpuInstanceProcessEvents(instance);
    }

    inline bool init(SDL_Window* window) {
        printf("[GPU] Initializing WebGPU Instance (Forced X11 on Linux)...\n");
        WGPUInstanceDescriptor instDesc = {};
        instance = wgpuCreateInstance(&instDesc);
        if (!instance) return false;

        WGPUSurfaceDescriptor surfDesc = {};
#ifdef __EMSCRIPTEN__
        static WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
        canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvasDesc.selector = s("#canvas");
        surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
#else
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        static WGPUSurfaceSourceXlibWindow xlibSource = {};
        xlibSource.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        xlibSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        xlibSource.window = static_cast<uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));

        if (!xlibSource.display) {
            printf("[GPU ERROR] Could not get X11 Display. Ensure SDL_VIDEODRIVER=x11\n");
            return false;
        }

        surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&xlibSource);
        printf("[GPU] Surface Type: Forced Xlib (X11)\n");
#endif
        surface = wgpuInstanceCreateSurface(instance, &surfDesc);
        if (!surface) return false;

        WGPURequestAdapterOptions opt = {};
        opt.compatibleSurface = surface;
        opt.powerPreference = WGPUPowerPreference_HighPerformance;

        auto onAdapter = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView, void* u, void*) {
            auto* ctx = static_cast<GpuContext*>(u);
            if (status != WGPURequestAdapterStatus_Success) {
                printf("[GPU ERROR] Failed to acquire adapter\n");
                return;
            }
            ctx->adapter = a;

            WGPUDeviceDescriptor devDesc = {};
            WGPURequestDeviceCallbackInfo dcb = { nullptr, WGPUCallbackMode_AllowProcessEvents,
                [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView, void* u, void*) {
                    auto* ctx = static_cast<GpuContext*>(u);
                    if (s != WGPURequestDeviceStatus_Success) return;
                    ctx->device = d; ctx->queue = wgpuDeviceGetQueue(d);
                    WGPUSurfaceCapabilities caps = {};
                    if (wgpuSurfaceGetCapabilities(ctx->surface, ctx->adapter, &caps) == WGPUStatus_Success) {
                        if (caps.formatCount > 0) ctx->surfaceFormat = caps.formats[0];
                        wgpuSurfaceCapabilitiesFreeMembers(caps);
                    }
                    ctx->isReady = true;
                    printf("[GPU] Device Ready. Format: 0x%X\n", ctx->surfaceFormat);
                }, ctx, nullptr };
            wgpuAdapterRequestDevice(ctx->adapter, &devDesc, dcb);
        };
        wgpuInstanceRequestAdapter(instance, &opt, {nullptr, WGPUCallbackMode_AllowProcessEvents, onAdapter, this, nullptr});
        return true;
    }

    inline void configureSurface(int w, int h) const {
        if (!device || !surface) return;
        WGPUSurfaceConfiguration config = {};
        config.device = device;
        config.format = surfaceFormat;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.width = static_cast<uint32_t>(w);
        config.height = static_cast<uint32_t>(h);
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &config);
    }

    inline ~GpuContext() {
        if (queue) wgpuQueueRelease(queue);
        if (surface) { if (device) wgpuSurfaceUnconfigure(surface); wgpuSurfaceRelease(surface); }
        if (device) wgpuDeviceRelease(device);
        if (adapter) wgpuAdapterRelease(adapter);
        if (instance) wgpuInstanceRelease(instance);
    }
};

} // namespace gpu