#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "gpu/d3d11_frame_processor.h"
#include "image/screenshot_service.h"
#include "audio/audio_timeline_mixer.h"
#include "audio/wasapi_capture.h"
#include "encoder/ffmpeg_audio_encoder.h"
#include "encoder/ffmpeg_d3d11_encoder.h"
#include "encoder/ffmpeg_encoder_registry.h"
#include "encoder/ffmpeg_muxer.h"
#include "encoder/ffmpeg_gif_converter.h"
#include "core/hotkey.h"
#include "core/recording_options.h"
#include "core/session_state.h"
#include "core/ui_scale.h"
#include "platform/capture_target_overlay.h"
#include "platform/capture_target_picker.h"
#include "platform/global_hotkeys.h"
#include "platform/system_tray.h"
#include "platform/windows_graphics_capture.h"
#include "ui/main_panel.h"

#include <cstdint>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace opencapture {

enum class ScreenshotRequestOrigin {
    MainPanel,
    SelectedTargetHotkey,
    QuickCapture,
};

struct ScreenshotRequest {
    ScreenshotDestination destination{ScreenshotDestination::Clipboard};
    CaptureTarget target{};
    ScreenshotRequestOrigin origin{ScreenshotRequestOrigin::MainPanel};
};

class Win32D3D11App final {
public:
    Win32D3D11App() = default;
    ~Win32D3D11App();

    Win32D3D11App(const Win32D3D11App&) = delete;
    Win32D3D11App& operator=(const Win32D3D11App&) = delete;

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateDeviceAndSwapChain();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void HandleDeviceFailure(HRESULT result);
    void ProcessCaptureFrames();
    void PumpRealtimePipeline();
    bool ProcessRecordingFrame(CapturedFrame frame);
    bool ProcessScreenshotFrame(const CapturedFrame& frame);
    bool SendRecordingFrame(const ProcessedFrame& frame, std::int64_t presentationTimestamp);
    void PumpRecordingClock();
    void PumpRecordingAudio(bool finalDrain = false);
    bool StartRecording(int framesPerSecond, int quality, std::string requestedEncoder = {},
                        bool systemAudio = true, bool microphone = false, bool remuxToMp4 = false,
                        bool gif = false, int gifHeight = 720, int gifColors = 256);
    bool PauseRecording();
    bool ResumeRecording();
    void StopRecording();
    void FailRecording(std::string error);
    [[nodiscard]] bool RecordingActive() const noexcept;
    [[nodiscard]] std::string MakeRecordingPath() const;
    [[nodiscard]] std::string MakeWorkingRecordingPath(const std::string& finalPath) const;
    bool HasRecordingSpace(const std::string& path, std::uint64_t minimumBytes);
    bool CommitRecordingFile();
    bool InitializeOutputDirectory();
    bool ChooseOutputDirectory();
    bool SaveOutputDirectory() const;
    void ScanRecoverableRecordings();
    bool RecoverRecording(std::size_t index);
    bool RemuxRecordingToMp4(std::string_view sourcePath);
    bool ConvertRecordingToGif();
    void PollMediaJob();
    void CancelMediaJob();
    [[nodiscard]] std::int64_t EffectiveRecordingDelta(std::int64_t qpc) const noexcept;
    [[nodiscard]] std::wstring MakeScreenshotPath() const;
    bool StartScreenshot(ScreenshotRequest request);
    bool RunRegionSelection(bool persistent, CaptureTarget* temporaryTarget = nullptr);
    bool StartQuickCapture();
    bool RunNvencSmoke(const ProcessedFrame& frame);
    bool RunEncoderFallbackSmoke();
    bool RunScreenshotSmoke();
    void HandleHotkey(HotkeyAction action);
    void LoadUiScaleSettings();
    bool SaveUiScaleSettings() const;
    void LoadScreenshotSettings();
    bool SaveScreenshotSettings() const;
    void LoadTraySettings();
    bool SaveTraySettings() const;
    void LoadRecordingSettings();
    bool SaveRecordingSettings() const;
    void ApplyRecordingPreferencesFromCommand(const MainPanelCommand& command);
    void ResetAllSettings();
    void BeginHotkeyCapture(int action);
    void CancelHotkeyCapture();
    void CompleteHotkeyCapture(HotkeyChord chord);
    bool HandleHotkeyCaptureMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ShowMainWindow();
    void ApplyUiScale();
    bool SetUiScalePercent(int percent);
    void RefreshWindowDpi();
    void Render();
    void Shutdown();

