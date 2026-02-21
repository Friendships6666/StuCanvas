#pragma once
#include <webgpu/webgpu.h>
#include <SDL3/SDL.h>
#include <iostream>
#include <atomic>
#include <cstdio>


namespace gpu {

    inline WGPUStringView s(const char* str) {
        return { str, WGPU_STRLEN };
    }

    inline void onDeviceError(const WGPUDevice*, WGPUErrorType type, WGPUStringView message, void*, void*) {
        printf("[WebGPU ERROR] Type: %d, Message: %s\n", type, message.data ? message.data : "Unknown");
    }

    class GpuContext {
    public:
        WGPUInstance instance = nullptr;
        WGPUSurface surface = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;

        std::atomic<bool> isReady = false;

        void update() {
            if (isReady) return;
            if (instance) {
                // 驱动桌面端(Dawn)和Web端的异步回调
                wgpuInstanceProcessEvents(instance);
            }
        }

        bool init(SDL_Window* window) {
            printf("[GPU] Initializing WebGPU Instance...\n");
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
            // 💡 优先探测 Wayland
            if (SDL_HasProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER)) {
                static WGPUSurfaceSourceWaylandSurface waylandSource = {};
                waylandSource.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                waylandSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
                waylandSource.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
                surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&waylandSource);
                printf("[GPU] Surface Type: Wayland detected\n");
            }
            // 💡 其次探测 X11
            else if (SDL_HasProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER)) {
                static WGPUSurfaceSourceXlibWindow xlibSource = {};
                xlibSource.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                xlibSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
                xlibSource.window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
                surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&xlibSource);
                printf("[GPU] Surface Type: X11 detected\n");
            }
#endif
            surface = wgpuInstanceCreateSurface(instance, &surfDesc);
            if (!surface) return false;

            WGPURequestAdapterOptions opt = {};
            opt.compatibleSurface = surface;

            WGPURequestAdapterCallbackInfo acb = {};
            acb.mode = WGPUCallbackMode_AllowProcessEvents;
            acb.userdata1 = this;
            acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata, void*) {
                auto* ctx = static_cast<GpuContext*>(userdata);
                if (status != WGPURequestAdapterStatus_Success) {
                    printf("[GPU ERROR] Adapter request failed\n");
                    return;
                }
                ctx->adapter = adapter;
                WGPUDeviceDescriptor devDesc = {};
                devDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;
                WGPURequestDeviceCallbackInfo dcb = { nullptr, WGPUCallbackMode_AllowProcessEvents,
                    [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata, void*) {
                        auto* ctx = static_cast<GpuContext*>(userdata);
                        if (status != WGPURequestDeviceStatus_Success) return;
                        ctx->device = device;
                        ctx->queue = wgpuDeviceGetQueue(device);
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
            wgpuInstanceRequestAdapter(instance, &opt, acb);
            return true;
        }

        void configureSurface(SDL_Window* window) const {
            if (!device || !surface) return;
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            WGPUSurfaceConfiguration config = {};
            config.device = device;
            config.format = surfaceFormat;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = (uint32_t)w;
            config.height = (uint32_t)h;
            config.presentMode = WGPUPresentMode_Fifo;
            config.alphaMode = WGPUCompositeAlphaMode_Auto;
            wgpuSurfaceConfigure(surface, &config);
            wgpuSurfaceConfigure(surface, &config);
        }

        ~GpuContext() {
            if (queue) wgpuQueueRelease(queue);
            if (surface) { if (device) wgpuSurfaceUnconfigure(surface); wgpuSurfaceRelease(surface); }
            if (device) wgpuDeviceRelease(device);
            if (adapter) wgpuAdapterRelease(adapter);
            if (instance) wgpuInstanceRelease(instance);
        }
    };
}