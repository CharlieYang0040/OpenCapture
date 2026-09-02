#include "ui/theme.h"

#include <imgui.h>

namespace opencapture {
namespace {

constexpr ImVec4 Rgb(int r, int g, int b, float a = 1.0F) noexcept {
    return {static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
            static_cast<float>(b) / 255.0F, a};
}

} // namespace

void ApplyOpenCaptureTheme(ImGuiStyle& style) noexcept {
    style = ImGuiStyle{};
    style.WindowRounding = 0.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.PopupRounding = 8.0F;
    style.ScrollbarRounding = 8.0F;
    style.TabRounding = 6.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowPadding = ImVec2(18.0F, 14.0F);
    style.FramePadding = ImVec2(12.0F, 7.0F);
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.IndentSpacing = 16.0F;
    style.ScrollbarSize = 14.0F;
    style.GrabMinSize = 12.0F;
    style.HoverDelayNormal = 0.40F;
    style.HoverDelayShort = 0.15F;

    const ImVec4 bg = Rgb(14, 18, 24);
    const ImVec4 card = Rgb(22, 28, 38);
    const ImVec4 cardHover = Rgb(30, 38, 52);
    const ImVec4 text = Rgb(230, 237, 243);
    const ImVec4 muted = Rgb(154, 167, 184);
    const ImVec4 accent = Rgb(64, 140, 255);
    const ImVec4 accentHover = Rgb(92, 160, 255);
    const ImVec4 border = Rgb(42, 52, 68);
    const ImVec4 recording = Rgb(240, 196, 64);

    auto* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = muted;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = card;
    colors[ImGuiCol_PopupBg] = Rgb(18, 24, 34, 0.98F);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = Rgb(12, 16, 24);
    colors[ImGuiCol_FrameBgHovered] = cardHover;
    colors[ImGuiCol_FrameBgActive] = Rgb(36, 48, 68);
    colors[ImGuiCol_TitleBg] = bg;
    colors[ImGuiCol_TitleBgActive] = bg;
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg] = card;
    colors[ImGuiCol_ScrollbarBg] = bg;
    colors[ImGuiCol_ScrollbarGrab] = border;
    colors[ImGuiCol_ScrollbarGrabHovered] = muted;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Button] = Rgb(36, 54, 86);
    colors[ImGuiCol_ButtonHovered] = Rgb(52, 78, 122);
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = Rgb(32, 48, 74);
    colors[ImGuiCol_HeaderHovered] = Rgb(48, 72, 110);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = border;
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accentHover;
    colors[ImGuiCol_Tab] = Rgb(24, 32, 44);
    colors[ImGuiCol_TabHovered] = Rgb(48, 72, 110);
#if IMGUI_VERSION_NUM >= 19100
    colors[ImGuiCol_TabSelected] = Rgb(36, 58, 96);
    colors[ImGuiCol_TabDimmed] = Rgb(20, 26, 36);
    colors[ImGuiCol_TabDimmedSelected] = Rgb(32, 48, 74);
#else
    colors[ImGuiCol_TabActive] = Rgb(36, 58, 96);
    colors[ImGuiCol_TabUnfocused] = Rgb(20, 26, 36);
    colors[ImGuiCol_TabUnfocusedActive] = Rgb(32, 48, 74);
#endif
    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = recording;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = recording;
    colors[ImGuiCol_TableHeaderBg] = card;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = Rgb(18, 24, 34, 0.4F);
    colors[ImGuiCol_TextSelectedBg] = Rgb(64, 140, 255, 0.35F);
    colors[ImGuiCol_DragDropTarget] = recording;
#if IMGUI_VERSION_NUM >= 19100
    colors[ImGuiCol_NavCursor] = accent;
#else
    colors[ImGuiCol_NavHighlight] = accent;
#endif
    colors[ImGuiCol_NavWindowingHighlight] = Rgb(255, 255, 255, 0.3F);
    colors[ImGuiCol_NavWindowingDimBg] = Rgb(0, 0, 0, 0.35F);
    colors[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.45F);
}

} // namespace opencapture
