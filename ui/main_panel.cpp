#include "ui/main_panel.h"

#include "core/recording_options.h"
#include "platform/capture_target_picker.h"
#include "platform/windows_graphics_capture.h"
#include "ui/i18n.h"
#include "ui/tooltips.h"

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

void Explain(Msg title, Msg body, bool allowWhenDisabled = false) {
    ExplainLastItem(Tr(title), Tr(body), allowWhenDisabled);
}

constexpr std::array kProfileUiOrder{
    RecordingProfile::Compatibility,
    RecordingProfile::Balanced,
    RecordingProfile::Compact,
    RecordingProfile::Quality,
    RecordingProfile::Custom,
};

constexpr std::array kProfileTitles{
    Msg::profile_game, Msg::profile_balanced, Msg::profile_small, Msg::profile_quality,
    Msg::profile_custom,
};

constexpr std::array kProfileDetails{
    Msg::profile_game_detail, Msg::profile_balanced_detail, Msg::profile_small_detail,
    Msg::profile_quality_detail, Msg::profile_custom_detail,
};

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
    int recordingProfile{static_cast<int>(RecordingProfile::Compatibility)};
    int videoCodec{static_cast<int>(VideoCodecPreference::H264)};
    int videoResolution{static_cast<int>(VideoResolutionLimit::Height1080)};
    int encoderEfficiency{static_cast<int>(EncoderEfficiencyMode::Realtime)};
    int customBitRateMbps{10};
    bool allowCodecFallback{true};
    bool useCustomBitRate{};
    bool systemAudio{true};
    bool microphone{};
    bool recordingSettingsInitialized{};
    bool encoderSelectionPending{};
    bool screenshotDestinationInitialized{};
    int screenshotDestination{};
    bool screenshotProfileInitialized{};
    int screenshotProfile{static_cast<int>(ScreenshotProfile::WebpBalanced)};
    bool traySettingsInitialized{};
    bool closeToTray{};
    bool openTargetPicker{};
    bool openSaveRegion{};
    bool openRenameRegion{};
    bool quickCapturePopupWasOpen{};
    int gifFpsIndex{2};
    int gifHeightIndex{2};
    int gifColorIndex{3};
    int animationFormat{static_cast<int>(AnimationFormat::WebP)};
    int animationProfile{static_cast<int>(AnimationProfile::Balanced)};
    int animationQuality{82};
    int avifCrf{34};
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
        panel.recordingProfile = static_cast<int>(preferences.profile);
        panel.videoCodec = static_cast<int>(preferences.codec);
        panel.videoResolution = static_cast<int>(preferences.resolution);
        panel.encoderEfficiency = static_cast<int>(preferences.efficiency);
        panel.customBitRateMbps = preferences.customBitRateMbps;
        panel.allowCodecFallback = preferences.allowCodecFallback;
        panel.useCustomBitRate = preferences.useCustomBitRate;
        panel.encoderSelectionPending = true;
    }
    if (includeGif) {
        panel.gifFpsIndex = GifFpsIndex(preferences.gifFramesPerSecond);
        panel.gifHeightIndex = GifHeightIndex(preferences.gifHeight);
        panel.gifColorIndex = GifColorIndex(preferences.gifColors);
        panel.animationFormat = static_cast<int>(preferences.animationFormat);
        panel.animationProfile = static_cast<int>(preferences.animationProfile);
        panel.animationQuality = preferences.animationQuality;
        panel.avifCrf = preferences.avifCrf;
    }
}

void ResetPanelPreferences(MainPanelState& panel) {
    panel.uiScalePercent = 100;
    panel.borderVisible = true;
    panel.borderThickness = 3;
    panel.borderOpacity = 85;
    panel.outsideDimmingPercent = 30;
    panel.screenshotDestination = 0;
    panel.screenshotProfile = static_cast<int>(ScreenshotProfile::WebpBalanced);
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

bool EncoderChoiceVisible(const EncoderUiChoice& choice, int codec) {
    return codec == static_cast<int>(VideoCodecPreference::Auto) ||
           choice.codec == static_cast<VideoCodecPreference>(codec);
}

void CopyRecordingCommand(MainPanelCommand& command, const MainPanelState& panel,
                          const std::vector<EncoderUiChoice>& encoderChoices) {
    command.framesPerSecond = panel.fps;
    command.quality = panel.quality;
    command.remuxToMp4 = panel.format == 1;
    command.systemAudio = panel.systemAudio;
    command.microphone = panel.microphone;
    command.recordingProfile = static_cast<RecordingProfile>(panel.recordingProfile);
    command.videoCodec = static_cast<VideoCodecPreference>(panel.videoCodec);
    command.videoResolution = static_cast<VideoResolutionLimit>(panel.videoResolution);
    command.encoderEfficiency = static_cast<EncoderEfficiencyMode>(panel.encoderEfficiency);
    command.customBitRateMbps = panel.customBitRateMbps;
    command.allowCodecFallback = panel.allowCodecFallback;
    command.useCustomBitRate = panel.useCustomBitRate;
    command.encoderName = SelectedEncoderName(panel, encoderChoices);
    command.gifFramesPerSecond = kGifFpsChoices[static_cast<std::size_t>(
        std::clamp(panel.gifFpsIndex, 0, static_cast<int>(kGifFpsChoices.size()) - 1))];
    command.gifHeight = kGifHeightChoices[static_cast<std::size_t>(
        std::clamp(panel.gifHeightIndex, 0, static_cast<int>(kGifHeightChoices.size()) - 1))];
    command.gifColors = kGifColorChoices[static_cast<std::size_t>(
        std::clamp(panel.gifColorIndex, 0, static_cast<int>(kGifColorChoices.size()) - 1))];
    command.animationFormat = static_cast<AnimationFormat>(panel.animationFormat);
    command.animationProfile = static_cast<AnimationProfile>(panel.animationProfile);
    command.animationQuality = panel.animationQuality;
    command.avifCrf = panel.avifCrf;
}

ImVec4 StatusColor(const RecordingUiState& recording) {
    if (!recording.error.empty()) return ImVec4(1.0F, 0.40F, 0.35F, 1.0F);
    if (recording.paused) return ImVec4(1.0F, 0.55F, 0.25F, 1.0F);
    if (recording.active) return ImVec4(1.0F, 0.82F, 0.25F, 1.0F);
    if (recording.mediaJobActive) return ImVec4(0.45F, 0.75F, 1.0F, 1.0F);
    return ImVec4(0.40F, 0.78F, 1.0F, 1.0F);
}

const char* StatusLabel(const RecordingUiState& recording) {
    if (!recording.error.empty()) return Tr(Msg::status_error);
    if (recording.paused) return Tr(Msg::status_paused);
    if (recording.active && recording.gif) return Tr(Msg::status_gif);
    if (recording.active && recording.starting) return Tr(Msg::status_starting);
    if (recording.active) return Tr(Msg::status_recording);
    if (recording.mediaJobActive) return Tr(Msg::status_converting);
    return Tr(Msg::status_ready);
}

void DrawLanguageToggle(MainPanelCommand& command) {
    const bool korean = CurrentLanguage() == Language::Korean;
    if (korean) ImGui::BeginDisabled();
    if (ImGui::SmallButton(Tr(Msg::language_ko_short))) {
        command.applyLanguage = true;
        command.language = Language::Korean;
    }
    if (korean) ImGui::EndDisabled();
    Explain(Msg::tooltip_language_title, Msg::tooltip_language_body);
    ImGui::SameLine();
    if (!korean) ImGui::BeginDisabled();
    if (ImGui::SmallButton(Tr(Msg::language_en_short))) {
        command.applyLanguage = true;
        command.language = Language::English;
    }
    if (!korean) ImGui::EndDisabled();
    Explain(Msg::tooltip_language_title, Msg::tooltip_language_body);
}

void DrawHeader(const RecordingUiState& recording) {
    ImGui::TextUnformatted(Tr(Msg::app_name));
    ImGui::SameLine();
    ImGui::TextColored(StatusColor(recording), "%s", StatusLabel(recording));
    if (recording.active) {
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f s", recording.elapsedSeconds);
    }
}

void DrawSourceBar(MainPanelState& panel, MainPanelCommand& command,
                   const RecordingUiState& recording, std::string_view outputDirectory,
                   std::string_view targetOverlayStatus, CaptureTargetPicker& picker) {
    BeginCard("global-source");
    ImGui::Text("%s", Tr(Msg::header_target));
    ImGui::SameLine();
    ImGui::TextWrapped("%s", picker.SelectedLabel().c_str());
    if (recording.active) ImGui::BeginDisabled();
    if (ImGui::Button(Tr(Msg::header_change_target), ImVec2(180.0F, 0.0F))) {
        panel.targetType = static_cast<int>(picker.Selected().type);
        picker.Refresh();
        panel.openTargetPicker = true;
    }
    if (recording.active) ImGui::EndDisabled();
    Explain(Msg::tooltip_change_target_title, Msg::tooltip_change_target_body);
    ImGui::SameLine();
    if (recording.active) ImGui::BeginDisabled();
    if (ImGui::Button(Tr(Msg::header_quick_capture), ImVec2(160.0F, 0.0F))) {
        command.quickCapture = true;
    }
    if (recording.active) ImGui::EndDisabled();
    Explain(Msg::tooltip_quick_capture_title, Msg::tooltip_quick_capture_body);
    ImGui::TextDisabled("%s: %.*s", Tr(Msg::header_output),
                        static_cast<int>(outputDirectory.size()), outputDirectory.data());
    ImGui::SameLine();
    const char* openFolder = CurrentLanguage() == Language::Korean
        ? "출력 폴더 열기###OpenOutputFolder" : "Open output folder###OpenOutputFolder";
    if (ImGui::SmallButton(openFolder)) command.openOutputDirectory = true;
    if (!targetOverlayStatus.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%.*s",
                           static_cast<int>(targetOverlayStatus.size()), targetOverlayStatus.data());
    }
    EndCard();
}

