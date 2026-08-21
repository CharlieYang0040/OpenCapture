#include "ui/main_panel.h"

#include "core/recording_options.h"
#include "platform/capture_target_picker.h"
#include "platform/windows_graphics_capture.h"

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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

void ExplainLastItem(const char* text, bool allowWhenDisabled = false) {
    const ImGuiHoveredFlags flags = allowWhenDisabled ? ImGuiHoveredFlags_AllowWhenDisabled
                                                       : ImGuiHoveredFlags_None;
    if (ImGui::IsItemHovered(flags)) ImGui::SetTooltip("%s", text);
}

struct MainPanelState {
    int activeTab{};
    bool displaySettingsInitialized{};
    int uiScalePercent{100};
    int selectedEncoder{};
    int targetType{-1};
    bool borderSettingsInitialized{};
    bool borderVisible{true};
    int borderThickness{3};
    int borderOpacity{85};
    bool regionSelectionSettingsInitialized{};
    int outsideDimmingPercent{30};
    int selectedPreset{-1};
    std::array<char, 128> renameName{};
    std::array<char, 128> presetName{};
    int presetAnchor{static_cast<int>(RegionAnchorType::VirtualDesktop)};
    int anchorWindow{};
    int format{};
    int fps{60};
    int quality{1};
    bool systemAudio{true};
    bool microphone{};
    bool recordingSettingsInitialized{};
    bool encoderSelectionPending{};
    bool screenshotDestinationInitialized{};
    int screenshotDestination{};
    bool traySettingsInitialized{};
    bool closeToTray{};
    int gifFpsIndex{2};
    int gifHeightIndex{2};
    int gifColorIndex{3};
};

MainPanelState& PanelState() {
    static MainPanelState state;
    return state;
}

void ApplyRecordingPreferences(MainPanelState& panel, const RecordingPreferences& preferences,
                               bool includeVideo, bool includeGif) {
    if (includeVideo) {
        panel.fps = preferences.framesPerSecond;
        panel.quality = preferences.quality;
        panel.format = preferences.remuxToMp4 ? 1 : 0;
        panel.systemAudio = preferences.systemAudio;
        panel.microphone = preferences.microphone;
        panel.encoderSelectionPending = true;
    }
    if (includeGif) {
        panel.gifFpsIndex = GifFpsIndex(preferences.gifFramesPerSecond);
        panel.gifHeightIndex = GifHeightIndex(preferences.gifHeight);
        panel.gifColorIndex = GifColorIndex(preferences.gifColors);
    }
}

void ResetPanelPreferences(MainPanelState& panel) {
    panel.uiScalePercent = 100;
    panel.borderVisible = true;
    panel.borderThickness = 3;
    panel.borderOpacity = 85;
    panel.outsideDimmingPercent = 30;
    panel.screenshotDestination = 0;
    panel.closeToTray = false;
    panel.selectedEncoder = 0;
    ApplyRecordingPreferences(panel, DefaultRecordingPreferences(), true, true);
    panel.encoderSelectionPending = false;
}

std::string SelectedEncoderName(const MainPanelState& panel,
                                const std::vector<EncoderUiChoice>& encoderChoices) {
    if (panel.selectedEncoder <= 0) return {};
    const auto index = static_cast<std::size_t>(panel.selectedEncoder - 1);
    if (index >= encoderChoices.size()) return {};
    return std::string(encoderChoices[index].name);
}

int EncoderSelectionIndex(const std::vector<EncoderUiChoice>& encoderChoices,
                          std::string_view encoderName) {
    if (encoderName.empty()) return 0;
    for (std::size_t index = 0; index < encoderChoices.size(); ++index) {
        if (encoderChoices[index].name == encoderName) return static_cast<int>(index + 1);
    }
    return 0;
}

} // namespace

