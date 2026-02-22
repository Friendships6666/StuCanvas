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

    // --- 几何与布局 ---
    style.WindowPadding     = ImVec2(18, 18);
    style.FramePadding      = ImVec2(10, 8);
    style.ItemSpacing       = ImVec2(12, 10);
    style.WindowRounding    = 14.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.GrabRounding      = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;

    // --- 赛博配色 ---
    ImVec4 electricCyan      = ImVec4(0.00f, 1.00f, 0.95f, 1.00f);
    ImVec4 electricCyanTrans = ImVec4(0.00f, 1.00f, 0.95f, 0.25f);
    ImVec4 glassBg           = ImVec4(0.06f, 0.07f, 0.09f, 0.75f);

    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_WindowBg]               = glassBg;
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.05f, 0.05f, 0.07f, 0.85f); // 顶部菜单栏背景
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.09f, 0.11f, 0.95f); // 下拉菜单背景

    colors[ImGuiCol_Border]                 = ImVec4(0.00f, 1.00f, 0.95f, 0.40f); // 边框常亮

    colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.04f, 0.06f, 0.80f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.04f, 0.04f, 0.06f, 0.95f);

    // 交互高亮
    colors[ImGuiCol_Header]                 = electricCyanTrans;
    colors[ImGuiCol_HeaderHovered]          = electricCyan;
    colors[ImGuiCol_HeaderActive]           = electricCyan;

    colors[ImGuiCol_Button]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
    colors[ImGuiCol_ButtonHovered]          = electricCyanTrans;
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 1.00f, 0.95f, 0.45f);

    colors[ImGuiCol_Separator]              = electricCyanTrans;
}

// 定义菜单回调函数类型
using MenuCallback = std::function<void(const char* category, const char* item)>;

// 2. 封装菜单栏绘制逻辑
inline void RenderMainMenuBar(const MenuCallback& onAction, bool is3DMode, float fps) {
    if (ImGui::BeginMainMenuBar()) {

        // --- 文件菜单 ---
        if (ImGui::BeginMenu("文件 (File)")) {
            if (ImGui::BeginMenu("保存文件 (Save As)")) {
                if (ImGui::MenuItem("GeoEngine Script (.sc)")) onAction("Save", "sc");
                if (ImGui::MenuItem("GeoGebra (.ggb)"))        onAction("Save", "ggb");
                if (ImGui::MenuItem("Object File (.off)"))     onAction("Save", "off");
                if (ImGui::MenuItem("Vector Graphics (.svg)")) onAction("Save", "svg");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("导入文件 (Import)")) {
                if (ImGui::MenuItem("GeoEngine Script (.sc)")) onAction("Import", "sc");
                if (ImGui::MenuItem("GeoGebra (.ggb)"))        onAction("Import", "ggb");
                if (ImGui::MenuItem("Object File (.off)"))     onAction("Import", "off");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("退出 (Exit)", "Alt+F4")) {
                onAction("System", "Exit");
            }
            ImGui::EndMenu();
        }

        // --- 创建菜单 ---
        if (ImGui::BeginMenu("创建 (Create)")) {
            // 平面几何
            if (ImGui::BeginMenu("平面几何 (Plane Geometry)")) {
                if (ImGui::BeginMenu("点对象 (Points)")) {
                    if (ImGui::MenuItem("智能点 (Smart Point)"))      onAction("Create", "SmartPoint");
                    if (ImGui::MenuItem("自由点 (Free Point)"))       onAction("Create", "FreePoint");
                    if (ImGui::MenuItem("解析约束点 (Analytic)"))     onAction("Create", "AnalyticPoint");
                    if (ImGui::MenuItem("图解约束点 (Graphical)"))    onAction("Create", "GraphPoint");
                    if (ImGui::MenuItem("中点 (Midpoint)"))           onAction("Create", "Midpoint");
                    if (ImGui::MenuItem("定比分点 (Ratio Point)"))    onAction("Create", "RatioPoint");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("线对象 (Lines)")) {
                    if (ImGui::MenuItem("线段 (Segment)"))            onAction("Create", "Segment");
                    if (ImGui::MenuItem("射线 (Ray)"))                onAction("Create", "Ray");
                    if (ImGui::MenuItem("直线 (Line)"))               onAction("Create", "Line");
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // 圆与曲线
            if (ImGui::BeginMenu("圆与弧 (Circles)")) {
                if (ImGui::MenuItem("圆 (Circle)"))             onAction("Create", "Circle");
                if (ImGui::MenuItem("圆弧 (Arc)"))              onAction("Create", "Arc");
                if (ImGui::MenuItem("扇形 (Sector)"))           onAction("Create", "Sector");
                ImGui::EndMenu();
            }

            // 圆锥曲线
            if (ImGui::BeginMenu("圆锥曲线 (Conics)")) {
                if (ImGui::MenuItem("椭圆 (Ellipse)"))          onAction("Create", "Ellipse");
                if (ImGui::MenuItem("双曲线 (Hyperbola)"))      onAction("Create", "Hyperbola");
                if (ImGui::MenuItem("抛物线 (Parabola)"))       onAction("Create", "Parabola");
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // --- 视图菜单 ---
        if (ImGui::BeginMenu("视图 (View)")) {
            if (ImGui::MenuItem("重置相机 (Reset Camera)", "Home")) {
                onAction("View", "ResetCamera");
            }
            bool vsync = true;
            ImGui::MenuItem("垂直同步 (V-Sync)", nullptr, &vsync);
            ImGui::EndMenu();
        }

        // --- 右侧状态栏 ---
        // 动态律动文字颜色
        float pulse = (sin(ImGui::GetTime() * 2.0f) * 0.5f) + 0.5f;
        const char* statusStr = is3DMode ? "状态: 沉浸漫游 (按ESC退出)" : "状态: 界面操作 (点击背景锁定)";
        ImVec4 statusColor = is3DMode ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.9f, 0.8f + pulse * 0.2f);

        char fpsStr[32];
        snprintf(fpsStr, sizeof(fpsStr), "FPS: %.0f", fps);

        // 计算右对齐位置
        float width = ImGui::GetWindowWidth();
        float textW = ImGui::CalcTextSize(statusStr).x;
        float fpsW = ImGui::CalcTextSize(fpsStr).x;

        ImGui::SameLine(width - textW - fpsW - 40);
        ImGui::TextColored(statusColor, "%s", statusStr);

        ImGui::SameLine(width - fpsW - 10);
        ImGui::TextDisabled("%s", fpsStr);

        ImGui::EndMainMenuBar();
    }
}
} // namespace gpu