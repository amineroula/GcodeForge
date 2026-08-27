#include "ui/Theme.h"

#include <imgui.h>

namespace {
ImVec4 rgb(float r, float g, float b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
} // namespace

void applyGcodeForgeTheme() {
    ImGui::StyleColorsDark(); // start from the dark baseline, override what actually needs to change

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 kWindowBg = rgb(43, 45, 49);
    const ImVec4 kChildBg = rgb(35, 37, 41);
    const ImVec4 kFrameBg = rgb(30, 32, 36);
    const ImVec4 kFrameBgHovered = rgb(51, 54, 59);
    const ImVec4 kFrameBgActive = rgb(61, 64, 70);
    const ImVec4 kAccent = rgb(76, 134, 214);       // ButtonHovered / CheckMark / SliderGrab / DockingPreview
    const ImVec4 kAccentActive = rgb(47, 113, 198);  // ButtonActive / TitleBgActive / TabActive
    const ImVec4 kHeader = rgb(58, 61, 66);
    const ImVec4 kTab = rgb(47, 50, 55);
    const ImVec4 kText = rgb(228, 230, 235);
    const ImVec4 kTextDisabled = rgb(138, 141, 147);
    const ImVec4 kBorder = rgb(26, 27, 30);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDisabled;
    colors[ImGuiCol_WindowBg] = kWindowBg;
    colors[ImGuiCol_ChildBg] = kChildBg;
    colors[ImGuiCol_PopupBg] = kChildBg;
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_FrameBg] = kFrameBg;
    colors[ImGuiCol_FrameBgHovered] = kFrameBgHovered;
    colors[ImGuiCol_FrameBgActive] = kFrameBgActive;
    colors[ImGuiCol_TitleBg] = kChildBg;
    colors[ImGuiCol_TitleBgActive] = kAccentActive;
    colors[ImGuiCol_TitleBgCollapsed] = kChildBg;
    colors[ImGuiCol_MenuBarBg] = kChildBg;
    colors[ImGuiCol_ScrollbarBg] = kChildBg;
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentActive;
    colors[ImGuiCol_Button] = kHeader;
    colors[ImGuiCol_ButtonHovered] = kAccent;
    colors[ImGuiCol_ButtonActive] = kAccentActive;
    colors[ImGuiCol_Header] = kHeader;
    colors[ImGuiCol_HeaderHovered] = kAccent;
    colors[ImGuiCol_HeaderActive] = kAccent;
    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccent;
    colors[ImGuiCol_SeparatorActive] = kAccentActive;
    colors[ImGuiCol_ResizeGrip] = kHeader;
    colors[ImGuiCol_ResizeGripHovered] = kAccent;
    colors[ImGuiCol_ResizeGripActive] = kAccentActive;
    colors[ImGuiCol_Tab] = kTab;
    colors[ImGuiCol_TabHovered] = kAccent;
    colors[ImGuiCol_TabActive] = kAccentActive;
    colors[ImGuiCol_TabUnfocused] = kTab;
    colors[ImGuiCol_TabUnfocusedActive] = kHeader;
    colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f);
    colors[ImGuiCol_DockingEmptyBg] = kWindowBg;
    colors[ImGuiCol_PlotLines] = kAccent;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_NavHighlight] = kAccent;

    // Rounding/padding -- unchanged from the app's prior look-and-feel
    // (already read as reasonable), just centralized here alongside the
    // colors so the whole theme is one self-contained function.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
}
