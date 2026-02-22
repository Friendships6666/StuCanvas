#pragma once
#include <../third_party/imgui/imgui.h>
#include <../third_party/imgui/backends/imgui_impl_sdl3.h>
#include <../third_party/imgui/backends/imgui_impl_wgpu.h>
#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <cstdio>

namespace gpu {

class GuiManager {
public:
    bool showTerminal = true;
    bool isSdlReady = false;
    bool isWgpuReady = false;

    inline void initSdl(SDL_Window* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // 加载字体
        io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
        // 注意：WASM下需要确保此路径在虚拟文件系统中
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/NotoSansSC-Regular.ttf",
            20.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
        );
        if (!font) printf("[GUI] Warning: Font assets/fonts/NotoSansSC-Regular.ttf not found.\n");

        ImGui::StyleColorsDark();
        ImGui_ImplSDL3_InitForOther(window);
        isSdlReady = true;
    }

    inline void initWgpu(WGPUDevice device, WGPUTextureFormat format) {
        ImGui_ImplWGPU_InitInfo init_info = {};
        init_info.Device = device;
        init_info.NumFramesInFlight = 3;
        init_info.RenderTargetFormat = format;
        init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

        ImGui_ImplWGPU_Init(&init_info);
        ImGui_ImplWGPU_CreateDeviceObjects();
        isWgpuReady = true;
    }

    inline void beginFrame() {
        if (!isWgpuReady) return;
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    inline void drawTerminal(uint32_t pointCount, float frameTime) {
        if (showTerminal) {
            ImGui::SetNextWindowSize(ImVec2(350, 180), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("GeoEngine 系统终端", &showTerminal)) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "系统状态: 稳定运行");
                ImGui::Text("活跃顶点数: %u", pointCount);
                ImGui::Text("帧延迟: %.1f ms", frameTime);
                ImGui::Separator();
                ImGui::BulletText("点击背景: 锁定相机控制视角");
                ImGui::BulletText("ESC 键: 释放鼠标点击 UI");
                if (ImGui::Button("隐藏终端", ImVec2(-1, 35))) showTerminal = false;
            }
            ImGui::End();
        }
    }

    inline void endFrame(WGPURenderPassEncoder pass) {
        if (!isWgpuReady) return;
        ImGui::Render();
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    }

    inline void cleanup() {
        if (isWgpuReady) ImGui_ImplWGPU_Shutdown();
        if (isSdlReady) { ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); }
    }
};

} // namespace gpu