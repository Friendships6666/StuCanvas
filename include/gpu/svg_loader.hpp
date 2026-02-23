#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <cstdio>
#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "../../third_party/nanosvg/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "../../third_party/nanosvg/nanosvgrast.h"

namespace gpu {

// 图标纹理数据结构
struct IconTexture {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;

    bool isValid() const { return view != nullptr; }
};

/**
 * @brief 解析 SVG 并上传至 WebGPU 显存
 */
inline IconTexture LoadSvgToWebGPU(WGPUDevice device, WGPUQueue queue, const char* filepath) {
    IconTexture result;

    // 1. 解析 SVG 文件
    NSVGimage* image = nsvgParseFromFile(filepath, "px", 96.0f);
    if (!image) {
        printf("[SVG Loader ERROR] Failed to load or parse SVG file: %s\n", filepath);
        return result;
    }

    // 2. 创建光栅化器
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        printf("[SVG Loader ERROR] Failed to create SVG rasterizer.\n");
        nsvgDelete(image);
        return result;
    }

    // 强制光栅化为 64x64 分辨率，满足 WebGPU 256 字节对齐
    int targetWidth = 64;
    int targetHeight = 64;

    float scaleX = (float)targetWidth / image->width;
    float scaleY = (float)targetHeight / image->height;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    float tx = (targetWidth - image->width * scale) * 0.5f;
    float ty = (targetHeight - image->height * scale) * 0.5f;

    std::vector<unsigned char> pixels(targetWidth * targetHeight * 4, 0);

    nsvgRasterize(rast, image, tx, ty, scale, pixels.data(), targetWidth, targetHeight, targetWidth * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    // 3. 创建 WebGPU Texture
    WGPUTextureDescriptor texDesc = {};
    // 💡 修复：使用 WGPUStringView 结构体赋值
    texDesc.label = { filepath, WGPU_STRLEN };
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {(uint32_t)targetWidth, (uint32_t)targetHeight, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    result.texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!result.texture) {
        printf("[SVG Loader ERROR] Failed to create WGPUTexture for %s\n", filepath);
        return result;
    }

    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = result.texture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = targetWidth * 4; // 256
    layout.rowsPerImage = targetHeight;

    WGPUExtent3D writeSize = {(uint32_t)targetWidth, (uint32_t)targetHeight, 1};

    // 写入显存
    wgpuQueueWriteTexture(queue, &dest, pixels.data(), pixels.size(), &layout, &writeSize);

    // 5. 创建 TextureView
    WGPUTextureViewDescriptor viewDesc = {};
    // 💡 修复：使用 WGPUStringView 结构体赋值
    viewDesc.label = { filepath, WGPU_STRLEN };
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    result.view = wgpuTextureCreateView(result.texture, &viewDesc);

    printf("[SVG Loader] Successfully loaded and uploaded: %s\n", filepath);
    return result;
}

inline void DestroyIconTexture(IconTexture& icon) {
    if (icon.view) {
        wgpuTextureViewRelease(icon.view);
        icon.view = nullptr;
    }
    if (icon.texture) {
        wgpuTextureRelease(icon.texture);
        icon.texture = nullptr;
    }
}

} // namespace gpu