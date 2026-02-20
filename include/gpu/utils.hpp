#pragma once
#include <webgpu/webgpu.h>
#include <iostream>

namespace gpu {

    // 辅助将 C 字符串转为 WebGPU StringView
    inline WGPUStringView s(const char* str) {
        return { str, WGPU_STRLEN };
    }

    // 通用的异步状态追踪
    struct RequestState {
        bool ended = false;
        void* result = nullptr;
    };

    // 通用错误回调
    inline void onDeviceError(const WGPUDevice*, WGPUErrorType type, WGPUStringView message, void*, void*) {
        std::cerr << "WebGPU Error: " << (message.data ? message.data : "Unknown") << std::endl;
    }

} // namespace gpu