MainPanelCommand MainPanel::Draw(std::string_view gpuName, std::string_view ffmpegVersion,
                                 std::string_view encoderSummary,
                                 const std::vector<EncoderUiChoice>& encoderChoices,
                                  std::string_view frameProcessingError,
                                  std::string_view screenshotStatus,
                                  std::string_view targetOverlayStatus,
                                  std::string_view audioStatus,
                                 std::string_view recoveryStatus,
                                 std::string_view remuxStatus,
                                 std::string_view gifStatus,
                                  std::string_view outputDirectory,
                                  const std::vector<RecoverableRecordingUiItem>& recoverableRecordings,
                                   const RecordingUiState& recording,
                                   const RecordingPreferences& recordingPreferences,
                                   const HotkeyUiState& hotkeys,
                                   const BorderUiState& border,
                                   const RegionSelectionUiState& regionSelection,
                                   const DisplayUiState& display,
                                   const ScreenshotUiState& screenshot,
                                   const TrayUiState& tray,
                                   CaptureTargetPicker& picker, WindowsGraphicsCapture& capture,
                                   ID3D11Device* device) {
    MainPanelCommand command{};
    auto& panel = PanelState();
    if (!panel.recordingSettingsInitialized) {
        ApplyRecordingPreferences(panel, recordingPreferences, true, true);
        panel.recordingSettingsInitialized = true;
    }
    if (panel.encoderSelectionPending) {
        panel.selectedEncoder = EncoderSelectionIndex(encoderChoices, recordingPreferences.encoderName);
        if (recordingPreferences.encoderName.empty() || panel.selectedEncoder > 0 ||
            !encoderChoices.empty()) {
            panel.encoderSelectionPending = false;
        }
    }
    const bool mediaBusy = recording.mediaJobActive;
    const bool outputBusy = recording.active || mediaBusy;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OpenCapture", nullptr, flags);

    ImGui::TextUnformatted("OpenCapture");
    ImGui::SameLine();
    if (ImGui::Button("Quick Capture")) command.quickCapture = true;
    ExplainLastItem("Temporarily select an area and capture it without changing the saved target.");
    ImGui::TextWrapped("Target: %s", picker.SelectedLabel().c_str());
    ImGui::TextWrapped("Output: %.*s",
                       static_cast<int>(outputDirectory.size()), outputDirectory.data());
    if (recording.active) {
        ImGui::SameLine();
        if (ImGui::Button(recording.paused ? "Resume" : "Pause")) {
            if (recording.paused) command.resumeRecording = true;
            else command.pauseRecording = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(recording.gif ? "Stop GIF" : "Stop video")) {
            command.stopRecording = true;
        }
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainSections")) {
        if (ImGui::BeginTabItem("Capture")) {
            panel.activeTab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Video")) {
            panel.activeTab = 1;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("GIF")) {
            panel.activeTab = 2;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            panel.activeTab = 3;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (!panel.displaySettingsInitialized) {
        panel.uiScalePercent = display.userScalePercent;
        panel.displaySettingsInitialized = true;
    }
    if (panel.activeTab == 3) {
    ImGui::SeparatorText("Display");
    ImGui::Text("Windows scaling: %d%% | Effective UI: %d%%",
                display.windowsDpiPercent, display.effectiveScalePercent);
    if (!display.status.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(display.status.size()), display.status.data());
    }
    ImGui::SliderInt("Additional UI scale", &panel.uiScalePercent, 75, 200, "%d%%");
    ExplainLastItem("Multiplies the Windows monitor scaling. Use 100% for automatic sizing.");
    if (ImGui::SmallButton("Apply UI scale")) {
        command.applyUiScale = true;
        command.uiScalePercent = panel.uiScalePercent;
    }
    ExplainLastItem("Apply immediately and save the UI size for the next launch.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset UI scale")) {
        panel.uiScalePercent = 100;
        command.resetUiScale = true;
    }
    ExplainLastItem("Follow Windows display scaling without an additional adjustment.");
    ImGui::Spacing();
    }

    if (panel.selectedEncoder > static_cast<int>(encoderChoices.size())) panel.selectedEncoder = 0;
    const char* encoderPreview = "Auto (recommended)";
    if (panel.selectedEncoder > 0) encoderPreview = encoderChoices[static_cast<std::size_t>(panel.selectedEncoder - 1)].displayName.data();
    if (panel.activeTab == 1 || panel.activeTab == 2) {
    ImGui::TextUnformatted("Video encoder");
    ExplainLastItem("Auto selects the best available hardware encoder. A manual choice is useful for compatibility testing.");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##VideoEncoder", encoderPreview)) {
        if (ImGui::Selectable("Auto (recommended)", panel.selectedEncoder == 0)) {
            panel.selectedEncoder = 0;
            command.applyRecordingSettings = true;
        }
        for (std::size_t index = 0; index < encoderChoices.size(); ++index) {
            const auto& choice = encoderChoices[index];
            if (!choice.usable) ImGui::BeginDisabled();
            const bool selected = panel.selectedEncoder == static_cast<int>(index + 1);
            if (ImGui::Selectable(choice.displayName.data(), selected) && choice.usable) {
                panel.selectedEncoder = static_cast<int>(index + 1);
                command.applyRecordingSettings = true;
            }
            if (ImGui::IsItemHovered() && !choice.detail.empty()) ImGui::SetTooltip("%.*s", static_cast<int>(choice.detail.size()), choice.detail.data());
            if (!choice.usable) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    }

    if (panel.activeTab == 0) {
    if (panel.targetType < 0) panel.targetType = static_cast<int>(picker.Selected().type);
    ImGui::TextUnformatted("Target");
    ImGui::SameLine();
    ImGui::RadioButton("Window", &panel.targetType, 0);
    ExplainLastItem("Capture one window and keep the target border attached while it moves or resizes.");
    ImGui::SameLine();
    ImGui::RadioButton("Region", &panel.targetType, 1);
    ExplainLastItem("Capture only a rectangular desktop area. The selected area remains outlined.");
    ImGui::SameLine();
    ImGui::RadioButton("Monitor", &panel.targetType, 2);
    ExplainLastItem("Capture one entire monitor.");
    if (ImGui::Button("Select capture target", ImVec2(220.0F, 0.0F))) {
        if (panel.targetType == static_cast<int>(CaptureTargetType::Region)) command.selectRegion = true;
        else { picker.Refresh(); ImGui::OpenPopup("Capture target"); }
    }
    ExplainLastItem("Choose the window, region, or monitor used by screenshots, video, and GIF recording.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) picker.Refresh();
    ExplainLastItem("Refresh the list of currently available windows and monitors.");
    ImGui::TextWrapped("Selected: %s", picker.SelectedLabel().c_str());
    if (!targetOverlayStatus.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%.*s",
                           static_cast<int>(targetOverlayStatus.size()), targetOverlayStatus.data());
    }
    }

    if (panel.activeTab == 3) {
    ImGui::Spacing();
    ImGui::SeparatorText("Background");
    if (!panel.traySettingsInitialized) {
        panel.closeToTray = tray.closeToTray;
        panel.traySettingsInitialized = true;
    } else if (panel.closeToTray != tray.closeToTray) {
        panel.closeToTray = tray.closeToTray;
    }
    if (!tray.available) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Keep running when the window is closed", &panel.closeToTray)) {
        command.applyCloseToTray = true;
        command.closeToTray = panel.closeToTray;
    }
    ExplainLastItem("Keep global shortcuts available from the notification area after closing the main window.",
                    !tray.available);
    if (!tray.available) ImGui::EndDisabled();
    if (!tray.status.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(tray.status.size()), tray.status.data());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Target border");
    if (!panel.borderSettingsInitialized) {
        panel.borderVisible = border.visible;
        panel.borderThickness = border.thickness;
        panel.borderOpacity = border.opacityPercent;
        panel.borderSettingsInitialized = true;
    }
    ImGui::Checkbox("Show target border", &panel.borderVisible);
    ExplainLastItem("Show or hide the always-on-top border around the selected target.");
    ImGui::SliderInt("Border thickness", &panel.borderThickness, 1, 12, "%d px");
    ExplainLastItem("Use a thin border when you want the target guide to stay unobtrusive.");
    ImGui::SliderInt("Border opacity", &panel.borderOpacity, 20, 100, "%d%%");
    ExplainLastItem("Lower opacity makes the border less distracting while keeping the target visible.");
    if (ImGui::SmallButton("Apply border settings")) {
        command.applyBorderSettings = true;
        command.borderVisible = panel.borderVisible;
        command.borderThickness = panel.borderThickness;
        command.borderOpacityPercent = panel.borderOpacity;
    }
    ExplainLastItem("Apply and save these settings for the next app launch.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset border defaults")) {
        panel.borderVisible = true;
        panel.borderThickness = 3;
        panel.borderOpacity = 85;
        command.resetBorderSettings = true;
    }
    ExplainLastItem("Restore visible, 3 px, and 85% opacity.");

    ImGui::Spacing();
    ImGui::SeparatorText("Region selection appearance");
    if (!panel.regionSelectionSettingsInitialized) {
        panel.outsideDimmingPercent = regionSelection.outsideDimmingPercent;
        panel.regionSelectionSettingsInitialized = true;
    }
    ImGui::SliderInt("Selection outside dimming", &panel.outsideDimmingPercent, 0, 70, "%d%%");
    ExplainLastItem("Dims only the area outside a region selection. The selected area stays clear.");
    if (ImGui::SmallButton("Apply selection appearance")) {
        command.applyRegionSelectionSettings = true;
        command.regionOutsideDimmingPercent = panel.outsideDimmingPercent;
    }
    ExplainLastItem("Apply and save the region selection screen brightness.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset selection appearance")) {
        panel.outsideDimmingPercent = 30;
        command.resetRegionSelectionSettings = true;
    }
    ExplainLastItem("Restore 30% outside dimming.");
    }

    if (panel.activeTab == 0) {
    const auto& presets = picker.Presets();
    const char* preview = panel.selectedPreset >= 0 && panel.selectedPreset < static_cast<int>(presets.size())
        ? presets[static_cast<std::size_t>(panel.selectedPreset)].name.c_str() : "Choose a saved region";
    ImGui::SetNextItemWidth(260.0F);
    if (ImGui::BeginCombo("Region preset", preview)) {
        for (std::size_t index = 0; index < presets.size(); ++index) {
            const bool selected = panel.selectedPreset == static_cast<int>(index);
            const std::string label = presets[index].name +
                (presets[index].anchorType == RegionAnchorType::WindowClient ? "  [Window]##" : "  [Desktop]##") +
                presets[index].id;
            if (ImGui::Selectable(label.c_str(), selected)) panel.selectedPreset = static_cast<int>(index);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply") && panel.selectedPreset >= 0) {
        if (picker.ApplyRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) {
            panel.targetType = static_cast<int>(CaptureTargetType::Region);
        }
    }
    ExplainLastItem("Immediately use the selected saved region as the current capture target.", true);
    ImGui::SameLine();
    if (ImGui::Button("Save current")) {
        picker.Refresh();
        ImGui::OpenPopup("Save region preset");
    }
    ExplainLastItem("Save the current region for repeated browser or application captures.");
    ImGui::SameLine();
    if (ImGui::Button("Delete") && panel.selectedPreset >= 0) {
        if (picker.DeleteRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) panel.selectedPreset = -1;
    }
    ExplainLastItem("Delete the selected saved region. This does not delete captured files.", true);
    if (panel.selectedPreset >= 0) {
        if (ImGui::SmallButton("Rename")) {
            std::snprintf(panel.renameName.data(), panel.renameName.size(), "%s",
                          presets[static_cast<std::size_t>(panel.selectedPreset)].name.c_str());
            ImGui::OpenPopup("Rename region preset");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicate") && picker.DuplicateRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) ++panel.selectedPreset;
        ImGui::SameLine();
        if (ImGui::ArrowButton("preset-up", ImGuiDir_Up) && picker.MoveRegionPreset(static_cast<std::size_t>(panel.selectedPreset), -1)) --panel.selectedPreset;
        ImGui::SameLine();
        if (ImGui::ArrowButton("preset-down", ImGuiDir_Down) && picker.MoveRegionPreset(static_cast<std::size_t>(panel.selectedPreset), 1)) ++panel.selectedPreset;
    }

    if (ImGui::BeginPopupModal("Rename region preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("New name", panel.renameName.data(), panel.renameName.size());
        if (ImGui::Button("Rename") && panel.selectedPreset >= 0 &&
            picker.RenameRegionPreset(static_cast<std::size_t>(panel.selectedPreset), panel.renameName.data())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Save region preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", panel.presetName.data(), panel.presetName.size());
        ImGui::RadioButton("Desktop coordinates", &panel.presetAnchor, static_cast<int>(RegionAnchorType::VirtualDesktop));
        ImGui::SameLine();
        ImGui::RadioButton("Window-relative", &panel.presetAnchor, static_cast<int>(RegionAnchorType::WindowClient));
        if (panel.presetAnchor == static_cast<int>(RegionAnchorType::WindowClient)) {
            const auto& windows = picker.Windows();
            if (panel.anchorWindow >= static_cast<int>(windows.size())) panel.anchorWindow = 0;
            const char* windowPreview = windows.empty() ? "No window available" : windows[static_cast<std::size_t>(panel.anchorWindow)].title.c_str();
            if (ImGui::BeginCombo("Anchor window", windowPreview)) {
                for (std::size_t index = 0; index < windows.size(); ++index) {
                    const std::string label = windows[index].title + "  [" + windows[index].processName + "]##anchor" + std::to_string(index);
                    if (ImGui::Selectable(label.c_str(), panel.anchorWindow == static_cast<int>(index))) panel.anchorWindow = static_cast<int>(index);
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Save")) {
            if (picker.CreateRegionPreset(panel.presetName.data(), static_cast<RegionAnchorType>(panel.presetAnchor),
                                          static_cast<std::size_t>(panel.anchorWindow))) {
                panel.selectedPreset = static_cast<int>(picker.Presets().size()) - 1;
                panel.presetName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!picker.LastError().empty()) ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", picker.LastError().c_str());

    if (ImGui::BeginPopupModal("Capture target", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (panel.targetType == static_cast<int>(CaptureTargetType::Window)) {
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
    }

    constexpr std::array formats{"MKV / H.264", "MP4 copy / H.264"};
    constexpr std::array qualities{"Performance", "Balanced", "Quality"};
    if (panel.activeTab == 1) {
    if (ImGui::Combo("Format", &panel.format, formats.data(), static_cast<int>(formats.size()))) {
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("MKV is written safely while recording. MP4 copy remuxes the completed MKV without re-encoding.");
    ImGui::SliderInt("FPS", &panel.fps, 15, 120);
    if (ImGui::IsItemDeactivatedAfterEdit()) command.applyRecordingSettings = true;
    ExplainLastItem("Higher FPS makes motion smoother but increases encoder load and file size.");
    if (ImGui::Combo("Quality", &panel.quality, qualities.data(), static_cast<int>(qualities.size()))) {
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("Performance reduces recording overhead; Quality uses a higher bitrate and creates larger files.");
    command.framesPerSecond = panel.fps;
    command.quality = panel.quality;
    command.remuxToMp4 = panel.format == 1;

    if (ImGui::Checkbox("System audio", &panel.systemAudio)) command.applyRecordingSettings = true;
    ExplainLastItem("Include sound played by Windows applications in video recordings. GIF never includes audio.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Microphone", &panel.microphone)) command.applyRecordingSettings = true;
    ExplainLastItem("Include the default microphone in video recordings. GIF never includes audio.");
    ImGui::SameLine();
    bool cursorIncluded = true;
    ImGui::BeginDisabled();
    ImGui::Checkbox("Cursor (always included)", &cursorIncluded);
    ImGui::EndDisabled();
    ExplainLastItem(
        "Windows Graphics Capture currently includes the pointer. On/off control will be enabled only after the capture engine supports and verifies it.",
        true);
    command.systemAudio = panel.systemAudio;
    command.microphone = panel.microphone;
    if (ImGui::SmallButton("Restore Default")) {
        ApplyRecordingPreferences(panel, DefaultRecordingPreferences(), true, false);
        panel.selectedEncoder = 0;
        panel.encoderSelectionPending = false;
        command.resetVideoSettings = true;
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("Restore 60 FPS, balanced quality, MKV, Auto encoder, system audio on, and microphone off.");
    if (!audioStatus.empty()) {
        ImGui::TextWrapped("Audio: %.*s", static_cast<int>(audioStatus.size()), audioStatus.data());
    }
    }
    command.framesPerSecond = panel.fps;
    command.quality = panel.quality;
    command.remuxToMp4 = panel.format == 1;
    command.systemAudio = panel.systemAudio;
    command.microphone = panel.microphone;
    command.encoderName = SelectedEncoderName(panel, encoderChoices);
    command.gifFramesPerSecond = kGifFpsChoices[static_cast<std::size_t>(
        std::clamp(panel.gifFpsIndex, 0, static_cast<int>(kGifFpsChoices.size()) - 1))];
    command.gifHeight = kGifHeightChoices[static_cast<std::size_t>(
        std::clamp(panel.gifHeightIndex, 0, static_cast<int>(kGifHeightChoices.size()) - 1))];
    command.gifColors = kGifColorChoices[static_cast<std::size_t>(
        std::clamp(panel.gifColorIndex, 0, static_cast<int>(kGifColorChoices.size()) - 1))];

    if (panel.activeTab == 3) {
    ImGui::Spacing();
    ImGui::SeparatorText("Output");
    ImGui::TextWrapped("Screenshots, video recordings, and GIF files are saved in the same folder.");
    ImGui::TextWrapped("Folder: %.*s", static_cast<int>(outputDirectory.size()), outputDirectory.data());
    if (outputBusy) ImGui::BeginDisabled();
    if (ImGui::Button("Change output folder...", ImVec2(210.0F, 32.0F))) {
        command.chooseOutputDirectory = true;
    }
    if (!outputBusy) ExplainLastItem("Choose the shared folder used for PNG, video, GIF, and conversion outputs.");
    if (outputBusy) {
        ImGui::EndDisabled();
        ExplainLastItem("Stop recording or wait for media conversion before changing the output folder.", true);
    }

    if (!recoverableRecordings.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Recording recovery");
        ImGui::TextWrapped("Validate an incomplete MKV and finalize it without overwriting an existing recording.");
        for (std::size_t index = 0; index < recoverableRecordings.size(); ++index) {
            const auto& item = recoverableRecordings[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("%s (%.1f MiB)", item.fileName.c_str(),
                        static_cast<double>(item.sizeBytes) / (1024.0 * 1024.0));
            ImGui::SameLine();
            if (outputBusy) ImGui::BeginDisabled();
            if (ImGui::SmallButton("Recover")) command.recoverRecordingIndex = static_cast<int>(index);
            if (outputBusy) ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Global shortcuts");
    ImGui::TextWrapped("Click Set shortcut, then press the key combination you want. Shortcuts require Ctrl, Alt, or Shift.");
    constexpr std::array hotkeyActionNames{
        "Capture selected target",
        "Start / stop video",
        "Start / stop GIF",
        "Quick Capture",
    };
    for (std::size_t action = 0; action < hotkeyActionNames.size(); ++action) {
        ImGui::PushID(static_cast<int>(action));
        const bool capturing = hotkeys.capturingAction == static_cast<int>(action);
        ImGui::Text("%s", hotkeyActionNames[action]);
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", hotkeys.labels[action].c_str());
        if (capturing) {
            ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.35F, 1.0F),
                               "Press a key combination... Esc cancels.");
        }
        if (ImGui::SmallButton(capturing ? "Listening..." : "Set shortcut")) {
            if (capturing) command.cancelHotkeyCapture = true;
            else command.listenHotkeyAction = static_cast<int>(action);
        }
        ExplainLastItem(capturing
                            ? "Press Esc or click again to cancel without changing the shortcut."
                            : "Capture the next key combination, including letters, numbers, and function keys.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) command.clearHotkeyAction = static_cast<int>(action);
        ExplainLastItem("Remove this global shortcut until you assign a new one.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore Default")) {
            command.resetHotkeyAction = static_cast<int>(action);
        }
        ExplainLastItem("Restore this action to its original OpenCapture shortcut.");
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Restore default shortcuts")) {
        command.resetHotkeys = true;
        command.cancelHotkeyCapture = true;
    }
    ExplainLastItem("Restore Ctrl+Shift+F9 screenshot, F10 video, F11 GIF, and F8 Quick Capture.");
    if (!hotkeys.error.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F), "%.*s",
                           static_cast<int>(hotkeys.error.size()), hotkeys.error.data());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Reset");
    if (ImGui::Button("Restore Default")) {
        ResetPanelPreferences(panel);
        command.resetAllSettings = true;
        command.cancelHotkeyCapture = true;
    }
    ExplainLastItem("Restore shortcuts, video, GIF, screenshot, display, border, region selection, and background settings.");

    ImGui::Spacing();
    ImGui::SeparatorText("Diagnostics");
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
    }

    if (panel.activeTab == 0) {
    ImGui::Spacing();
    ImGui::SeparatorText("Screenshot");
    ImGui::TextUnformatted("Capture one still image from the selected target.");
    if (!panel.screenshotDestinationInitialized) {
        panel.screenshotDestination =
            screenshot.shortcutDestination == ScreenshotDestination::File ? 1 :
            screenshot.shortcutDestination == ScreenshotDestination::FileAndClipboard ? 2 : 0;
        panel.screenshotDestinationInitialized = true;
    }
    constexpr std::array shortcutDestinationLabels{
        "Clipboard only", "Save PNG file", "Save PNG + clipboard",
    };
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::Combo("Screenshot shortcut result", &panel.screenshotDestination,
                     shortcutDestinationLabels.data(),
                     static_cast<int>(shortcutDestinationLabels.size()))) {
        command.applyScreenshotShortcutDestination = true;
        command.screenshotShortcutDestination =
            panel.screenshotDestination == 1 ? ScreenshotDestination::File :
            panel.screenshotDestination == 2 ? ScreenshotDestination::FileAndClipboard :
                                         ScreenshotDestination::Clipboard;
    }
    ExplainLastItem("Used by the selected-target screenshot shortcut and Quick Capture.");
    const bool stackScreenshotButtons = ImGui::GetContentRegionAvail().x < 500.0F;
    const float screenshotButtonWidth = stackScreenshotButtons ? -1.0F : 155.0F;
    if (ImGui::Button("Copy to clipboard", ImVec2(screenshotButtonWidth, 42.0F))) {
        command.copyScreenshot = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the screenshot only. No file is created.");
    if (!stackScreenshotButtons) ImGui::SameLine();
    if (ImGui::Button("Save PNG file",
                      ImVec2(stackScreenshotButtons ? -1.0F : 140.0F, 42.0F))) {
        command.saveScreenshot = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save a PNG in the output folder.");
    if (!stackScreenshotButtons) ImGui::SameLine();
    if (ImGui::Button("Save PNG + copy", ImVec2(screenshotButtonWidth, 42.0F))) {
        command.saveAndCopyScreenshot = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save a PNG and also copy it to the clipboard.");
    if (!screenshotStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(screenshotStatus.size()), screenshotStatus.data());
    }
    }

    if (panel.activeTab == 1 || (panel.activeTab == 2 && recording.active)) {
    ImGui::Spacing();
    ImGui::SeparatorText(recording.gif ? "GIF recording" : "Video");
    ImGui::TextUnformatted(recording.gif
        ? "Recording a silent, size-controlled GIF source from the selected target."
        : "Record the selected target with the FPS, quality, and audio options above.");
    if (recording.active) {
        if (recording.paused) {
            if (ImGui::Button(recording.gif ? "Resume GIF recording" : "Resume video recording", ImVec2(205.0F, 44.0F))) {
                command.resumeRecording = true;
            }
            ExplainLastItem("Continue the current recording in the same output file.");
        } else if (ImGui::Button(recording.gif ? "Pause GIF recording" : "Pause video recording", ImVec2(205.0F, 44.0F))) {
            command.pauseRecording = true;
        }
        if (!recording.paused) ExplainLastItem("Pause frame and audio writing without finalizing the output file.");
        ImGui::SameLine();
        if (ImGui::Button(recording.gif ? "Stop and create GIF" : "Stop video recording", ImVec2(205.0F, 44.0F))) {
            command.stopRecording = true;
        }
        ExplainLastItem(recording.gif
                            ? "Stop recording, keep the safe MKV source, and create the optimized GIF."
                            : "Finalize the current video so it can be opened by a media player.");
    } else {
        if (mediaBusy) ImGui::BeginDisabled();
        if (ImGui::Button("Start video recording", ImVec2(205.0F, 44.0F))) {
            command.startRecording = true;
            if (panel.selectedEncoder > 0) command.encoderName = encoderChoices[static_cast<std::size_t>(panel.selectedEncoder - 1)].name;
        }
        if (mediaBusy) ImGui::EndDisabled();
        ExplainLastItem(mediaBusy
                            ? "Wait for the current GIF conversion to finish or cancel it first."
                            : "Start recording the selected target with the video, quality, and audio settings above.",
                        mediaBusy);
    }
    if (recording.active || !recording.outputPath.empty()) {
        ImGui::Text("Encoded: %llu | source: %llu | skipped ticks: %llu | %.2f s",
                    static_cast<unsigned long long>(recording.frameCount),
                    static_cast<unsigned long long>(recording.sourceFrameCount),
                    static_cast<unsigned long long>(recording.skippedFrameTicks),
                    recording.elapsedSeconds);
        ExplainLastItem("Skipped ticks are timeline slots the recorder did not encode while it was late. "
                        "They expose real capture or encoder stalls instead of hiding them with duplicate-frame bursts.");
        const double sourceFps = recording.elapsedSeconds > 0.0
            ? static_cast<double>(recording.sourceFrameCount) / recording.elapsedSeconds : 0.0;
        ImGui::Text("Source: %.1f fps | max gap: %.1f ms | capture drops: %llu | mux peak: %zu",
                    sourceFps, recording.maximumSourceGapMilliseconds,
                    static_cast<unsigned long long>(recording.captureDroppedFrameCount),
                    recording.muxQueuePeak);
        ExplainLastItem("A growing mux peak points to slow storage. Capture drops or a large source gap point "
                        "to capture/GPU scheduling pressure before encoding.");
        ImGui::TextWrapped("Output: %.*s", static_cast<int>(recording.outputPath.size()), recording.outputPath.data());
        if (!recording.encoderName.empty()) {
            ImGui::Text("Active encoder: %.*s", static_cast<int>(recording.encoderName.size()), recording.encoderName.data());
        }
    }
    if (!recording.active && !mediaBusy && recording.canRemux) {
        if (ImGui::Button("Create MP4 copy", ImVec2(180.0F, 34.0F))) {
            command.remuxLastRecording = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remux the last MKV without re-encoding. The source MKV is kept.");
        }
    }
    if (!remuxStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(remuxStatus.size()), remuxStatus.data());
    }
    }
    constexpr std::array gifFpsValues{6, 10, 12, 15, 20, 30};
    constexpr std::array gifFpsLabels{"6 fps", "10 fps", "12 fps", "15 fps", "20 fps", "30 fps"};
    constexpr std::array gifHeightValues{360, 480, 720, 1080};
    constexpr std::array gifHeightLabels{"360p", "480p", "720p", "1080p"};
    constexpr std::array gifColorValues{64, 128, 192, 256};
    constexpr std::array gifColorLabels{"64 colors", "128 colors", "192 colors", "256 colors"};
    if (panel.activeTab == 2) {
    ImGui::Spacing();
    ImGui::SeparatorText("GIF size controls");
    ImGui::TextWrapped("Lower resolution, FPS, and color count create much smaller GIF files. Recording stops automatically at 30 seconds or the safe pixel budget.");
    if (outputBusy) ImGui::BeginDisabled();
    if (ImGui::Combo("GIF FPS", &panel.gifFpsIndex, gifFpsLabels.data(), static_cast<int>(gifFpsLabels.size()))) {
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("Lower FPS greatly reduces GIF size. 12 fps is the recommended default.", outputBusy);
    if (ImGui::Combo("GIF resolution", &panel.gifHeightIndex, gifHeightLabels.data(), static_cast<int>(gifHeightLabels.size()))) {
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("Limits output height while preserving aspect ratio. Lower resolution creates a much smaller GIF.", outputBusy);
    if (ImGui::Combo("GIF colors", &panel.gifColorIndex, gifColorLabels.data(), static_cast<int>(gifColorLabels.size()))) {
        command.applyRecordingSettings = true;
    }
    ExplainLastItem("Fewer palette colors reduce file size but may introduce visible banding.", outputBusy);
    if (outputBusy) ImGui::EndDisabled();
    if (!outputBusy && ImGui::SmallButton("Restore Default")) {
        ApplyRecordingPreferences(panel, DefaultRecordingPreferences(), false, true);
        command.resetGifSettings = true;
        command.applyRecordingSettings = true;
    }
    if (!outputBusy) {
        ExplainLastItem("Restore 12 fps, 720p, and 256 colors.");
    }
    command.gifFramesPerSecond = gifFpsValues[static_cast<std::size_t>(panel.gifFpsIndex)];
    command.gifHeight = gifHeightValues[static_cast<std::size_t>(panel.gifHeightIndex)];
    command.gifColors = gifColorValues[static_cast<std::size_t>(panel.gifColorIndex)];
    if (command.gifHeight >= 1080 || command.gifFramesPerSecond >= 30) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F),
                           "Large GIF warning: use 720p / 12 fps or lower for sharing.");
    }
    if (!outputBusy &&
        ImGui::Button("Start GIF recording", ImVec2(205.0F, 44.0F))) {
        command.startGif = true;
        if (panel.selectedEncoder > 0) {
            command.encoderName =
                encoderChoices[static_cast<std::size_t>(panel.selectedEncoder - 1)].name;
        }
    }
    if (!outputBusy && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Records without audio, then creates an optimized GIF.");
    }
    if (mediaBusy) {
        ImGui::ProgressBar(static_cast<float>(std::clamp(recording.mediaProgress, 0.0, 1.0)),
                           ImVec2(-1.0F, 0.0F), "Creating GIF...");
        if (recording.mediaCancelRequested) ImGui::BeginDisabled();
        if (ImGui::Button(recording.mediaCancelRequested ? "Cancelling..." : "Cancel GIF conversion",
                          ImVec2(205.0F, 36.0F))) {
            command.cancelMediaJob = true;
        }
        if (recording.mediaCancelRequested) ImGui::EndDisabled();
    }
    if (!gifStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(gifStatus.size()), gifStatus.data());
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
