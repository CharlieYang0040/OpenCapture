#pragma once

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

struct MainPanelCommand {
    bool chooseOutputDirectory{};
    int recoverRecordingIndex{-1};
    bool startRecording{};
    bool stopRecording{};
    bool pauseRecording{};
    bool resumeRecording{};
    bool remuxLastRecording{};
    bool remuxToMp4{};
    bool startGif{};
    bool copyScreenshot{};
    bool saveScreenshot{};
    bool saveAndCopyScreenshot{};
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
                                 std::string_view audioStatus,
                                 std::string_view recoveryStatus,
                                 std::string_view remuxStatus,
                                 std::string_view gifStatus,
                                 std::string_view outputDirectory,
                                 const std::vector<RecoverableRecordingUiItem>& recoverableRecordings,
                                 const RecordingUiState& recording,
                                 CaptureTargetPicker& picker, WindowsGraphicsCapture& capture,
                                 HWND owner, ID3D11Device* device);
};

} // namespace opencapture
