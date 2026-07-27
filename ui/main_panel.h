#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <Windows.h>

struct ID3D11Device;

namespace opencapture {

class CaptureTargetPicker;
class WindowsGraphicsCapture;

struct RecordingUiState {
    bool active{};
    bool starting{};
    bool paused{};
    bool gif{};
    bool canRemux{};
    bool mediaJobActive{};
    bool mediaCancelRequested{};
    double mediaProgress{};
    std::string_view outputPath;
    std::string_view error;
    std::string_view encoderName;
    std::uint64_t frameCount{};
    double elapsedSeconds{};
};

struct EncoderUiChoice {
    std::string_view name;
    std::string_view displayName;
    bool usable{};
    std::string_view detail;
};

struct RecoverableRecordingUiItem {
    std::string path;
    std::string fileName;
    std::uint64_t sizeBytes{};
};

struct HotkeyUiState {
    std::array<UINT, 3> modifiers{};
    std::array<UINT, 3> virtualKeys{};
    std::array<std::string, 3> labels{};
    std::string_view error;
};

struct BorderUiState {
    bool visible{true};
    int thickness{3};
    int opacityPercent{85};
};

struct RegionSelectionUiState {
    int outsideDimmingPercent{30};
};

struct MainPanelCommand {
    bool selectRegion{};
    bool chooseOutputDirectory{};
    int recoverRecordingIndex{-1};
    bool startRecording{};
    bool stopRecording{};
    bool pauseRecording{};
    bool resumeRecording{};
    bool remuxLastRecording{};
    bool remuxToMp4{};
    bool startGif{};
    bool cancelMediaJob{};
    bool copyScreenshot{};
    bool saveScreenshot{};
    bool saveAndCopyScreenshot{};
    int changeHotkeyAction{-1};
    UINT hotkeyModifiers{};
    UINT hotkeyVirtualKey{};
    bool resetHotkeys{};
    bool applyBorderSettings{};
    bool resetBorderSettings{};
    bool applyRegionSelectionSettings{};
    bool resetRegionSelectionSettings{};
    bool borderVisible{true};
    int borderThickness{3};
    int borderOpacityPercent{85};
    int regionOutsideDimmingPercent{30};
    int framesPerSecond{60};
    int quality{1};
    int gifFramesPerSecond{12};
    int gifHeight{720};
    int gifColors{256};
    bool systemAudio{true};
    bool microphone{};
    std::string encoderName;
};

class MainPanel final {
public:
    static MainPanelCommand Draw(std::string_view gpuName, std::string_view ffmpegVersion,
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
                                  const HotkeyUiState& hotkeys,
                                  const BorderUiState& border,
                                  const RegionSelectionUiState& regionSelection,
                                  CaptureTargetPicker& picker, WindowsGraphicsCapture& capture,
                                  ID3D11Device* device);
};

} // namespace opencapture