void DrawQuickCapturePopup(MainPanelCommand& command, MainPanelState& panel,
                           const QuickCaptureUiState& quickCapture) {
    const std::string popup = std::string(CurrentLanguage() == Language::Korean
        ? "빠른 캡처" : "Quick Capture") + "###QuickCaptureAction";
    if (quickCapture.actionPending && !ImGui::IsPopupOpen(popup.c_str())) {
        ImGui::OpenPopup(popup.c_str());
        panel.quickCapturePopupWasOpen = true;
    }
    if (!quickCapture.actionPending) panel.quickCapturePopupWasOpen = false;
    if (!ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(CurrentLanguage() == Language::Korean
        ? "선택한 영역으로 무엇을 만들까요?" : "Choose what to create from the selected region.");
    if (ImGui::Button(CurrentLanguage() == Language::Korean ? "스크린샷" : "Screenshot",
                      ImVec2(180.0F, 42.0F))) {
        command.quickCaptureScreenshot = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(Msg::tab_video), ImVec2(180.0F, 42.0F))) {
        command.quickCaptureVideo = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(Msg::tab_gif), ImVec2(180.0F, 42.0F))) {
        command.quickCaptureAnimation = true;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(Tr(Msg::cancel), ImVec2(180.0F, 0.0F))) {
        command.cancelQuickCapture = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawStatusBar(const RecordingUiState& recording) {
    if (!recording.active && recording.outputPath.empty()) return;
    BeginCard("recording-strip");
    ImGui::Text("%s: %llu | %s: %llu | %s: %llu | %.2f s",
                Tr(Msg::strip_encoded), static_cast<unsigned long long>(recording.frameCount),
                Tr(Msg::strip_source), static_cast<unsigned long long>(recording.sourceFrameCount),
                Tr(Msg::strip_skipped), static_cast<unsigned long long>(recording.skippedFrameTicks),
                recording.elapsedSeconds);
    Explain(Msg::tooltip_skipped_title, Msg::tooltip_skipped_body);
    const double sourceFps = recording.elapsedSeconds > 0.0
        ? static_cast<double>(recording.sourceFrameCount) / recording.elapsedSeconds : 0.0;
    ImGui::Text("%s: %.1f fps | %s: %llu | %s: %llu",
                Tr(Msg::strip_source), sourceFps,
                Tr(Msg::strip_capture_drops),
                static_cast<unsigned long long>(recording.captureDroppedFrameCount),
                Tr(Msg::strip_realtime_drops),
                static_cast<unsigned long long>(recording.processingDroppedFrameCount));
    Explain(Msg::tooltip_drops_title, Msg::tooltip_drops_body);
    ImGui::Text("%s: %zu | %s %llu | %s %.1f ms",
                Tr(Msg::strip_encoder_queue), recording.encoderQueuePeak,
                Tr(Msg::strip_encoder_drops),
                static_cast<unsigned long long>(recording.encoderDroppedFrameCount),
                Tr(Msg::strip_submit), recording.maximumEncodeSubmissionMilliseconds);
    if (recording.maximumEncodeSubmissionMilliseconds > 20.0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F), "%s", Tr(Msg::strip_encoder_pressure));
    }
    Explain(Msg::tooltip_encoder_queue_title, Msg::tooltip_encoder_queue_body);
    ImGui::Text("%s: %zu", Tr(Msg::strip_mux_peak), recording.muxQueuePeak);
    Explain(Msg::tooltip_mux_title, Msg::tooltip_mux_body);
    if (!recording.encoderName.empty()) {
        ImGui::Text("%s: %.*s", Tr(Msg::strip_active_encoder),
                    static_cast<int>(recording.encoderName.size()), recording.encoderName.data());
    }
    if (recording.outputWidth > 0 && recording.outputHeight > 0) {
        ImGui::Text("%s: %dx%d | %.1f %s", Tr(Msg::header_output), recording.outputWidth,
                    recording.outputHeight, static_cast<double>(recording.videoBitRate) / 1'000'000.0,
                    Tr(Msg::mbps_suffix));
    }
    if (!recording.outputPath.empty()) {
        ImGui::TextWrapped("%s: %.*s", Tr(Msg::header_output),
                           static_cast<int>(recording.outputPath.size()), recording.outputPath.data());
    }
    EndCard();
}

