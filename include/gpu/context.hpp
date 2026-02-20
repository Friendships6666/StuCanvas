#pragma once
#ifndef __EMSCRIPTEN__
#include <dawn/webgpu.h>
#endif
#include <gpu/utils.hpp>
#include <SDL3/SDL.h>
#include <iostream>

namespace gpu {

class GpuContext {
public:
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;

    bool init(SDL_Window* window) {
        WGPUInstanceDescriptor instDesc = {};
        instance = wgpuCreateInstance(&instDesc);
        if (!instance) return false;

        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        WGPUSurfaceDescriptor surfDesc = {};

#ifndef __EMSCRIPTEN__
        if (SDL_HasProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER)) {
            static WGPUSurfaceSourceXlibWindow xlibSource = {};
            xlibSource.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            xlibSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            xlibSource.window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
            surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&xlibSource);
        }
        else if (SDL_HasProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER)) {
            static WGPUSurfaceSourceWaylandSurface waylandSource = {};
            waylandSource.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
            waylandSource.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            waylandSource.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&waylandSource);
        }
#endif
        surface = wgpuInstanceCreateSurface(instance, &surfDesc);

        // 请求 Adapter
        RequestState adapterState;
        WGPURequestAdapterOptions opt = {};
        opt.compatibleSurface = surface;
        WGPURequestAdapterCallbackInfo acb = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void* u1, void* u2) {
            auto* state = static_cast<RequestState*>(u1);
            if (s == WGPURequestAdapterStatus_Success) state->result = a;
            state->ended = true;
        }, .userdata1 = &adapterState };

        WGPUFuture adapterFuture = wgpuInstanceRequestAdapter(instance, &opt, acb);
        WGPUFutureWaitInfo adapterWait = { .future = adapterFuture };
        while (!adapterState.ended) wgpuInstanceWaitAny(instance, 1, &adapterWait, 0);
        adapter = static_cast<WGPUAdapter>(adapterState.result);

        // 请求 Device
        RequestState deviceState;
        WGPUDeviceDescriptor devDesc = {};
        // 错误回调现在在这里设置
        devDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;

        WGPURequestDeviceCallbackInfo dcb = { .mode = WGPUCallbackMode_AllowProcessEvents, .callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void* u1, void* u2) {
            auto* state = static_cast<RequestState*>(u1);
            if (s == WGPURequestDeviceStatus_Success) state->result = d;
            state->ended = true;
        }, .userdata1 = &deviceState };

        WGPUFuture deviceFuture = wgpuAdapterRequestDevice(adapter, &devDesc, dcb);
        WGPUFutureWaitInfo deviceWait = { .future = deviceFuture };
        while (!deviceState.ended) wgpuInstanceWaitAny(instance, 1, &deviceWait, 0);
        device = static_cast<WGPUDevice>(deviceState.result);

        queue = wgpuDeviceGetQueue(device);
        WGPUSurfaceCapabilities caps = {};
        if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success) {
            if (caps.formatCount > 0) surfaceFormat = caps.formats[0];
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        return true;
    }

    void configureSurface(SDL_Window* window) const {
        int w, h;
        if (SDL_GetWindowSizeInPixels(window, &w, &h) && w > 0) {
            WGPUSurfaceConfiguration config = { .device = device, .format = surfaceFormat, .usage = WGPUTextureUsage_RenderAttachment, .width = (uint32_t)w, .height = (uint32_t)h, .presentMode = WGPUPresentMode_Fifo, .alphaMode = WGPUCompositeAlphaMode_Auto };
            wgpuSurfaceConfigure(surface, &config);
        }
    }

    ~GpuContext() {
        if (queue) {
            wgpuQueueRelease(queue);
            queue = nullptr;
        }

        // ==========================================
        // 核心修复：必须在 Device 释放前释放 Surface！
        // ==========================================
        if (surface) {
            if (device) {
                // 解除 SwapChain 与 Device 的绑定
                wgpuSurfaceUnconfigure(surface);
            }
            wgpuSurfaceRelease(surface);
            surface = nullptr;
        }

        // 现在可以安全地销毁 Device 了
        if (device) {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
            device = nullptr;
        }

        if (adapter) {
            wgpuAdapterRelease(adapter);
            adapter = nullptr;
        }

        if (instance) {
            wgpuInstanceRelease(instance);
            instance = nullptr;
        }
    }
};

} // namespace gpu