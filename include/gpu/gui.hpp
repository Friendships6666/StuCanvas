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

    // 💡 增加一个参数接收模糊后的场景纹理视图
    inline void drawTerminal(uint32_t pointCount, float frameTime, WGPUTextureView blurredView) {
        if (showTerminal) {
            // 1. 设置窗口样式：透明背景，开启细微圆角
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // 背景完全透明
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

            ImGui::SetNextWindowSize(ImVec2(350, 180), ImGuiCond_FirstUseEver);

            // 💡 开启窗口
            if (ImGui::Begin("GeoEngine 系统终端", &showTerminal, ImGuiWindowFlags_NoBackground)) {
                // 2. 获取当前窗口的几何信息
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();
                ImDrawList* drawList = ImGui::GetWindowDrawList();

                // 3. 核心：在背景处绘制“采样纹理”
                // 我们把模糊后的场景纹理，按照窗口在屏幕的位置，对应 UV 坐标贴上去
                // 这里假设 blurredView 已经由后端转为 ImTextureID
                drawList->AddImage(
                    (ImTextureID)blurredView,
                    pos,
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    ImVec2(pos.x / 1280.0f, pos.y / 720.0f), // UV 坐标映射
                    ImVec2((pos.x + size.x) / 1280.0f, (pos.y + size.y) / 720.0f),
                    IM_COL32(255, 255, 255, 180) // 这里的 Alpha 控制玻璃的通透度
                );

                // 4. 增加一层微弱的“白雾”和“内发光”感
                drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(255, 255, 255, 20), 12.0f); // 极淡的白色叠加

                // 原有的内容绘制
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "系统状态: 液态玻璃模式");
                ImGui::Text("活跃顶点: %u", pointCount);
                ImGui::Separator();
                if (ImGui::Button("确定", ImVec2(-1, 35))) showTerminal = false;
            }
            ImGui::End();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
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
    // 在 GuiManager 类中增加一个简单方法
    inline void setMouseEnabled(bool enabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (enabled) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            // 💡 关键：当鼠标禁用时，把坐标移到屏幕外，防止悬停高亮
            io.AddMousePosEvent(-1.0f, -1.0f);
        }
    }



};
inline void ApplyCyberGlassTheme() {
    auto& style = ImGui::GetStyle();
    auto& colors = style.Colors;

    // --- 形状与布局 ---
    style.WindowPadding     = ImVec2(18, 18);
    style.FramePadding      = ImVec2(10, 8);
    style.ItemSpacing       = ImVec2(12, 10);
    style.WindowRounding    = 14.0f;
    style.FrameRounding     = 6.0f;
    style.WindowBorderSize  = 1.0f; // 💡 开启边框
    style.FrameBorderSize   = 1.0f;

    // --- 定义电光青色 (Electric Cyan) ---
    ImVec4 electricCyan      = ImVec4(0.00f, 1.00f, 0.95f, 1.00f);
    ImVec4 electricCyanTrans = ImVec4(0.00f, 1.00f, 0.95f, 0.25f);
    ImVec4 glassBg           = ImVec4(0.06f, 0.07f, 0.09f, 0.65f); // 💡 稍微加深一点背景，对比度更高

    // --- 核心配色：让边框永远亮着 ---
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_WindowBg]               = glassBg;

    // 💡 关键：让边框色始终为电光青，不分活动/非活动
    colors[ImGuiCol_Border]                 = ImVec4(0.00f, 1.00f, 0.95f, 0.40f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // 💡 关键：标题栏样式，去掉那块沉重的蓝色
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.10f, 0.70f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.08f, 0.10f, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);

    // 💡 关键：分隔线也用青色
    colors[ImGuiCol_Separator]              = electricCyanTrans;
    colors[ImGuiCol_SeparatorHovered]       = electricCyan;
    colors[ImGuiCol_SeparatorActive]        = electricCyan;

    // 控件高亮
    colors[ImGuiCol_CheckMark]              = electricCyan;
    colors[ImGuiCol_SliderGrab]             = electricCyanTrans;
    colors[ImGuiCol_SliderGrabActive]       = electricCyan;

    // 按钮
    colors[ImGuiCol_Button]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    colors[ImGuiCol_ButtonHovered]          = electricCyanTrans;
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 1.00f, 0.95f, 0.45f);

    // 帧背景（输入框等）
    colors[ImGuiCol_FrameBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(1.00f, 1.00f, 1.00f, 0.15f);
}
} // namespace gpu