void DrawTargetPopups(MainPanelCommand& command, MainPanelState& panel,
                      CaptureTargetPicker& picker, ID3D11Device* device) {
    const std::string targetPopup = std::string(Tr(Msg::popup_capture_target)) + "###CaptureTargetPicker";
    const std::string savePopup = std::string(Tr(Msg::popup_save_region)) + "###SaveRegionDialog";
    const std::string renamePopup = std::string(Tr(Msg::popup_rename_region)) + "###RenameRegionDialog";
    if (panel.openTargetPicker) {
        ImGui::OpenPopup(targetPopup.c_str());
        panel.openTargetPicker = false;
    }
    if (panel.openSaveRegion) {
        ImGui::OpenPopup(savePopup.c_str());
        panel.openSaveRegion = false;
    }
    if (panel.openRenameRegion) {
        ImGui::OpenPopup(renamePopup.c_str());
        panel.openRenameRegion = false;
    }

    if (ImGui::BeginPopupModal(renamePopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr(Msg::new_name), panel.renameName.data(), panel.renameName.size());
        if (ImGui::Button(Tr(Msg::rename)) && panel.selectedPreset >= 0 &&
            picker.RenameRegionPreset(static_cast<std::size_t>(panel.selectedPreset), panel.renameName.data())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(Msg::cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(savePopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr(Msg::name), panel.presetName.data(), panel.presetName.size());
        ImGui::RadioButton(Tr(Msg::desktop_coordinates), &panel.presetAnchor,
                           static_cast<int>(RegionAnchorType::VirtualDesktop));
        ImGui::SameLine();
        ImGui::RadioButton(Tr(Msg::window_relative), &panel.presetAnchor,
                           static_cast<int>(RegionAnchorType::WindowClient));
        if (panel.presetAnchor == static_cast<int>(RegionAnchorType::WindowClient)) {
            const auto& windows = picker.Windows();
            if (panel.anchorWindow >= static_cast<int>(windows.size())) panel.anchorWindow = 0;
            const char* windowPreview = windows.empty() ? Tr(Msg::no_window_available)
                                                        : windows[static_cast<std::size_t>(panel.anchorWindow)].title.c_str();
            if (ImGui::BeginCombo(Tr(Msg::anchor_window), windowPreview)) {
                for (std::size_t index = 0; index < windows.size(); ++index) {
                    const std::string label = windows[index].title + "  [" + windows[index].processName + "]##anchor" +
                                              std::to_string(index);
                    if (ImGui::Selectable(label.c_str(), panel.anchorWindow == static_cast<int>(index))) {
                        panel.anchorWindow = static_cast<int>(index);
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button(Tr(Msg::save))) {
            if (picker.CreateRegionPreset(panel.presetName.data(),
                                          static_cast<RegionAnchorType>(panel.presetAnchor),
                                          static_cast<std::size_t>(panel.anchorWindow))) {
                panel.selectedPreset = static_cast<int>(picker.Presets().size()) - 1;
                panel.presetName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(Msg::cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(targetPopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::RadioButton(Tr(Msg::target_window), &panel.targetType,
                           static_cast<int>(CaptureTargetType::Window));
        Explain(Msg::tooltip_window_title, Msg::tooltip_window_body);
        ImGui::SameLine();
        ImGui::RadioButton(Tr(Msg::target_region), &panel.targetType,
                           static_cast<int>(CaptureTargetType::Region));
        Explain(Msg::tooltip_region_title, Msg::tooltip_region_body);
        ImGui::SameLine();
        ImGui::RadioButton(Tr(Msg::target_monitor), &panel.targetType,
                           static_cast<int>(CaptureTargetType::Monitor));
        Explain(Msg::tooltip_monitor_title, Msg::tooltip_monitor_body);
        ImGui::SameLine();
        if (ImGui::SmallButton(Tr(Msg::refresh))) picker.Refresh();
        Explain(Msg::tooltip_refresh_title, Msg::tooltip_refresh_body);
        ImGui::Separator();

        if (panel.targetType == static_cast<int>(CaptureTargetType::Window)) {
            ImGui::TextUnformatted(Tr(Msg::choose_visible_window));
            ImGui::BeginChild("windows", ImVec2(560.0F, 300.0F), ImGuiChildFlags_Borders);
            const auto& windows = picker.Windows();
            Icons().Prune(windows);
            for (std::size_t index = 0; index < windows.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                const std::string label = windows[index].title + "  [" + windows[index].processName + "]##" +
                                          std::to_string(index);
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
        } else if (panel.targetType == static_cast<int>(CaptureTargetType::Monitor)) {
            ImGui::TextUnformatted(Tr(Msg::choose_a_monitor));
            const auto& monitors = picker.Monitors();
            for (std::size_t index = 0; index < monitors.size(); ++index) {
                const auto& monitor = monitors[index];
                const std::string label = monitor.deviceName + "  " +
                    std::to_string(monitor.bounds.right - monitor.bounds.left) + "x" +
                    std::to_string(monitor.bounds.bottom - monitor.bounds.top) +
                    (monitor.primary ? std::string("  (") + Tr(Msg::primary_monitor) + ")" : "") +
                    "##" + std::to_string(index);
                if (ImGui::Selectable(label.c_str())) {
                    picker.SelectMonitor(index);
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            if (ImGui::Button(Tr(Msg::select_capture_target), ImVec2(220.0F, 36.0F))) {
                command.selectRegion = true;
                ImGui::CloseCurrentPopup();
            }
            Explain(Msg::tooltip_select_target_title, Msg::tooltip_select_target_body);

            ImGui::SeparatorText(Tr(Msg::region_presets));
            const auto& presets = picker.Presets();
            if (panel.selectedPreset >= static_cast<int>(presets.size())) panel.selectedPreset = -1;
            const char* preview = panel.selectedPreset >= 0
                ? presets[static_cast<std::size_t>(panel.selectedPreset)].name.c_str()
                : Tr(Msg::choose_saved_region);
            ImGui::SetNextItemWidth(300.0F);
            if (ImGui::BeginCombo("##RegionPreset", preview)) {
                for (std::size_t index = 0; index < presets.size(); ++index) {
                    const bool selected = panel.selectedPreset == static_cast<int>(index);
                    const std::string label = presets[index].name + "  [" +
                        std::string(presets[index].anchorType == RegionAnchorType::WindowClient
                                        ? Tr(Msg::preset_window_tag)
                                        : Tr(Msg::preset_desktop_tag)) +
                        "]##" + presets[index].id;
                    if (ImGui::Selectable(label.c_str(), selected)) panel.selectedPreset = static_cast<int>(index);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(Msg::apply_preset)) && panel.selectedPreset >= 0) {
                if (picker.ApplyRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) {
                    panel.targetType = static_cast<int>(CaptureTargetType::Region);
                    ImGui::CloseCurrentPopup();
                }
            }
            Explain(Msg::tooltip_apply_preset_title, Msg::tooltip_apply_preset_body, true);

            if (ImGui::Button(Tr(Msg::save_current))) {
                picker.Refresh();
                panel.openSaveRegion = true;
                ImGui::CloseCurrentPopup();
            }
            Explain(Msg::tooltip_save_preset_title, Msg::tooltip_save_preset_body);
            ImGui::SameLine();
            if (ImGui::Button(Tr(Msg::delete_preset)) && panel.selectedPreset >= 0) {
                if (picker.DeleteRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) {
                    panel.selectedPreset = -1;
                }
            }
            Explain(Msg::tooltip_delete_preset_title, Msg::tooltip_delete_preset_body, true);
            if (panel.selectedPreset >= 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(Msg::rename))) {
                    std::snprintf(panel.renameName.data(), panel.renameName.size(), "%s",
                                  presets[static_cast<std::size_t>(panel.selectedPreset)].name.c_str());
                    panel.openRenameRegion = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(Tr(Msg::duplicate)) &&
                    picker.DuplicateRegionPreset(static_cast<std::size_t>(panel.selectedPreset))) {
                    ++panel.selectedPreset;
                }
                ImGui::SameLine();
                if (ImGui::ArrowButton("preset-up", ImGuiDir_Up) &&
                    picker.MoveRegionPreset(static_cast<std::size_t>(panel.selectedPreset), -1)) {
                    --panel.selectedPreset;
                }
                ImGui::SameLine();
                if (ImGui::ArrowButton("preset-down", ImGuiDir_Down) &&
                    picker.MoveRegionPreset(static_cast<std::size_t>(panel.selectedPreset), 1)) {
                    ++panel.selectedPreset;
                }
            }
            if (!picker.LastError().empty()) {
                ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", picker.LastError().c_str());
            }
        }
        if (ImGui::Button(Tr(Msg::cancel))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawCaptureTab(MainPanelCommand& command, MainPanelState& panel,
                    const ScreenshotUiState& screenshot, std::string_view screenshotStatus) {
    BeginCard("capture-screenshot");
    ImGui::TextUnformatted(Tr(Msg::capture_step_screenshot));
    ImGui::TextWrapped("%s", Tr(Msg::screenshot_intro));
    if (!panel.screenshotDestinationInitialized) {
        panel.screenshotDestination =
            screenshot.shortcutDestination == ScreenshotDestination::File ? 1 :
            screenshot.shortcutDestination == ScreenshotDestination::FileAndClipboard ? 2 : 0;
        panel.screenshotDestinationInitialized = true;
    }
    if (!panel.screenshotProfileInitialized) {
        panel.screenshotProfile = static_cast<int>(screenshot.profile);
        panel.screenshotProfileInitialized = true;
    }
    ImGui::SeparatorText(CurrentLanguage() == Language::Korean ? "저장 프리셋" : "Save preset");
    constexpr std::array<const char*, 5> english{
        "PNG lossless", "WebP document", "WebP balanced", "JPEG compatible", "AVIF smallest"};
    constexpr std::array<const char*, 5> korean{
        "PNG 무손실", "WebP 문서", "WebP 균형", "JPEG 호환", "AVIF 최소 용량"};
    constexpr std::array<const char*, 5> englishDetails{
        "Exact pixels and transparency · largest", "Near-lossless text and UI · smaller than PNG",
        "Good general quality · recommended", "Works almost everywhere · no transparency",
        "Smallest file · slower to save"};
    constexpr std::array<const char*, 5> koreanDetails{
        "픽셀과 투명도 보존 · 가장 큰 용량", "글자와 UI를 선명하게 · PNG보다 작음",
        "일반 용도 화질과 용량 균형 · 권장", "거의 모든 곳에서 호환 · 투명도 없음",
        "가장 작은 용량 · 저장 속도 느림"};
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available > 720.0F ? 3 : (available > 460.0F ? 2 : 1);
    const float width = (available - static_cast<float>(columns - 1) * 8.0F) / columns;
    for (int index = 0; index < 5; ++index) {
        if (index > 0 && index % columns != 0) ImGui::SameLine();
        const bool selected = panel.screenshotProfile == index;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        ImGui::PushID(index + 600);
        if (ImGui::Button(CurrentLanguage() == Language::Korean ? korean[index] : english[index],
                          ImVec2(width, 42.0F))) {
            panel.screenshotProfile = index;
            command.screenshotProfile = static_cast<ScreenshotProfile>(index);
            command.applyScreenshotProfile = true;
        }
        ExplainLastItem(CurrentLanguage() == Language::Korean ? korean[index] : english[index],
                        CurrentLanguage() == Language::Korean ? koreanDetails[index] : englishDetails[index]);
        ImGui::PopID();
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(240.0F);
    const char* shortcutPreview =
        panel.screenshotDestination == 1 ? (CurrentLanguage() == Language::Korean ? "파일 저장" : "Save file") :
        panel.screenshotDestination == 2 ? (CurrentLanguage() == Language::Korean ? "파일 저장 + 클립보드" : "Save file + clipboard") :
                                           Tr(Msg::clipboard_only);
    if (ImGui::BeginCombo(Tr(Msg::shortcut_result), shortcutPreview)) {
        constexpr int optionCount = 3;
        for (int index = 0; index < optionCount; ++index) {
            const bool selected = panel.screenshotDestination == index;
            const char* label = index == 0 ? Tr(Msg::clipboard_only) :
                index == 1 ? (CurrentLanguage() == Language::Korean ? "파일 저장" : "Save file") :
                             (CurrentLanguage() == Language::Korean ? "파일 저장 + 클립보드" : "Save file + clipboard");
            if (ImGui::Selectable(label, selected)) {
                panel.screenshotDestination = index;
                command.applyScreenshotShortcutDestination = true;
                command.screenshotShortcutDestination =
                    index == 1 ? ScreenshotDestination::File :
                    index == 2 ? ScreenshotDestination::FileAndClipboard :
                                 ScreenshotDestination::Clipboard;
            }
        }
        ImGui::EndCombo();
    }
    Explain(Msg::tooltip_shortcut_result_title, Msg::tooltip_shortcut_result_body);
    command.screenshotProfile = static_cast<ScreenshotProfile>(panel.screenshotProfile);
    if (!screenshotStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(screenshotStatus.size()), screenshotStatus.data());
    }
    EndCard();
}

void DrawEncoderCombo(MainPanelCommand& command, MainPanelState& panel,
                      const std::vector<EncoderUiChoice>& encoderChoices, const char* encoderPreview) {
    if (ImGui::BeginCombo(Tr(Msg::encoder_backend), encoderPreview)) {
        if (ImGui::Selectable(Tr(Msg::encoder_auto), panel.selectedEncoder == 0)) {
            panel.selectedEncoder = 0;
            command.applyRecordingSettings = true;
        }
        for (std::size_t index = 0; index < encoderChoices.size(); ++index) {
            const auto& choice = encoderChoices[index];
            if (!EncoderChoiceVisible(choice, panel.videoCodec)) continue;
            if (!choice.usable) ImGui::BeginDisabled();
            const bool selected = panel.selectedEncoder == static_cast<int>(index + 1);
            if (ImGui::Selectable(choice.displayName.data(), selected) && choice.usable) {
                panel.selectedEncoder = static_cast<int>(index + 1);
                command.applyRecordingSettings = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !choice.detail.empty()) {
                ExplainLastItem(choice.displayName.data(), std::string(choice.detail).c_str(), true);
            }
            if (!choice.usable) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    Explain(Msg::tooltip_encoder_title, Msg::tooltip_encoder_body);
}

void DrawVideoTab(MainPanelCommand& command, MainPanelState& panel,
                  const std::vector<EncoderUiChoice>& encoderChoices,
                  std::string_view audioStatus, std::string_view remuxStatus) {
    const char* encoderPreview = Tr(Msg::encoder_auto);
    if (panel.selectedEncoder > 0) {
        encoderPreview = encoderChoices[static_cast<std::size_t>(panel.selectedEncoder - 1)].displayName.data();
    }

    BeginCard("video-profiles");
    ImGui::TextUnformatted(Tr(Msg::video_profiles));
    Explain(Msg::tooltip_profile_title, Msg::tooltip_profile_body);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = avail > 640.0F ? 3 : (avail > 420.0F ? 2 : 1);
    const float cardWidth = (avail - (static_cast<float>(columns - 1) * 8.0F)) / static_cast<float>(columns);
    for (std::size_t index = 0; index < kProfileUiOrder.size(); ++index) {
        if (index > 0 && index % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
        const bool selected = panel.recordingProfile == static_cast<int>(kProfileUiOrder[index]);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Button(Tr(kProfileTitles[index]), ImVec2(cardWidth, 44.0F))) {
            panel.recordingProfile = static_cast<int>(kProfileUiOrder[index]);
            RecordingPreferences selectedPrefs{};
            selectedPrefs.framesPerSecond = panel.fps;
            selectedPrefs.quality = panel.quality;
            selectedPrefs.codec = static_cast<VideoCodecPreference>(panel.videoCodec);
            selectedPrefs.resolution = static_cast<VideoResolutionLimit>(panel.videoResolution);
            selectedPrefs.efficiency = static_cast<EncoderEfficiencyMode>(panel.encoderEfficiency);
            selectedPrefs.customBitRateMbps = panel.customBitRateMbps;
            selectedPrefs.allowCodecFallback = panel.allowCodecFallback;
            selectedPrefs.useCustomBitRate = panel.useCustomBitRate;
            selectedPrefs = ApplyRecordingProfile(selectedPrefs, static_cast<RecordingProfile>(panel.recordingProfile));
            panel.fps = selectedPrefs.framesPerSecond;
            panel.quality = selectedPrefs.quality;
            panel.videoCodec = static_cast<int>(selectedPrefs.codec);
            panel.videoResolution = static_cast<int>(selectedPrefs.resolution);
            panel.encoderEfficiency = static_cast<int>(selectedPrefs.efficiency);
            panel.allowCodecFallback = selectedPrefs.allowCodecFallback;
            panel.useCustomBitRate = selectedPrefs.useCustomBitRate;
            panel.selectedEncoder = 0;
            command.applyRecordingSettings = true;
        }
        Explain(Msg::tooltip_profile_title, kProfileDetails[index]);
        ImGui::PopID();
        if (selected) ImGui::PopStyleColor();
    }
    const auto current = std::find(kProfileUiOrder.begin(), kProfileUiOrder.end(),
                                   static_cast<RecordingProfile>(panel.recordingProfile));
    const std::size_t detailIndex = current == kProfileUiOrder.end()
        ? 0
        : static_cast<std::size_t>(std::distance(kProfileUiOrder.begin(), current));
    ImGui::TextWrapped("%s", Tr(kProfileDetails[detailIndex]));
    EndCard();

    BeginCard("video-basic");
    if (ImGui::Checkbox(Tr(Msg::system_audio), &panel.systemAudio)) command.applyRecordingSettings = true;
    Explain(Msg::tooltip_system_audio_title, Msg::tooltip_system_audio_body);
    ImGui::SameLine();
    if (ImGui::Checkbox(Tr(Msg::microphone), &panel.microphone)) command.applyRecordingSettings = true;
    Explain(Msg::tooltip_microphone_title, Msg::tooltip_microphone_body);
    ImGui::SameLine();
    bool cursorIncluded = true;
    ImGui::BeginDisabled();
    ImGui::Checkbox(Tr(Msg::cursor_always), &cursorIncluded);
    ImGui::EndDisabled();
    Explain(Msg::tooltip_cursor_title, Msg::tooltip_cursor_body, true);

    const char* formatPreview = panel.format == 1 ? Tr(Msg::format_mp4) : Tr(Msg::format_mkv);
    if (ImGui::BeginCombo(Tr(Msg::format), formatPreview)) {
        if (ImGui::Selectable(Tr(Msg::format_mkv), panel.format == 0)) {
            panel.format = 0;
            command.applyRecordingSettings = true;
        }
        if (ImGui::Selectable(Tr(Msg::format_mp4), panel.format == 1)) {
            panel.format = 1;
            command.applyRecordingSettings = true;
        }
        ImGui::EndCombo();
    }
    Explain(Msg::tooltip_format_title, Msg::tooltip_format_body);
    if (panel.format == 1) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%s", Tr(Msg::mp4_removes_mkv));
    }

    const int estimateHeight = panel.videoResolution == static_cast<int>(VideoResolutionLimit::Height720) ? 720 : 1080;
    const int estimateWidth = estimateHeight == 720 ? 1280 : 1920;
    RecordingPreferences estimate{};
    estimate.framesPerSecond = panel.fps;
    estimate.quality = panel.quality;
    estimate.profile = static_cast<RecordingProfile>(panel.recordingProfile);
    estimate.codec = static_cast<VideoCodecPreference>(panel.videoCodec);
    estimate.resolution = static_cast<VideoResolutionLimit>(panel.videoResolution);
    estimate.efficiency = static_cast<EncoderEfficiencyMode>(panel.encoderEfficiency);
    estimate.customBitRateMbps = panel.customBitRateMbps;
    estimate.useCustomBitRate = panel.useCustomBitRate;
    const auto estimateCodec = estimate.codec == VideoCodecPreference::Auto ? VideoCodecPreference::Hevc : estimate.codec;
    const auto estimateRate = RecommendedVideoBitRate(estimate, estimateWidth, estimateHeight, estimateCodec);
    const auto estimateBytes = EstimatedRecordingBytesPerHour(estimateRate, panel.systemAudio || panel.microphone);
    ImGui::Text("%s: %.1f %s | %.2f %s @ %dp", Tr(Msg::estimate_prefix),
                static_cast<double>(estimateRate) / 1'000'000.0, Tr(Msg::mbps_suffix),
                static_cast<double>(estimateBytes) / 1'000'000'000.0, Tr(Msg::gb_per_hour), estimateHeight);
    ImGui::TextDisabled("%s", Tr(Msg::estimate_suffix));
    const int pressureHeight = panel.videoResolution == static_cast<int>(VideoResolutionLimit::Source)
        ? 1440 : estimateHeight;
    const auto predictedPressure = PredictRecordingGpuPressure(estimate, pressureHeight);
    Msg pressure = Msg::gpu_pressure_low;
    ImVec4 pressureColor(0.45F, 0.9F, 0.55F, 1.0F);
    if (predictedPressure == RecordingGpuPressure::Moderate) {
        pressure = Msg::gpu_pressure_moderate;
        pressureColor = ImVec4(0.45F, 0.75F, 1.0F, 1.0F);
    } else if (predictedPressure == RecordingGpuPressure::High) {
        pressure = Msg::gpu_pressure_high;
        pressureColor = ImVec4(1.0F, 0.75F, 0.3F, 1.0F);
    } else if (predictedPressure == RecordingGpuPressure::VeryHigh) {
        pressure = Msg::gpu_pressure_very_high;
        pressureColor = ImVec4(1.0F, 0.45F, 0.35F, 1.0F);
    }
    ImGui::TextColored(pressureColor, "%s", Tr(pressure));
    Explain(Msg::tooltip_pressure_title, Msg::tooltip_pressure_body);

    if (!remuxStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(remuxStatus.size()), remuxStatus.data());
    }
    if (!audioStatus.empty()) {
        ImGui::TextWrapped("%s: %.*s", Tr(Msg::audio_prefix),
                           static_cast<int>(audioStatus.size()), audioStatus.data());
    }
    EndCard();

    if (ImGui::CollapsingHeader(Tr(Msg::advanced_encoding))) {
        BeginCard("video-advanced");
        constexpr std::array resolutions{Msg::resolution_source, Msg::resolution_1080, Msg::resolution_720};
        if (ImGui::BeginCombo(Tr(Msg::resolution), Tr(resolutions[static_cast<std::size_t>(
                std::clamp(panel.videoResolution, 0, 2))]))) {
            for (int index = 0; index < 3; ++index) {
                if (ImGui::Selectable(Tr(resolutions[static_cast<std::size_t>(index)]),
                                      panel.videoResolution == index)) {
                    panel.videoResolution = index;
                    panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
                    panel.useCustomBitRate = false;
                    command.applyRecordingSettings = true;
                }
            }
            ImGui::EndCombo();
        }
        constexpr std::array codecs{Msg::codec_auto, Msg::codec_h264, Msg::codec_hevc, Msg::codec_av1};
        if (ImGui::BeginCombo(Tr(Msg::codec), Tr(codecs[static_cast<std::size_t>(
                std::clamp(panel.videoCodec, 0, 3))]))) {
            for (int index = 0; index < 4; ++index) {
                if (ImGui::Selectable(Tr(codecs[static_cast<std::size_t>(index)]), panel.videoCodec == index)) {
                    panel.videoCodec = index;
                    panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
                    panel.useCustomBitRate = false;
                    panel.selectedEncoder = 0;
                    command.applyRecordingSettings = true;
                }
            }
            ImGui::EndCombo();
        }
        Explain(Msg::tooltip_codec_title, Msg::tooltip_codec_body);
        bool selectedCodecAvailable = panel.videoCodec == static_cast<int>(VideoCodecPreference::Auto);
        for (const auto& choice : encoderChoices) {
            if (choice.usable && choice.codec == static_cast<VideoCodecPreference>(panel.videoCodec)) {
                selectedCodecAvailable = true;
                break;
            }
        }
        if (!selectedCodecAvailable) {
            ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.35F, 1.0F), "%s", Tr(Msg::codec_unavailable));
        }
        ImGui::SliderInt(Tr(Msg::fps), &panel.fps, 15, 120);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
            command.applyRecordingSettings = true;
        }
        Explain(Msg::tooltip_fps_title, Msg::tooltip_fps_body);
        DrawEncoderCombo(command, panel, encoderChoices, encoderPreview);
        constexpr std::array efforts{Msg::effort_realtime, Msg::effort_balanced, Msg::effort_efficient,
                                     Msg::effort_quality};
        if (ImGui::BeginCombo(Tr(Msg::encoder_effort), Tr(efforts[static_cast<std::size_t>(
                std::clamp(panel.encoderEfficiency, 0, 3))]))) {
            for (int index = 0; index < 4; ++index) {
                if (ImGui::Selectable(Tr(efforts[static_cast<std::size_t>(index)]),
                                      panel.encoderEfficiency == index)) {
                    panel.encoderEfficiency = index;
                    panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
                    panel.useCustomBitRate = false;
                    command.applyRecordingSettings = true;
                }
            }
            ImGui::EndCombo();
        }
        Explain(Msg::tooltip_effort_title, Msg::tooltip_effort_body);
        if (ImGui::Checkbox(Tr(Msg::custom_bitrate), &panel.useCustomBitRate)) {
            panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
            command.applyRecordingSettings = true;
        }
        if (panel.useCustomBitRate) {
            ImGui::SliderInt(Tr(Msg::target_bitrate), &panel.customBitRateMbps, 1, 100, "%d Mbps");
            if (ImGui::IsItemDeactivatedAfterEdit()) command.applyRecordingSettings = true;
        }
        if (ImGui::Checkbox(Tr(Msg::allow_fallback), &panel.allowCodecFallback)) {
            panel.recordingProfile = static_cast<int>(RecordingProfile::Custom);
            command.applyRecordingSettings = true;
        }
        ImGui::TextDisabled("%s", Tr(Msg::color_pipeline));
        Explain(Msg::tooltip_hdr_title, Msg::tooltip_hdr_body);
        if (ImGui::SmallButton(Tr(Msg::restore_video_defaults))) {
            ApplyRecordingPreferences(panel, DefaultRecordingPreferences(), true, false);
            panel.selectedEncoder = 0;
            panel.encoderSelectionPending = false;
            command.resetVideoSettings = true;
            command.applyRecordingSettings = true;
        }
        Explain(Msg::tooltip_restore_video_title, Msg::tooltip_restore_video_body);
        EndCard();
    }
}

void DrawGifTab(MainPanelCommand& command, MainPanelState& panel, const RecordingUiState& recording,
                std::string_view gifStatus, bool outputBusy, bool mediaBusy) {
    BeginCard("animation-profiles");
    ImGui::TextUnformatted(CurrentLanguage() == Language::Korean ? "애니메이션 프리셋" : "Animation presets");
    constexpr std::array<const char*, 6> english{"Quick share", "Balanced", "Smooth", "High quality",
                                                 "Maximum compatibility", "Smallest file"};
    constexpr std::array<const char*, 6> korean{"빠른 공유", "균형", "부드러운 움직임", "고화질",
                                                "최대 호환", "최소 용량"};
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available > 650.0F ? 3 : 2;
    const float width = (available - static_cast<float>(columns - 1) * 8.0F) / columns;
    for (int index = 0; index < 6; ++index) {
        if (index > 0 && index % columns != 0) ImGui::SameLine();
        const bool selected = panel.animationProfile == index;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        if (ImGui::Button((CurrentLanguage() == Language::Korean ? korean[index] : english[index]),
                          ImVec2(width, 40.0F))) {
            RecordingPreferences preferences{};
            preferences.gifFramesPerSecond = kGifFpsChoices[static_cast<std::size_t>(panel.gifFpsIndex)];
            preferences.gifHeight = kGifHeightChoices[static_cast<std::size_t>(panel.gifHeightIndex)];
            preferences.gifColors = kGifColorChoices[static_cast<std::size_t>(panel.gifColorIndex)];
            preferences.animationFormat = static_cast<AnimationFormat>(panel.animationFormat);
            preferences.animationQuality = panel.animationQuality;
            preferences.avifCrf = panel.avifCrf;
            preferences = ApplyAnimationProfile(preferences, static_cast<AnimationProfile>(index));
            panel.gifFpsIndex = GifFpsIndex(preferences.gifFramesPerSecond);
            panel.gifHeightIndex = GifHeightIndex(preferences.gifHeight);
            panel.gifColorIndex = GifColorIndex(preferences.gifColors);
            panel.animationFormat = static_cast<int>(preferences.animationFormat);
            panel.animationProfile = index;
            panel.animationQuality = preferences.animationQuality;
            panel.avifCrf = preferences.avifCrf;
            command.applyRecordingSettings = true;
        }
        if (selected) ImGui::PopStyleColor();
    }
    EndCard();

    BeginCard("gif-controls");
    ImGui::TextWrapped("%s", CurrentLanguage() == Language::Korean
        ? "해상도와 FPS를 낮추면 용량이 크게 줄어듭니다. WebP는 일반 용도에 권장하고, GIF는 최대 호환, AVIF는 최소 용량에 적합합니다."
        : "Lower resolution and FPS reduce size substantially. WebP is recommended for general use, GIF for maximum compatibility, and AVIF for the smallest files.");
    if (outputBusy) ImGui::BeginDisabled();
    constexpr std::array<const char*, 3> formatNamesEnglish{
        "GIF", "Animated WebP (recommended)", "Animated AVIF (smallest, slow)"};
    constexpr std::array<const char*, 3> formatNamesKorean{
        "GIF (최대 호환)", "Animated WebP (권장)", "Animated AVIF (최소 용량·느림)"};
    const auto& formatNames = CurrentLanguage() == Language::Korean
        ? formatNamesKorean : formatNamesEnglish;
    if (ImGui::Combo(CurrentLanguage() == Language::Korean ? "출력 형식" : "Output format",
                     &panel.animationFormat, formatNames.data(),
                     static_cast<int>(formatNames.size()))) {
        panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
        command.applyRecordingSettings = true;
    }
    constexpr std::array gifFpsLabels{"6 fps", "10 fps", "12 fps", "15 fps", "20 fps", "30 fps"};
    if (ImGui::Combo(Tr(Msg::gif_fps), &panel.gifFpsIndex, gifFpsLabels.data(),
                     static_cast<int>(gifFpsLabels.size()))) {
        panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
        command.applyRecordingSettings = true;
    }
    Explain(Msg::tooltip_gif_fps_title, Msg::tooltip_gif_fps_body, outputBusy);
    constexpr std::array gifHeightLabels{"360p", "480p", "720p", "1080p"};
    if (ImGui::Combo(Tr(Msg::gif_resolution), &panel.gifHeightIndex, gifHeightLabels.data(),
                     static_cast<int>(gifHeightLabels.size()))) {
        panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
        command.applyRecordingSettings = true;
    }
    Explain(Msg::tooltip_gif_res_title, Msg::tooltip_gif_res_body, outputBusy);
    if (panel.animationFormat == static_cast<int>(AnimationFormat::Gif)) {
        const std::array gifColorLabels{Tr(Msg::gif_colors_64), Tr(Msg::gif_colors_128), Tr(Msg::gif_colors_192),
                                        Tr(Msg::gif_colors_256)};
        if (ImGui::BeginCombo(Tr(Msg::gif_colors), gifColorLabels[static_cast<std::size_t>(
                std::clamp(panel.gifColorIndex, 0, 3))])) {
            for (int index = 0; index < 4; ++index) {
                if (ImGui::Selectable(gifColorLabels[static_cast<std::size_t>(index)],
                                      panel.gifColorIndex == index)) {
                    panel.gifColorIndex = index;
                    panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
                    command.applyRecordingSettings = true;
                }
            }
            ImGui::EndCombo();
        }
        Explain(Msg::tooltip_gif_colors_title, Msg::tooltip_gif_colors_body, outputBusy);
    } else if (panel.animationFormat == static_cast<int>(AnimationFormat::WebP)) {
        ImGui::SliderInt("WebP quality", &panel.animationQuality, 1, 100);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
            command.applyRecordingSettings = true;
        }
    } else {
        ImGui::SliderInt("AVIF CRF (lower is higher quality)", &panel.avifCrf, 0, 63);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            panel.animationProfile = static_cast<int>(AnimationProfile::Custom);
            command.applyRecordingSettings = true;
        }
    }
    if (outputBusy) ImGui::EndDisabled();
    command.gifFramesPerSecond = kGifFpsChoices[static_cast<std::size_t>(
        std::clamp(panel.gifFpsIndex, 0, static_cast<int>(kGifFpsChoices.size()) - 1))];
    command.gifHeight = kGifHeightChoices[static_cast<std::size_t>(
        std::clamp(panel.gifHeightIndex, 0, static_cast<int>(kGifHeightChoices.size()) - 1))];
    command.gifColors = kGifColorChoices[static_cast<std::size_t>(
        std::clamp(panel.gifColorIndex, 0, static_cast<int>(kGifColorChoices.size()) - 1))];
    command.animationFormat = static_cast<AnimationFormat>(panel.animationFormat);
    command.animationProfile = static_cast<AnimationProfile>(panel.animationProfile);
    command.animationQuality = panel.animationQuality;
    command.avifCrf = panel.avifCrf;
    const int estimateWidth = command.gifHeight * 16 / 9;
    const auto tenSeconds = EstimateAnimationSize(command.animationFormat, estimateWidth, command.gifHeight,
                                                   command.gifFramesPerSecond, 10.0,
                                                   command.animationQuality, command.gifColors, command.avifCrf);
    const double limit = GifDurationLimit(SIZE{estimateWidth, command.gifHeight}, command.gifFramesPerSecond);
    const auto maximum = EstimateAnimationSize(command.animationFormat, estimateWidth, command.gifHeight,
                                                command.gifFramesPerSecond, limit,
                                                command.animationQuality, command.gifColors, command.avifCrf);
    ImGui::Text(CurrentLanguage() == Language::Korean
                    ? "예상 10초: %.1f-%.1f MB | 자동 종료 %.0f초: %.1f-%.1f MB"
                    : "Estimated 10 s: %.1f-%.1f MB | auto-stop %.0f s: %.1f-%.1f MB",
                tenSeconds.minimumBytes / 1'000'000.0, tenSeconds.maximumBytes / 1'000'000.0, limit,
                maximum.minimumBytes / 1'000'000.0, maximum.maximumBytes / 1'000'000.0);
    ImGui::TextDisabled("%s", CurrentLanguage() == Language::Korean
        ? "움직임과 화면 복잡도에 따라 실제 용량은 크게 달라질 수 있습니다."
        : "Estimate varies widely with motion and screen detail.");
    if (command.gifHeight >= 1080 || command.gifFramesPerSecond >= 30) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%s", Tr(Msg::gif_warning_large));
    }
    if (!outputBusy && ImGui::SmallButton(Tr(Msg::restore_gif_defaults))) {
        ApplyRecordingPreferences(panel, DefaultRecordingPreferences(), false, true);
        command.resetGifSettings = true;
        command.applyRecordingSettings = true;
    }
    if (!outputBusy) Explain(Msg::tooltip_restore_gif_title, Msg::tooltip_restore_gif_body);
    if (mediaBusy) {
        ImGui::ProgressBar(static_cast<float>(std::clamp(recording.mediaProgress, 0.0, 1.0)),
                           ImVec2(-1.0F, 0.0F), Tr(Msg::creating_gif));
    }
    if (!gifStatus.empty()) {
        ImGui::TextWrapped("%.*s", static_cast<int>(gifStatus.size()), gifStatus.data());
    }
    EndCard();
}

void DrawSessionCommandBar(MainPanelCommand& command, const MainPanelState& panel,
                           const RecordingUiState& recording, bool mediaBusy) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
    ImGui::BeginChild("SessionCommandBar", ImVec2(-1.0F, 64.0F), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextColored(StatusColor(recording), "%s", StatusLabel(recording));
    if (recording.active) {
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f s", recording.elapsedSeconds);
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr(Msg::footer_ready));
    }

    float totalWidth = 0.0F;
    if (recording.active) totalWidth = 290.0F;
    else if (mediaBusy) totalWidth = 220.0F;
    else if (panel.activeTab == 1 && recording.canRemux) totalWidth = 408.0F;
    else if (panel.activeTab <= 2) totalWidth = 220.0F;
    if (totalWidth > 0.0F) {
        ImGui::SameLine();
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - totalWidth));
    }

    if (recording.active) {
        if (ImGui::Button(recording.paused ? Tr(Msg::resume) : Tr(Msg::pause), ImVec2(130.0F, 38.0F))) {
            if (recording.paused) command.resumeRecording = true;
            else command.pauseRecording = true;
        }
        Explain(recording.paused ? Msg::tooltip_resume_title : Msg::tooltip_pause_title,
                recording.paused ? Msg::tooltip_resume_body : Msg::tooltip_pause_body);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72F, 0.18F, 0.18F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86F, 0.24F, 0.24F, 1.0F));
        if (ImGui::Button(recording.gif ? Tr(Msg::stop_gif) : Tr(Msg::stop_video),
                          ImVec2(150.0F, 38.0F))) {
            command.stopRecording = true;
        }
        ImGui::PopStyleColor(2);
        Explain(recording.gif ? Msg::tooltip_stop_gif_title : Msg::tooltip_stop_video_title,
                recording.gif ? Msg::tooltip_stop_gif_body : Msg::tooltip_stop_video_body);
    } else if (mediaBusy) {
        if (recording.mediaCancelRequested) ImGui::BeginDisabled();
        if (ImGui::Button(recording.mediaCancelRequested ? Tr(Msg::cancelling) : Tr(Msg::cancel_gif),
                          ImVec2(220.0F, 38.0F))) {
            command.cancelMediaJob = true;
        }
        if (recording.mediaCancelRequested) ImGui::EndDisabled();
    } else if (panel.activeTab == 0) {
        const char* label = panel.screenshotDestination == 1
            ? (CurrentLanguage() == Language::Korean ? "스크린샷 저장" : "Save screenshot")
            : panel.screenshotDestination == 2
                ? (CurrentLanguage() == Language::Korean ? "저장하고 복사" : "Save and copy")
                : Tr(Msg::copy_to_clipboard);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        if (ImGui::Button(label, ImVec2(220.0F, 38.0F))) {
            if (panel.screenshotDestination == 1) command.saveScreenshot = true;
            else if (panel.screenshotDestination == 2) command.saveAndCopyScreenshot = true;
            else command.copyScreenshot = true;
        }
        ImGui::PopStyleColor();
        Explain(panel.screenshotDestination == 1 ? Msg::tooltip_png_title :
                panel.screenshotDestination == 2 ? Msg::tooltip_png_copy_title : Msg::tooltip_copy_title,
                panel.screenshotDestination == 1 ? Msg::tooltip_png_body :
                panel.screenshotDestination == 2 ? Msg::tooltip_png_copy_body : Msg::tooltip_copy_body);
    } else if (panel.activeTab == 1) {
        if (recording.canRemux) {
            if (ImGui::Button(Tr(Msg::convert_to_mp4), ImVec2(180.0F, 38.0F))) {
                command.remuxLastRecording = true;
            }
            Explain(Msg::tooltip_remux_title, Msg::tooltip_remux_body);
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        if (ImGui::Button(Tr(Msg::start_video), ImVec2(220.0F, 38.0F))) command.startRecording = true;
        ImGui::PopStyleColor();
        Explain(Msg::tooltip_start_video_title, Msg::tooltip_start_video_body);
    } else if (panel.activeTab == 2) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.38F, 0.72F, 1.0F));
        if (ImGui::Button(Tr(Msg::start_gif), ImVec2(220.0F, 38.0F))) command.startGif = true;
        ImGui::PopStyleColor();
        Explain(Msg::tooltip_start_gif_title, Msg::tooltip_start_gif_body);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void DrawSettingsTab(MainPanelCommand& command, MainPanelState& panel, const DisplayUiState& display,
                     const BorderUiState& border, const RegionSelectionUiState& regionSelection,
                     const HotkeyUiState& hotkeys, const TrayUiState& tray,
                     std::string_view outputDirectory,
                     const std::vector<RecoverableRecordingUiItem>& recoverableRecordings,
                     const RecordingUiState& recording, std::string_view gpuName,
                     std::string_view ffmpegVersion, std::string_view encoderSummary,
                     std::string_view frameProcessingError, CaptureTargetPicker& picker,
                     WindowsGraphicsCapture& capture, ID3D11Device* device, bool outputBusy) {
    if (ImGui::CollapsingHeader(Tr(Msg::settings_language), ImGuiTreeNodeFlags_DefaultOpen)) {
        BeginCard("settings-language");
        DrawLanguageToggle(command);
        ImGui::SameLine();
        ImGui::TextUnformatted(CurrentLanguage() == Language::Korean ? Tr(Msg::language_korean)
                                                                     : Tr(Msg::language_english));
        if (!display.fontStatus.empty()) ImGui::TextWrapped("%.*s", static_cast<int>(display.fontStatus.size()),
                                                            display.fontStatus.data());
        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr(Msg::settings_output), ImGuiTreeNodeFlags_DefaultOpen)) {
        BeginCard("settings-output");
        ImGui::TextWrapped("%s", Tr(Msg::output_intro));
        ImGui::TextWrapped("%s: %.*s", Tr(Msg::folder_prefix),
                           static_cast<int>(outputDirectory.size()), outputDirectory.data());
        if (outputBusy) ImGui::BeginDisabled();
        if (ImGui::Button(Tr(Msg::change_output_folder), ImVec2(220.0F, 32.0F))) {
            command.chooseOutputDirectory = true;
        }
        if (outputBusy) ImGui::EndDisabled();
        Explain(outputBusy ? Msg::tooltip_output_busy_title : Msg::tooltip_output_title,
                outputBusy ? Msg::tooltip_output_busy_body : Msg::tooltip_output_body, outputBusy);
        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr(Msg::settings_shortcuts), ImGuiTreeNodeFlags_DefaultOpen)) {
        BeginCard("settings-hotkeys");
        ImGui::TextWrapped("%s", Tr(Msg::hotkey_intro));
        constexpr std::array hotkeyNames{Msg::hotkey_capture, Msg::hotkey_video, Msg::hotkey_gif,
                                         Msg::hotkey_quick};
        for (std::size_t action = 0; action < hotkeyNames.size(); ++action) {
            ImGui::PushID(static_cast<int>(action));
            const bool capturing = hotkeys.capturingAction == static_cast<int>(action);
            ImGui::Text("%s", Tr(hotkeyNames[action]));
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", hotkeys.labels[action].c_str());
            if (capturing) {
                ImGui::TextColored(ImVec4(1.0F, 0.82F, 0.35F, 1.0F), "%s", Tr(Msg::press_keys));
            }
            if (ImGui::SmallButton(capturing ? Tr(Msg::listening) : Tr(Msg::set_shortcut))) {
                if (capturing) command.cancelHotkeyCapture = true;
                else command.listenHotkeyAction = static_cast<int>(action);
            }
            Explain(capturing ? Msg::tooltip_cancel_shortcut_title : Msg::tooltip_set_shortcut_title,
                    capturing ? Msg::tooltip_cancel_shortcut_body : Msg::tooltip_set_shortcut_body);
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr(Msg::clear))) command.clearHotkeyAction = static_cast<int>(action);
            Explain(Msg::tooltip_clear_shortcut_title, Msg::tooltip_clear_shortcut_body);
            ImGui::SameLine();
            if (ImGui::SmallButton(Tr(Msg::restore_default))) {
                command.resetHotkeyAction = static_cast<int>(action);
            }
            Explain(Msg::tooltip_reset_one_shortcut_title, Msg::tooltip_reset_one_shortcut_body);
            ImGui::PopID();
        }
        if (ImGui::SmallButton(Tr(Msg::restore_default_shortcuts))) {
            command.resetHotkeys = true;
            command.cancelHotkeyCapture = true;
        }
        Explain(Msg::tooltip_reset_shortcuts_title, Msg::tooltip_reset_shortcuts_body);
        if (!hotkeys.error.empty()) {
            ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F), "%.*s",
                               static_cast<int>(hotkeys.error.size()), hotkeys.error.data());
        }
        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr(Msg::settings_appearance), ImGuiTreeNodeFlags_DefaultOpen)) {
        BeginCard("settings-appearance");
        if (!panel.displaySettingsInitialized) {
            panel.uiScalePercent = display.userScalePercent;
            panel.displaySettingsInitialized = true;
        }
        ImGui::Text("%s: %d%% | %s: %d%%", Tr(Msg::windows_scaling), display.windowsDpiPercent,
                    Tr(Msg::effective_ui), display.effectiveScalePercent);
        if (!display.status.empty()) {
            ImGui::TextWrapped("%.*s", static_cast<int>(display.status.size()), display.status.data());
        }
        ImGui::SliderInt(Tr(Msg::additional_ui_scale), &panel.uiScalePercent, 75, 200, "%d%%");
        Explain(Msg::tooltip_ui_scale_title, Msg::tooltip_ui_scale_body);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            command.applyUiScale = true;
            command.uiScalePercent = panel.uiScalePercent;
        }

        if (!panel.borderSettingsInitialized) {
            panel.borderVisible = border.visible;
            panel.borderThickness = border.thickness;
            panel.borderOpacity = border.opacityPercent;
            panel.borderSettingsInitialized = true;
        }
        if (ImGui::Checkbox(Tr(Msg::show_target_border), &panel.borderVisible)) {
            command.applyBorderSettings = true;
            command.borderVisible = panel.borderVisible;
            command.borderThickness = panel.borderThickness;
            command.borderOpacityPercent = panel.borderOpacity;
        }
        Explain(Msg::tooltip_border_title, Msg::tooltip_border_body);
        ImGui::SliderInt(Tr(Msg::border_thickness), &panel.borderThickness, 1, 12, "%d px");
        Explain(Msg::tooltip_thickness_title, Msg::tooltip_thickness_body);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            command.applyBorderSettings = true;
            command.borderVisible = panel.borderVisible;
            command.borderThickness = panel.borderThickness;
            command.borderOpacityPercent = panel.borderOpacity;
        }
        ImGui::SliderInt(Tr(Msg::border_opacity), &panel.borderOpacity, 20, 100, "%d%%");
        Explain(Msg::tooltip_opacity_title, Msg::tooltip_opacity_body);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            command.applyBorderSettings = true;
            command.borderVisible = panel.borderVisible;
            command.borderThickness = panel.borderThickness;
            command.borderOpacityPercent = panel.borderOpacity;
        }

        if (!panel.regionSelectionSettingsInitialized) {
            panel.outsideDimmingPercent = regionSelection.outsideDimmingPercent;
            panel.regionSelectionSettingsInitialized = true;
        }
        ImGui::SliderInt(Tr(Msg::selection_dimming), &panel.outsideDimmingPercent, 0, 70, "%d%%");
        Explain(Msg::tooltip_dimming_title, Msg::tooltip_dimming_body);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            command.applyRegionSelectionSettings = true;
            command.regionOutsideDimmingPercent = panel.outsideDimmingPercent;
        }
        if (ImGui::SmallButton(Tr(Msg::reset_appearance))) {
            panel.uiScalePercent = 100;
            panel.borderVisible = true;
            panel.borderThickness = 3;
            panel.borderOpacity = 85;
            panel.outsideDimmingPercent = 30;
            command.resetUiScale = true;
            command.resetBorderSettings = true;
            command.resetRegionSelectionSettings = true;
        }
        Explain(Msg::tooltip_reset_appearance_title, Msg::tooltip_reset_appearance_body);
        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr(Msg::settings_background), ImGuiTreeNodeFlags_DefaultOpen)) {
        BeginCard("settings-background");
        if (!panel.traySettingsInitialized) {
            panel.closeToTray = tray.closeToTray;
            panel.traySettingsInitialized = true;
        } else if (panel.closeToTray != tray.closeToTray) {
            panel.closeToTray = tray.closeToTray;
        }
        if (!tray.available) ImGui::BeginDisabled();
        if (ImGui::Checkbox(Tr(Msg::keep_running), &panel.closeToTray)) {
            command.applyCloseToTray = true;
            command.closeToTray = panel.closeToTray;
        }
        Explain(Msg::tooltip_tray_title, Msg::tooltip_tray_body, !tray.available);
        if (!tray.available) ImGui::EndDisabled();
        if (!tray.status.empty()) {
            ImGui::TextWrapped("%.*s", static_cast<int>(tray.status.size()), tray.status.data());
        }
        EndCard();
    }

    if (ImGui::CollapsingHeader(Tr(Msg::settings_advanced))) {
        BeginCard("settings-advanced");
        if (!recoverableRecordings.empty()) {
            ImGui::TextUnformatted(Tr(Msg::recording_recovery));
            ImGui::TextWrapped("%s", Tr(Msg::recovery_intro));
            for (std::size_t index = 0; index < recoverableRecordings.size(); ++index) {
                const auto& item = recoverableRecordings[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::Text("%s (%.1f MiB)", item.fileName.c_str(),
                            static_cast<double>(item.sizeBytes) / (1024.0 * 1024.0));
                ImGui::SameLine();
                if (outputBusy) ImGui::BeginDisabled();
                if (ImGui::SmallButton(Tr(Msg::recover))) command.recoverRecordingIndex = static_cast<int>(index);
                if (outputBusy) ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::Spacing();
        }
        if (recording.active) {
            ImGui::TextUnformatted(recording.starting ? Tr(Msg::recording_pipeline_starting)
                                                      : Tr(Msg::recording_pipeline_active));
        } else if (!capture.IsRunning()) {
            if (ImGui::Button(Tr(Msg::test_wgc), ImVec2(180.0F, 32.0F))) {
                capture.Start(picker.Selected(), device);
            }
        } else if (ImGui::Button(Tr(Msg::stop_capture_test), ImVec2(180.0F, 32.0F))) {
            capture.Stop();
        }
        ImGui::SameLine();
        ImGui::Text("%s: %llu | %s: %zu | %s: %llu", Tr(Msg::frames_label),
                    static_cast<unsigned long long>(capture.FrameCount()), Tr(Msg::queued_label),
                    capture.QueuedFrameCount(), Tr(Msg::dropped_label),
                    static_cast<unsigned long long>(capture.DroppedFrameCount()));
        const auto captureError = capture.LastError();
        if (!captureError.empty()) ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%s", captureError.c_str());
        if (!frameProcessingError.empty()) {
            ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%.*s",
                               static_cast<int>(frameProcessingError.size()), frameProcessingError.data());
        }
        ImGui::Text("%s: %.*s", Tr(Msg::gpu_label), static_cast<int>(gpuName.size()), gpuName.data());
        ImGui::Text("%s: %.*s", Tr(Msg::ffmpeg_label), static_cast<int>(ffmpegVersion.size()),
                    ffmpegVersion.data());
        ImGui::TextWrapped("%s: %.*s", Tr(Msg::encoder_label), static_cast<int>(encoderSummary.size()),
                           encoderSummary.data());
        if (ImGui::Button(Tr(Msg::restore_all))) {
            ResetPanelPreferences(panel);
            command.resetAllSettings = true;
            command.cancelHotkeyCapture = true;
            command.applyLanguage = true;
            command.language = DetectOsLanguage();
        }
        Explain(Msg::tooltip_restore_all_title, Msg::tooltip_restore_all_body);
        EndCard();
    }
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
                                 const QuickCaptureUiState& quickCapture,
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
    if (panel.selectedEncoder > static_cast<int>(encoderChoices.size())) panel.selectedEncoder = 0;
    const bool mediaBusy = recording.mediaJobActive;
    const bool outputBusy = recording.active || mediaBusy;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("OpenCapture", nullptr, flags);

    constexpr float kMaximumContentWidth = 1200.0F;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float contentWidth = std::min(availableWidth, kMaximumContentWidth);
    if (contentWidth < availableWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - contentWidth) * 0.5F);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::BeginChild("MainContent", ImVec2(contentWidth, 0.0F));
    ImGui::PopStyleColor();

    if (panel.targetType < 0) panel.targetType = static_cast<int>(picker.Selected().type);
    DrawHeader(recording);
    DrawSourceBar(panel, command, recording, outputDirectory, targetOverlayStatus, picker);

    const std::string captureTab = std::string(Tr(Msg::tab_capture)) + "###CaptureTab";
    const std::string videoTab = std::string(Tr(Msg::tab_video)) + "###VideoTab";
    const std::string gifTab = std::string(Tr(Msg::tab_gif)) + "###GifTab";
    const std::string settingsTab = std::string(Tr(Msg::tab_settings)) + "###SettingsTab";
    if (ImGui::BeginTabBar("MainSections")) {
        if (ImGui::BeginTabItem(captureTab.c_str())) {
            panel.activeTab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(videoTab.c_str())) {
            panel.activeTab = 1;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(gifTab.c_str())) {
            panel.activeTab = 2;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(settingsTab.c_str())) {
            panel.activeTab = 3;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    constexpr float kSessionCommandBarHeight = 72.0F;
    ImGui::BeginChild("ModeContent", ImVec2(0.0F, -kSessionCommandBarHeight));
    if (recording.active || !recording.outputPath.empty()) DrawStatusBar(recording);
    if (panel.activeTab == 0) {
        DrawCaptureTab(command, panel, screenshot, screenshotStatus);
    } else if (panel.activeTab == 1) {
        DrawVideoTab(command, panel, encoderChoices, audioStatus, remuxStatus);
    } else if (panel.activeTab == 2) {
        DrawGifTab(command, panel, recording, gifStatus, outputBusy, mediaBusy);
    } else {
        DrawSettingsTab(command, panel, display, border, regionSelection, hotkeys, tray, outputDirectory,
                        recoverableRecordings, recording, gpuName, ffmpegVersion, encoderSummary,
                        frameProcessingError, picker, capture, device, outputBusy);
    }

    if (!recording.error.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.35F, 1.0F), "%.*s",
                           static_cast<int>(recording.error.size()), recording.error.data());
    }
    if (!recoveryStatus.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.25F, 1.0F), "%.*s",
                           static_cast<int>(recoveryStatus.size()), recoveryStatus.data());
    }
    ImGui::EndChild();
    DrawSessionCommandBar(command, panel, recording, mediaBusy);
    DrawTargetPopups(command, panel, picker, device);
    DrawQuickCapturePopup(command, panel, quickCapture);
    ImGui::EndChild();
    ImGui::End();
    CopyRecordingCommand(command, panel, encoderChoices);
    return command;
}

} // namespace opencapture
