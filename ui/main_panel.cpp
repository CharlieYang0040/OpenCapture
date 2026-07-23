#include "ui/main_panel.h"

#include "platform/capture_target_picker.h"
#include "platform/windows_graphics_capture.h"

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace opencapture {
namespace {

std::uint64_t IconFingerprint(const WindowEntry& window) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto pixel : window.iconPixels) {
        hash ^= pixel;
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct CachedWindowIcon {
    std::uint64_t fingerprint{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
};

class WindowIconCache final {
public:
    ID3D11ShaderResourceView* Get(ID3D11Device* device, const WindowEntry& window) {
        if (!device || !window.hasIcon) return nullptr;
        const auto key = reinterpret_cast<std::uintptr_t>(window.handle);
        const auto fingerprint = IconFingerprint(window);
        auto found = icons_.find(key);
        if (found != icons_.end() && found->second.fingerprint == fingerprint) return found->second.view.Get();

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(WindowEntry::IconWidth);
        description.Height = static_cast<UINT>(WindowEntry::IconHeight);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = window.iconPixels.data();
        data.SysMemPitch = static_cast<UINT>(WindowEntry::IconWidth * sizeof(std::uint32_t));
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&description, &data, &texture))) return nullptr;

        CachedWindowIcon cached{};
        cached.fingerprint = fingerprint;
        if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &cached.view))) return nullptr;
        auto& stored = icons_[key];
        stored = std::move(cached);
        return stored.view.Get();
    }

    void Prune(const std::vector<WindowEntry>& windows) {
        std::unordered_set<std::uintptr_t> active;
        active.reserve(windows.size());
        for (const auto& window : windows) active.insert(reinterpret_cast<std::uintptr_t>(window.handle));
        for (auto iterator = icons_.begin(); iterator != icons_.end();) {
            if (!active.contains(iterator->first)) iterator = icons_.erase(iterator);
            else ++iterator;
        }
    }

private:
    std::unordered_map<std::uintptr_t, CachedWindowIcon> icons_;
};

WindowIconCache& Icons() {
    static WindowIconCache cache;
    return cache;
}

} // namespace

