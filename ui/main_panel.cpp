#include "ui/main_panel.h"

#include <imgui.h>

#include <array>

namespace opencapture {

void MainPanel::Draw(std::string_view gpuName, std::string_view ffmpegVersion) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OpenCapture", nullptr, flags);

    ImGui::TextUnformatted("OpenCapture");
    ImGui::Separator();

    static int targetType = 0;
    ImGui::TextUnformatted("Target");
    ImGui::SameLine();
    ImGui::RadioButton("Window", &targetType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Region", &targetType, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Monitor", &targetType, 2);
    ImGui::Button("Select capture target", ImVec2(220.0F, 0.0F));

    static int format = 0;
    static int fps = 60;
    static int quality = 1;
    constexpr std::array formats{"MKV / H.264", "MP4 / H.264", "MKV / HEVC"};
    constexpr std::array qualities{"Performance", "Balanced", "Quality"};
    ImGui::Combo("Format", &format, formats.data(), static_cast<int>(formats.size()));
    ImGui::SliderInt("FPS", &fps, 15, 120);
    ImGui::Combo("Quality", &quality, qualities.data(), static_cast<int>(qualities.size()));

    static bool systemAudio = true;
    static bool microphone = false;
    static bool cursor = true;
    ImGui::Checkbox("System audio", &systemAudio);
    ImGui::SameLine();
    ImGui::Checkbox("Microphone", &microphone);
    ImGui::SameLine();
    ImGui::Checkbox("Cursor", &cursor);

    ImGui::Spacing();
    ImGui::Button("Screenshot", ImVec2(130.0F, 40.0F));
    ImGui::SameLine();
    ImGui::Button("GIF", ImVec2(100.0F, 40.0F));
    ImGui::SameLine();
    ImGui::Button("Start recording", ImVec2(170.0F, 40.0F));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Ready | GPU: %.*s", static_cast<int>(gpuName.size()), gpuName.data());
    ImGui::Text("FFmpeg: %.*s", static_cast<int>(ffmpegVersion.size()), ffmpegVersion.data());
    ImGui::TextDisabled("Capture and recording actions are enabled in the next milestone.");
    ImGui::End();
}

} // namespace opencapture

