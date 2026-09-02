#include "ui/tooltips.h"

#include <imgui.h>

namespace opencapture {

void ExplainLastItem(const char* title, const char* body, bool allowWhenDisabled) {
    ImGuiHoveredFlags flags = ImGuiHoveredFlags_DelayNormal;
#if IMGUI_VERSION_NUM >= 18950
    flags |= ImGuiHoveredFlags_Stationary;
#endif
    if (allowWhenDisabled) flags |= ImGuiHoveredFlags_AllowWhenDisabled;
    const bool keyboardFocused = ImGui::GetIO().NavVisible && ImGui::IsItemFocused();
    const bool show = ImGui::IsItemHovered(flags) || keyboardFocused;
    if (!show) return;
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
    if (title != nullptr && title[0] != '\0') {
        ImGui::TextUnformatted(title);
        if (body != nullptr && body[0] != '\0') ImGui::Spacing();
    }
    if (body != nullptr && body[0] != '\0') ImGui::TextUnformatted(body);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void BeginCard(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 12.0F));
    ImGui::BeginChild(id, ImVec2(-1.0F, 0.0F),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

} // namespace opencapture
