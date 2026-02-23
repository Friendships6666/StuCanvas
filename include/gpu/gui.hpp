#pragma once

#include <../third_party/imgui/imgui.h>
#include <../third_party/imgui/imgui_internal.h>
#include <../third_party/imgui/backends/imgui_impl_sdl3.h>
#include <../third_party/imgui/backends/imgui_impl_wgpu.h>
#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <cstdio>
#include <vector>
#include <string>
#include <functional>

namespace gpu {

using MenuCallback = std::function<void(const char* category, const char* item)>;

enum class RibbonCategory { PlaneGeometry, PlaneFunction };
struct DocumentTab { std::string name; bool isActive; };

struct ToolItem {
    const char* displayName;
    const char* actionName;
    ImTextureID iconId = (ImTextureID)0;
};

// -----------------------------------------------------------------
// 专属组件：带 SVG 图标渲染的悬浮记忆下拉菜单
// -----------------------------------------------------------------
inline void DrawToolDropdown(const char* popupId, const std::vector<ToolItem>& items, int& currentIndex, const ImVec2& size, const MenuCallback& onAction) {
    const auto& currentItem = items[currentIndex];

    std::string btnText = std::string("\n\n") + currentItem.displayName + " \xEF\xBC\x8B";

    ImVec2 pos = ImGui::GetCursorScreenPos();

    if (ImGui::Button(btnText.c_str(), size)) {
        onAction("Create", currentItem.actionName);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::OpenPopup(popupId);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (currentItem.iconId) {
        float iconSize = 36.0f;
        float iconX = pos.x + (size.x - iconSize) * 0.5f;
        float iconY = pos.y + 4.0f;

        ImVec2 iconMin(iconX, iconY);
        ImVec2 iconMax(iconX + iconSize, iconY + iconSize);

        // 💡 强化 SVG 图标颜色：传入深色 Tint 强行加深 (正片叠底)
        // 即使你的 SVG 原本是浅灰甚至白色的，也会被染成清晰的深灰色
        ImU32 iconColorTint = IM_COL32(20, 20, 20, 255);
        drawList->AddImage(currentItem.iconId, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), iconColorTint);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y));

