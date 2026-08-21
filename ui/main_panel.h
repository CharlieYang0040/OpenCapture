#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/recording_options.h"
#include "core/screenshot_options.h"

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
    std::uint64_t sourceFrameCount{};
    std::uint64_t skippedFrameTicks{};
    std::uint64_t captureDroppedFrameCount{};
    std::size_t muxQueuePeak{};
    double maximumSourceGapMilliseconds{};
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
    std::array<std::string, 4> labels{};
    std::string_view error;
    int capturingAction{-1};
};

struct ScreenshotUiState {
    ScreenshotDestination shortcutDestination{ScreenshotDestination::Clipboard};
};

struct TrayUiState {
    bool available{};
    bool closeToTray{};
    std::string_view status;
};

struct BorderUiState {
    bool visible{true};
    int thickness{3};
    int opacityPercent{85};
};

struct RegionSelectionUiState {
    int outsideDimmingPercent{30};
};

struct DisplayUiState {
    int windowsDpiPercent{100};
    int userScalePercent{100};
    int effectiveScalePercent{100};
    std::string_view status;
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
    bool quickCapture{};
    bool applyScreenshotShortcutDestination{};
    bool applyCloseToTray{};
    int listenHotkeyAction{-1};
    bool cancelHotkeyCapture{};
    int changeHotkeyAction{-1};
    unsigned hotkeyModifiers{};
    unsigned hotkeyVirtualKey{};
    int clearHotkeyAction{-1};
    int resetHotkeyAction{-1};
    bool resetHotkeys{};
    bool applyBorderSettings{};
    bool resetBorderSettings{};
    bool applyRegionSelectionSettings{};
    bool resetRegionSelectionSettings{};
    bool applyUiScale{};
    bool resetUiScale{};
    bool applyRecordingSettings{};
    bool resetVideoSettings{};
    bool resetGifSettings{};
    bool resetAllSettings{};
    bool borderVisible{true};
    int borderThickness{3};
    int borderOpacityPercent{85};
    int regionOutsideDimmingPercent{30};
    int uiScalePercent{100};
    ScreenshotDestination screenshotShortcutDestination{ScreenshotDestination::Clipboard};
    bool closeToTray{};
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
                                 const RecordingPreferences& recordingPreferences,
                                 const HotkeyUiState& hotkeys,
                                 const BorderUiState& border,
                                 const RegionSelectionUiState& regionSelection,
                                 const DisplayUiState& display,
                                 const ScreenshotUiState& screenshot,
                                 const TrayUiState& tray,
                                 CaptureTargetPicker& picker, WindowsGraphicsCapture& capture,
                                 ID3D11Device* device);
};

} // namespace opencapture