    HINSTANCE instance_{};
    HWND window_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    D3D11FrameProcessor frameProcessor_;
    ScreenshotService screenshotService_;
    std::optional<ProcessedFrame> processedFrame_;
    std::uint64_t processedFrameCount_{};
    std::string gpuName_;
    std::string ffmpegVersion_;
    std::string encoderSummary_;
    std::string frameProcessingError_;
    std::string screenshotStatus_;
    std::string targetOverlayStatus_;
    std::string audioStatus_;
    std::string recoveryStatus_;
    std::string remuxStatus_;
    std::vector<RecoverableRecordingUiItem> recoverableRecordings_;
    std::wstring outputDirectory_;
    std::string outputDirectoryUtf8_;
    std::optional<ScreenshotRequest> pendingScreenshot_;
    bool screenshotOwnsCapture_{};
    ScreenshotDestination screenshotHotkeyDestination_{ScreenshotDestination::Clipboard};
    std::uint32_t adapterVendorId_{};
    FFmpegEncoderRegistry encoderRegistry_;
    FFmpegD3D11Encoder videoEncoder_;
    FFmpegAudioEncoder audioEncoder_;
    FFmpegMuxer muxer_;
    FFmpegGifConverter gifConverter_;
    WasapiCapture systemAudioCapture_;
    WasapiCapture microphoneCapture_;
    AudioTimelineMixer audioMixer_;
    bool systemAudioEnabled_{};
    bool microphoneEnabled_{};
    bool recordingRemuxToMp4_{};
    bool recordingGif_{};
    int recordingMaximumHeight_{};
    int gifColors_{256};
    double gifDurationLimit_{30.0};
    std::string gifOutputPath_;
    std::string gifStatus_;
    std::jthread mediaWorker_;
    std::atomic<double> mediaProgress_{};
    std::atomic<bool> mediaJobRunning_{};
    std::atomic<bool> mediaJobFinished_{};
    std::mutex mediaResultMutex_;
    bool mediaJobSucceeded_{};
    std::string mediaJobResult_;
    std::string mediaJobDestination_;
    std::uint64_t encodedPacketCount_{};
    std::string recordSmokePath_;
    SessionState recordingState_;
    std::string recordingPath_;
    std::string recordingWorkingPath_;
    std::string requestedEncoderName_;
    std::string activeEncoderName_;
    int recordingFramesPerSecond_{60};
    std::int64_t recordingBitRate_{10'000'000};
    std::int64_t recordingQpcFrequency_{};
    std::int64_t recordingStartQpc_{};
    std::int64_t recordingPausedQpc_{};
    std::int64_t recordingPauseStartQpc_{};
    std::int64_t recordingLastPts_{-1};
    std::uint64_t recordingFrameCount_{};
    std::uint64_t recordingSourceFrameCount_{};
    std::uint64_t recordingSkippedFrameTicks_{};
    std::int64_t recordingPreviousSourceQpc_{};
    double recordingMaximumSourceGapMilliseconds_{};
    double recordingElapsedSeconds_{};
    CaptureTargetPicker targetPicker_;
    CaptureTargetOverlay targetOverlay_;
    GlobalHotkeys globalHotkeys_;
    SystemTray systemTray_;
    std::uint32_t pendingHotkeyActions_{};
    int capturingHotkeyAction_{-1};
    RecordingPreferences recordingPreferences_{};
    bool regionSelectionActive_{};
    bool closeToTray_{};
    bool exitRequested_{};
    bool automatedMode_{};
    std::string trayStatus_;
    UINT windowDpi_{96};
    int uiScalePercent_{100};
    float effectiveUiScale_{1.0F};
    bool uiScaleDirty_{true};
    std::string uiScaleStatus_;
    WindowsGraphicsCapture capture_;
    bool captureSmokeMode_{};
    bool gpuCropSmokeMode_{};
    bool gpuNv12SmokeMode_{};
    bool nvencSmokeMode_{};
    bool recordSmokeMode_{};
    bool realtimeRecordSmokeMode_{};
    bool pauseRecordSmokeMode_{};
    bool mp4RecordSmokeMode_{};
    bool recordFailureSmokeMode_{};
    bool encoderFallbackSmokeMode_{};
    bool avMuxSmokeMode_{};
    bool screenshotSmokeMode_{};
    bool recoverySmokeMode_{};
    bool remuxSmokeMode_{};
    bool gifConvertSmokeMode_{};
    bool gifCancelSmokeMode_{};
    bool gifRecordSmokeMode_{};
    bool realtimeRecordSmokeComplete_{};
    bool pauseSmokeStarted_{};
    bool pauseSmokeResumed_{};
    std::int64_t pauseSmokeWallStartQpc_{};
    bool captureSmokeFailed_{};
    bool initialized_{};
};

} // namespace opencapture