    if (ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration)) {
        for (int i = 0; i < (int)items.size(); ++i) {
            bool isSelected = (currentIndex == i);
            // 💡 下拉菜单文字颜色加深
            ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? IM_COL32(0, 95, 184, 255) : IM_COL32(20, 20, 20, 255));

            ImVec2 itemPos = ImGui::GetCursorScreenPos();

            std::string menuText = std::string("       ") + items[i].displayName;
            if (ImGui::Selectable(menuText.c_str(), isSelected, 0, ImVec2(120, 30))) {
                currentIndex = i;
                onAction("Create", items[i].actionName);
            }

            if (items[i].iconId) {
                float smallIconSize = 22.0f;
                float smallYOffset = (30.0f - smallIconSize) * 0.5f;

                ImVec2 smallMin(itemPos.x + 4.0f, itemPos.y + smallYOffset);
                ImVec2 smallMax(smallMin.x + smallIconSize, smallMin.y + smallIconSize + smallYOffset);

                // 💡 下拉菜单里的 SVG 小图标同样加深
                drawList->AddImage(items[i].iconId, smallMin, smallMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(20, 20, 20, 255));
            }

            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// -----------------------------------------------------------------
// 辅助函数：绘制面板底部居中的提示文字
// -----------------------------------------------------------------
inline void EndRibbonPanel(const char* panelName) {
    ImVec2 textSize = ImGui::CalcTextSize(panelName);
    float winWidth = ImGui::GetWindowWidth();
    float winHeight = ImGui::GetWindowHeight();

    ImGui::SetCursorPosY(winHeight - textSize.y - 4.0f);
    ImGui::SetCursorPosX((winWidth - textSize.x) * 0.5f);

    // 💡 注脚文字加深 (深灰)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 80, 80, 255));
    ImGui::Text("%s", panelName);
    ImGui::PopStyleColor();

    ImGui::EndChild();
}

// -----------------------------------------------------------------
// 核心：半透明 AutoCAD Ribbon 渲染
// -----------------------------------------------------------------
inline void RenderCadRibbon(const MenuCallback& onAction, bool is3DMode, float fps, ImTextureID pointIcon) {
    static RibbonCategory currentCategory = RibbonCategory::PlaneGeometry;
    static std::vector<DocumentTab> openTabs = { {"几何画布 1", true}, {"函数空间 1", false} };
    static int activeTabIdx = 0;
    static int docCounter = 3;

    static int currentPointTool = 0;
    static int currentLineTool = 0;
    static int currentCircleTool = 0;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float ribbonWidth = viewport->Size.x;
    float ribbonHeight = 195.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(ribbonWidth, ribbonHeight));

    ImGuiWindowFlags ribbonFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##ModernRibbon", nullptr, ribbonFlags)) {

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();

        float row1_Y = 35.0f;
        float row2_Y = 155.0f;
        float row3_Y = 190.0f;

        ImU32 colWhite    = IM_COL32(255, 255, 255, 200);
        ImU32 colGray     = IM_COL32(245, 245, 248, 190);
        ImU32 colCyanGray = IM_COL32(230, 235, 238, 180);
        ImU32 colBorder   = IM_COL32(200, 200, 200, 150);

        drawList->AddRectFilled(p0, ImVec2(p0.x + ribbonWidth, p0.y + row1_Y), colWhite);
        drawList->AddRectFilled(ImVec2(p0.x, p0.y + row1_Y), ImVec2(p0.x + ribbonWidth, p0.y + row2_Y), colGray);
        drawList->AddRectFilled(ImVec2(p0.x, p0.y + row2_Y), ImVec2(p0.x + ribbonWidth, p0.y + row3_Y), colCyanGray);

        drawList->AddLine(ImVec2(p0.x, p0.y + row1_Y), ImVec2(p0.x + ribbonWidth, p0.y + row1_Y), colBorder);
        drawList->AddLine(ImVec2(p0.x, p0.y + row2_Y), ImVec2(p0.x + ribbonWidth, p0.y + row2_Y), colBorder);

        // ==========================================
        // 1. 第一行：联通标签页
        // ==========================================
        ImGui::SetCursorPos(ImVec2(10.0f, 0.0f));

        auto drawTopTab = [&](const char* label, RibbonCategory cat) {
            bool isSelected = (currentCategory == cat);
            ImVec2 textSize = ImGui::CalcTextSize(label);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();

            float tabWidth = textSize.x + 24.0f;
            float tabHeight = row1_Y;

            if (isSelected) {
                drawList->AddRectFilled(
                    ImVec2(cursorPos.x, cursorPos.y),
                    ImVec2(cursorPos.x + tabWidth, cursorPos.y + tabHeight + 1.0f),
                    colGray
                );
                drawList->AddLine(ImVec2(cursorPos.x, cursorPos.y), ImVec2(cursorPos.x, cursorPos.y + tabHeight), colBorder);
                drawList->AddLine(ImVec2(cursorPos.x + tabWidth, cursorPos.y), ImVec2(cursorPos.x + tabWidth, cursorPos.y + tabHeight), colBorder);
                drawList->AddLine(ImVec2(cursorPos.x, cursorPos.y), ImVec2(cursorPos.x + tabWidth, cursorPos.y), IM_COL32(0, 95, 184, 255), 2.0f);
            }

            if (ImGui::InvisibleButton(label, ImVec2(tabWidth, tabHeight))) {
                currentCategory = cat;
            }

            // 💡 顶级标签文字加深：选中纯黑，未选中深灰
            ImU32 textColor = isSelected ? IM_COL32(0, 0, 0, 255) : IM_COL32(60, 60, 60, 255);
            drawList->AddText(ImVec2(cursorPos.x + 12.0f, cursorPos.y + (tabHeight - textSize.y) * 0.5f), textColor, label);
            ImGui::SameLine(0, 2.0f);
        };

        drawTopTab("平面几何", RibbonCategory::PlaneGeometry);
        drawTopTab("平面函数绘图", RibbonCategory::PlaneFunction);

        ImGui::SetCursorPos(ImVec2(ribbonWidth - 80.0f, 10.0f));
        // 💡 FPS 文字加深
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(60, 60, 60, 255));
        ImGui::Text("%.0f FPS", fps);
        ImGui::PopStyleColor();

        // ==========================================
        // 2. 第二行：功能区面板 (带 SVG 悬浮下拉)
        // ==========================================
        ImGui::SetCursorPos(ImVec2(0, row1_Y + 5.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        if (ImGui::BeginChild("##ToolGroupsContainer", ImVec2(0, row2_Y - row1_Y - 5), false, ImGuiWindowFlags_NoScrollbar)) {
            ImVec2 btnSize(70, 70);
            ImGui::SetCursorPosY(5.0f);
            ImGui::Indent(15.0f);

            if (currentCategory == RibbonCategory::PlaneGeometry) {
                if (ImGui::BeginChild("PanelDraw", ImVec2(240, 100), false, ImGuiWindowFlags_NoScrollbar)) {

                    std::vector<ToolItem> pointTools = {
                        {"自由点", "FreePoint", pointIcon},
                        {"约束点", "ConstrainedPoint", pointIcon},
                        {"交点", "IntersectPoint", pointIcon}
                    };
                    DrawToolDropdown("PointCombo", pointTools, currentPointTool, btnSize, onAction);
                    ImGui::SameLine();

                    std::vector<ToolItem> lineTools = {
                        {"线段", "Segment", (ImTextureID)0},
                        {"直线", "Line", (ImTextureID)0},
                        {"射线", "Ray", (ImTextureID)0}
                    };
                    DrawToolDropdown("LineCombo", lineTools, currentLineTool, btnSize, onAction);
                    ImGui::SameLine();

                    std::vector<ToolItem> circleTools = {
                        {"圆", "Circle", (ImTextureID)0}
                    };
                    DrawToolDropdown("CircleCombo", circleTools, currentCircleTool, btnSize, onAction);

                    EndRibbonPanel("绘图");
                }
                ImGui::SameLine(0, 10);

                ImVec2 vLinePos = ImGui::GetCursorScreenPos();
                drawList->AddLine(ImVec2(vLinePos.x, vLinePos.y + 10), ImVec2(vLinePos.x, vLinePos.y + 70), colBorder);
                ImGui::Dummy(ImVec2(1, 1));
                ImGui::SameLine(0, 10);

                if (ImGui::BeginChild("PanelModify", ImVec2(160, 100), false, ImGuiWindowFlags_NoScrollbar)) {
                    if (ImGui::Button("\n\n镜像", btnSize)) onAction("Modify", "Mirror"); ImGui::SameLine();
                    if (ImGui::Button("\n\n修剪", btnSize)) onAction("Modify", "Trim");
                    EndRibbonPanel("修改");
                }
            }
            else {
                if (ImGui::BeginChild("PanelFunc", ImVec2(280, 100), false, ImGuiWindowFlags_NoScrollbar)) {
                    if (ImGui::Button("显式\ny=f(x)", btnSize)) onAction("Plot", "Exp"); ImGui::SameLine();
                    if (ImGui::Button("极坐标\nr=f(θ)", btnSize)) onAction("Plot", "Pol"); ImGui::SameLine();
                    if (ImGui::Button("参数\n方程", btnSize)) onAction("Plot", "Param");
                    EndRibbonPanel("坐标系映射");
                }
            }
            ImGui::Unindent(15.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // ==========================================
        // 3. 第三行：平行四边形标签
        // ==========================================
        ImVec2 tabBasePos = ImVec2(p0.x, p0.y + row2_Y);
        float currentX = tabBasePos.x + 10.0f;
        float slant = 12.0f;
        float tabH = 35.0f;

        for (int i = 0; i < openTabs.size(); ++i) {
            ImVec2 textSize = ImGui::CalcTextSize(openTabs[i].name.c_str());
            float tabW = textSize.x + 40.0f + slant;

            ImVec2 pts[4] = {
                ImVec2(currentX, tabBasePos.y + tabH),
                ImVec2(currentX + slant, tabBasePos.y),
                ImVec2(currentX + tabW + slant, tabBasePos.y),
                ImVec2(currentX + tabW, tabBasePos.y + tabH)
            };

            ImRect tabRect(currentX, tabBasePos.y, currentX + tabW + slant, tabBasePos.y + tabH);
            bool isHovered = ImGui::IsMouseHoveringRect(tabRect.Min, tabRect.Max);
            bool isActive = (activeTabIdx == i);

            if (isHovered && ImGui::IsMouseClicked(0)) {
                activeTabIdx = i;
            }

            ImU32 bgColor = isActive ? IM_COL32(255, 255, 255, 220) : (isHovered ? IM_COL32(245, 245, 245, 180) : IM_COL32(220, 225, 228, 150));
            drawList->AddConvexPolyFilled(pts, 4, bgColor);

            ImU32 borderColor = isActive ? IM_COL32(180, 180, 180, 200) : IM_COL32(200, 205, 210, 150);
            drawList->AddPolyline(pts, 4, borderColor, ImDrawFlags_Closed, 1.0f);

            if (isActive) {
                drawList->AddLine(pts[0], pts[3], IM_COL32(255, 255, 255, 220), 2.0f);
            }

            // 💡 底部标签页文字加深：选中纯黑，未选中深灰
            ImU32 txtColor = isActive ? IM_COL32(0, 0, 0, 255) : IM_COL32(70, 70, 70, 255);
            drawList->AddText(ImVec2(currentX + slant + 12.0f, tabBasePos.y + (tabH - textSize.y) * 0.5f), txtColor, openTabs[i].name.c_str());

            ImVec2 closeCenter(currentX + tabW - 10.0f, tabBasePos.y + tabH * 0.5f);
            float xRadius = 4.0f;
            ImRect closeRect(closeCenter.x - xRadius - 4, closeCenter.y - xRadius - 4, closeCenter.x + xRadius + 4, closeCenter.y + xRadius + 4);
            bool closeHovered = ImGui::IsMouseHoveringRect(closeRect.Min, closeRect.Max);

            ImU32 xColor = closeHovered ? IM_COL32(255, 60, 60, 255) : IM_COL32(120, 120, 120, 255);
            drawList->AddLine(ImVec2(closeCenter.x - xRadius, closeCenter.y - xRadius), ImVec2(closeCenter.x + xRadius, closeCenter.y + xRadius), xColor, 1.5f);
            drawList->AddLine(ImVec2(closeCenter.x + xRadius, closeCenter.y - xRadius), ImVec2(closeCenter.x - xRadius, closeCenter.y + xRadius), xColor, 1.5f);

            if (closeHovered && ImGui::IsMouseClicked(0)) {
                openTabs.erase(openTabs.begin() + i);
                if (activeTabIdx >= openTabs.size()) activeTabIdx = openTabs.size() - 1;
                i--;
                continue;
            }
            currentX += tabW + 2.0f;
        }

        ImVec2 addCenter(currentX + 15.0f, tabBasePos.y + tabH * 0.5f);
        ImRect addRect(addCenter.x - 10, addCenter.y - 10, addCenter.x + 10, addCenter.y + 10);
        bool addHovered = ImGui::IsMouseHoveringRect(addRect.Min, addRect.Max);

        ImU32 plusColor = addHovered ? IM_COL32(0, 95, 184, 255) : IM_COL32(120, 120, 120, 255);
        drawList->AddLine(ImVec2(addCenter.x - 5, addCenter.y), ImVec2(addCenter.x + 5, addCenter.y), plusColor, 2.0f);
        drawList->AddLine(ImVec2(addCenter.x, addCenter.y - 5), ImVec2(addCenter.x, addCenter.y + 5), plusColor, 2.0f);

        if (addHovered && ImGui::IsMouseClicked(0)) {
            openTabs.push_back({"新建视口 " + std::to_string(docCounter++), true});
            activeTabIdx = openTabs.size() - 1;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // ==========================================
    // 4. 底部隐式状态栏
    // ==========================================
    ImGui::SetNextWindowPos(ImVec2(10, viewport->Size.y - 35));
    if (ImGui::Begin("##StatusBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground)) {
        const char* statusStr = is3DMode ? "沉浸漫游模式: [W/A/S/D]移动 | [ESC]退出" : "操作模式: [点击背景]锁定 3D 视图";
        // 💡 底部悬浮文字加深，确保在亮色场景里可见
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(60, 60, 60, 255));
        ImGui::Text("● %s", statusStr);
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

// -----------------------------------------------------------------
// 全局半透明浅色样式
// -----------------------------------------------------------------
inline void ApplyLightTheme() {
    auto& style = ImGui::GetStyle();
    auto& colors = style.Colors;

    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 6);
    style.ItemSpacing       = ImVec2(8, 8);
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;

    // 💡 全局核心文本颜色设置为纯黑
    colors[ImGuiCol_Text]                   = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    colors[ImGuiCol_Button]                 = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.9f, 0.94f, 0.98f, 0.8f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.85f, 0.90f, 0.95f, 0.9f);

    colors[ImGuiCol_Border]                 = ImVec4(0.85f, 0.85f, 0.85f, 0.5f);
}

// -----------------------------------------------------------------
// 管理类保持不变
// -----------------------------------------------------------------
class GuiManager {
public:
    bool showTerminal = false;
    bool isSdlReady = false;
    bool isWgpuReady = false;

    inline void initSdl(SDL_Window* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            "assets/fonts/NotoSansSC-Regular.ttf",
            20.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
        );
        if (!font) printf("[GUI] Warning: Font assets/fonts/NotoSansSC-Regular.ttf not found.\n");

        ImGui::StyleColorsLight();
        ApplyLightTheme();
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

    inline void drawTerminal(uint32_t pointCount, float frameTime, WGPUTextureView blurredView) { }

    inline void endFrame(WGPURenderPassEncoder pass) {
        if (!isWgpuReady) return;
        ImGui::Render();
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    }

    inline void cleanup() {
        if (isWgpuReady) ImGui_ImplWGPU_Shutdown();
        if (isSdlReady) { ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); }
    }

    inline void setMouseEnabled(bool enabled) {
        ImGuiIO& io = ImGui::GetIO();
        if (enabled) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            io.AddMousePosEvent(-1.0f, -1.0f);
        }
    }
};

} // namespace gpu