MainPanelCommand MainPanel::Draw(std::string_view gpuName, std::string_view ffmpegVersion,
                                 std::string_view encoderSummary,
                                 const std::vector<EncoderUiChoice>& encoderChoices,
                                 std::string_view frameProcessingError,
                                 std::string_view screenshotStatus,
                                 std::string_view audioStatus,
                                 std::string_view recoveryStatus,
                                 const RecordingUiState& recording,
                                 CaptureTargetPicker& picker, WindowsGraphicsCapture& capture,
                                 HWND owner, ID3D11Device* device) {
    MainPanelCommand command{};
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OpenCapture", nullptr, flags);

    ImGui::TextUnformatted("OpenCapture");
    ImGui::Separator();

    static int selectedEncoder = 0;
    if (selectedEncoder > static_cast<int>(encoderChoices.size())) selectedEncoder = 0;
    const char* encoderPreview = "Auto (recommended)";
    if (selectedEncoder > 0) encoderPreview = encoderChoices[static_cast<std::size_t>(selectedEncoder - 1)].displayName.data();
    ImGui::TextUnformatted("Video encoder");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##VideoEncoder", encoderPreview)) {
        if (ImGui::Selectable("Auto (recommended)", selectedEncoder == 0)) selectedEncoder = 0;
        for (std::size_t index = 0; index < encoderChoices.size(); ++index) {
            const auto& choice = encoderChoices[index];
            if (!choice.usable) ImGui::BeginDisabled();
            const bool selected = selectedEncoder == static_cast<int>(index + 1);
            if (ImGui::Selectable(choice.displayName.data(), selected) && choice.usable) {
                selectedEncoder = static_cast<int>(index + 1);
            }
            if (ImGui::IsItemHovered() && !choice.detail.empty()) ImGui::SetTooltip("%.*s", static_cast<int>(choice.detail.size()), choice.detail.data());
            if (!choice.usable) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }

    static int targetType = static_cast<int>(picker.Selected().type);
    ImGui::TextUnformatted("Target");
    ImGui::SameLine();
    ImGui::RadioButton("Window", &targetType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Region", &targetType, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Monitor", &targetType, 2);
    if (ImGui::Button("Select capture target", ImVec2(220.0F, 0.0F))) {
        if (targetType == static_cast<int>(CaptureTargetType::Region)) picker.SelectRegion(owner);
        else { picker.Refresh(); ImGui::OpenPopup("Capture target"); }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) picker.Refresh();
    ImGui::TextWrapped("Selected: %s", picker.SelectedLabel().c_str());

    static int selectedPreset = -1;
    const auto& presets = picker.Presets();
    const char* preview = selectedPreset >= 0 && selectedPreset < static_cast<int>(presets.size())
        ? presets[static_cast<std::size_t>(selectedPreset)].name.c_str() : "Choose a saved region";
    ImGui::SetNextItemWidth(260.0F);
    if (ImGui::BeginCombo("Region preset", preview)) {
        for (std::size_t index = 0; index < presets.size(); ++index) {
            const bool selected = selectedPreset == static_cast<int>(index);
            const std::string label = presets[index].name +
                (presets[index].anchorType == RegionAnchorType::WindowClient ? "  [Window]##" : "  [Desktop]##") +
                presets[index].id;
            if (ImGui::Selectable(label.c_str(), selected)) selectedPreset = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply") && selectedPreset >= 0) {
        if (picker.ApplyRegionPreset(static_cast<std::size_t>(selectedPreset), owner)) targetType = static_cast<int>(CaptureTargetType::Region);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save current")) {
        picker.Refresh();
        ImGui::OpenPopup("Save region preset");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selectedPreset >= 0) {
        if (picker.DeleteRegionPreset(static_cast<std::size_t>(selectedPreset))) selectedPreset = -1;
    }
    static char renameName[128]{};
    if (selectedPreset >= 0) {
        if (ImGui::SmallButton("Rename")) {
            std::snprintf(renameName, sizeof(renameName), "%s", presets[static_cast<std::size_t>(selectedPreset)].name.c_str());
            ImGui::OpenPopup("Rename region preset");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicate") && picker.DuplicateRegionPreset(static_cast<std::size_t>(selectedPreset))) ++selectedPreset;
        ImGui::SameLine();
        if (ImGui::ArrowButton("preset-up", ImGuiDir_Up) && picker.MoveRegionPreset(static_cast<std::size_t>(selectedPreset), -1)) --selectedPreset;
        ImGui::SameLine();
        if (ImGui::ArrowButton("preset-down", ImGuiDir_Down) && picker.MoveRegionPreset(static_cast<std::size_t>(selectedPreset), 1)) ++selectedPreset;
    }

    if (ImGui::BeginPopupModal("Rename region preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("New name", renameName, sizeof(renameName));
        if (ImGui::Button("Rename") && selectedPreset >= 0 &&
            picker.RenameRegionPreset(static_cast<std::size_t>(selectedPreset), renameName)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static char presetName[128]{};
    static int presetAnchor = static_cast<int>(RegionAnchorType::VirtualDesktop);
    static int anchorWindow = 0;
    if (ImGui::BeginPopupModal("Save region preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", presetName, sizeof(presetName));
        ImGui::RadioButton("Desktop coordinates", &presetAnchor, static_cast<int>(RegionAnchorType::VirtualDesktop));
        ImGui::SameLine();
        ImGui::RadioButton("Window-relative", &presetAnchor, static_cast<int>(RegionAnchorType::WindowClient));
        if (presetAnchor == static_cast<int>(RegionAnchorType::WindowClient)) {
            const auto& windows = picker.Windows();
            if (anchorWindow >= static_cast<int>(windows.size())) anchorWindow = 0;
            const char* windowPreview = windows.empty() ? "No window available" : windows[static_cast<std::size_t>(anchorWindow)].title.c_str();
            if (ImGui::BeginCombo("Anchor window", windowPreview)) {
                for (std::size_t index = 0; index < windows.size(); ++index) {
                    const std::string label = windows[index].title + "  [" + windows[index].processName + "]##anchor" + std::to_string(index);
                    if (ImGui::Selectable(label.c_str(), anchorWindow == static_cast<int>(index))) anchorWindow = static_cast<int>(index);
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Save")) {
            if (picker.CreateRegionPreset(presetName, static_cast<RegionAnchorType>(presetAnchor),
                                          static_cast<std::size_t>(anchorWindow))) {
                selectedPreset = static_cast<int>(picker.Presets().size()) - 1;
                presetName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!picker.LastError().empty()) ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", picker.LastError().c_str());

    if (ImGui::BeginPopupModal("Capture target", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (targetType == static_cast<int>(CaptureTargetType::Window)) {
            ImGui::TextUnformatted("Choose a visible top-level window");
            ImGui::BeginChild("windows", ImVec2(560.0F, 300.0F), true);
            const auto& windows = picker.Windows();
            Icons().Prune(windows);
            for (std::size_t index = 0; index < windows.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                const std::string label = windows[index].title + "  [" + windows[index].processName + "]##" + std::to_string(index);
                if (auto* icon = Icons().Get(device, windows[index])) {
                    const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(icon));
                    ImGui::Image(ImTextureRef(textureId), ImVec2(24.0F, 24.0F));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0.0F, 24.0F))) {
                    picker.SelectWindow(index);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        } else {
            ImGui::TextUnformatted("Choose a monitor");
            const auto& monitors = picker.Monitors();
            for (std::size_t index = 0; index < monitors.size(); ++index) {
                const auto& monitor = monitors[index];
                const std::string label = monitor.deviceName + "  " +
                    std::to_string(monitor.bounds.right - monitor.bounds.left) + "x" +
                    std::to_string(monitor.bounds.bottom - monitor.bounds.top) +
                    (monitor.primary ? "  (Primary)" : "") + "##" + std::to_string(index);
                if (ImGui::Selectable(label.c_str())) { picker.SelectMonitor(index); ImGui::CloseCurrentPopup(); }
            }
        }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static int format = 0;
    static int fps = 60;
    static int quality = 1;
    constexpr std::array formats{"MKV / H.264", "MP4 / H.264", "MKV / HEVC"};
    constexpr std::array qualities{"Performance", "Balanced", "Quality"};
    ImGui::Combo("Format", &format, formats.data(), static_cast<int>(formats.size()));
    ImGui::SliderInt("FPS", &fps, 15, 120);
    ImGui::Combo("Quality", &quality, qualities.data(), static_cast<int>(qualities.size()));
    command.framesPerSecond = fps;
    command.quality = quality;

    static bool systemAudio = true;
    static bool microphone = false;
    static bool cursor = true;
    ImGui::Checkbox("System audio", &systemAudio);
    ImGui::SameLine();
    ImGui::Checkbox("Microphone", &microphone);
    ImGui::SameLine();
    ImGui::Checkbox("Cursor", &cursor);
    command.systemAudio = systemAudio;
    command.microphone = microphone;
    if (!audioStatus.empty()) {
        ImGui::TextWrapped("Audio: %.*s", static_cast<int>(audioStatus.size()), audioStatus.data());
    }

    ImGui::Spacing();
    if (recording.active) {
        ImGui::Text("Recording pipeline: %s", recording.starting ? "starting" : "active");
    } else if (!capture.IsRunning()) {
        if (ImGui::Button("Test WGC capture", ImVec2(170.0F, 32.0F))) {
            capture.Start(picker.Selected(), device);
        }
    } else if (ImGui::Button("Stop capture test", ImVec2(170.0F, 32.0F))) {
        capture.Stop();
    }
    ImGui::SameLine();
    ImGui::Text("Frames: %llu | queued: %zu | dropped: %llu",
                static_cast<unsigned long long>(capture.FrameCount()), capture.QueuedFrameCount(),
                static_cast<unsigned long long>(capture.DroppedFrameCount()));
    const auto captureError = capture.LastError();
    if (!captureError.empty()) ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", captureError.c_str());
    if (!frameProcessingError.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%.*s",
                           static_cast<int>(frameProcessingError.size()), frameProcessingError.data());
    }

    ImGui::Spacing();
    if (ImGui::Button("Copy capture", ImVec2(120.0F, 40.0F))) command.copyScreenshot = true;
    ImGui::SameLine();
    if (ImGui::Button("Save PNG", ImVec2(105.0F, 40.0F))) command.saveScreenshot = true;
    ImGui::SameLine();
    if (ImGui::Button("Save + copy", ImVec2(120.0F, 40.0F))) command.saveAndCopyScreenshot = true;
    if (!screenshotStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(screenshotStatus.size()), screenshotStatus.data());
    }
    ImGui::Spacing();
    ImGui::Button("GIF", ImVec2(100.0F, 40.0F));
    ImGui::SameLine();
    if (recording.active) {
        if (ImGui::Button("Stop recording", ImVec2(170.0F, 40.0F))) command.stopRecording = true;
    } else if (ImGui::Button("Start recording", ImVec2(170.0F, 40.0F))) {
        command.startRecording = true;
        if (selectedEncoder > 0) command.encoderName = encoderChoices[static_cast<std::size_t>(selectedEncoder - 1)].name;
    }
    if (recording.active || !recording.outputPath.empty()) {
        ImGui::Text("Recorded: %llu frames | %.2f s",
                    static_cast<unsigned long long>(recording.frameCount), recording.elapsedSeconds);
        ImGui::TextWrapped("Output: %.*s", static_cast<int>(recording.outputPath.size()), recording.outputPath.data());
        if (!recording.encoderName.empty()) {
            ImGui::Text("Active encoder: %.*s", static_cast<int>(recording.encoderName.size()), recording.encoderName.data());
        }
    }
    if (!recording.error.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%.*s",
                           static_cast<int>(recording.error.size()), recording.error.data());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Ready | GPU: %.*s", static_cast<int>(gpuName.size()), gpuName.data());
    ImGui::Text("FFmpeg: %.*s", static_cast<int>(ffmpegVersion.size()), ffmpegVersion.data());
    ImGui::TextWrapped("Encoder: %.*s", static_cast<int>(encoderSummary.size()), encoderSummary.data());
    if (!recoveryStatus.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%.*s",
                           static_cast<int>(recoveryStatus.size()), recoveryStatus.data());
    }
    ImGui::TextDisabled("GPU capture, synchronized audio, and recoverable recording are ready.");
    ImGui::End();
    return command;
}

} // namespace opencapture
