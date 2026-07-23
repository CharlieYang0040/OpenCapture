#include "platform/win32_d3d11_app.h"

#include "ui/main_panel.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern "C" {
#include <libavutil/avutil.h>
}

#include <array>
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <winrt/base.h>
#include <ShlObj.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace opencapture {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenCaptureWindow";

std::string ToUtf8(const wchar_t* text) {
    const int length = static_cast<int>(std::wcslen(text));
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), required, nullptr, nullptr);
    return result;
}

void WriteSmokeFailure(std::string_view message) {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetEnvironmentVariableW(L"OPENCAPTURE_SMOKE_LOG", path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return;
    std::ofstream output(std::filesystem::path(path.data()), std::ios::trunc);
    output << message;
}

std::string ReadEnvironmentUtf8(const wchar_t* name) {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    return length > 0 && length < value.size() ? ToUtf8(value.data()) : std::string{};
}

} // namespace

Win32D3D11App::~Win32D3D11App() { Shutdown(); }

bool Win32D3D11App::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    captureSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--wgc-smoke") != nullptr;
    gpuCropSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--gpu-crop-smoke") != nullptr;
    gpuNv12SmokeMode_ = std::wcsstr(GetCommandLineW(), L"--gpu-nv12-smoke") != nullptr;
    nvencSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--nvenc-smoke") != nullptr;
    recordSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--record-smoke") != nullptr;
    realtimeRecordSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--realtime-record-smoke") != nullptr;
    encoderFallbackSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--encoder-fallback-smoke") != nullptr;
    nvencSmokeMode_ = nvencSmokeMode_ || recordSmokeMode_;
    if (recordSmokeMode_) recordSmokePath_ = ReadEnvironmentUtf8(L"OPENCAPTURE_RECORD_SMOKE");
    gpuNv12SmokeMode_ = gpuNv12SmokeMode_ || nvencSmokeMode_;
    gpuCropSmokeMode_ = gpuCropSmokeMode_ || gpuNv12SmokeMode_;
    captureSmokeMode_ = captureSmokeMode_ || gpuCropSmokeMode_;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return false;

    window_ = CreateWindowW(kWindowClass, L"OpenCapture", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 720, 520, nullptr, nullptr, instance_, this);
    if (!window_ || !CreateDeviceAndSwapChain() || !frameProcessor_.Initialize(device_.Get(), context_.Get())) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(window_) || !ImGui_ImplDX11_Init(device_.Get(), context_.Get())) return false;

    ffmpegVersion_ = av_version_info();
    encoderRegistry_.Probe(adapterVendorId_);
    encoderSummary_ = encoderRegistry_.Summary();
    initialized_ = true;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    if (encoderFallbackSmokeMode_) {
        captureSmokeFailed_ = !RunEncoderFallbackSmoke();
        if (captureSmokeFailed_) WriteSmokeFailure(frameProcessingError_);
        PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (captureSmokeMode_) {
        CaptureTarget smokeTarget{};
        smokeTarget.monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY);
        smokeTarget.type = gpuCropSmokeMode_ ? CaptureTargetType::Region : CaptureTargetType::Monitor;
        if (gpuCropSmokeMode_) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(smokeTarget.monitor, &info)) {
                const LONG centerX = (info.rcMonitor.left + info.rcMonitor.right) / 2;
                const LONG centerY = (info.rcMonitor.top + info.rcMonitor.bottom) / 2;
                const LONG halfWidth = gpuNv12SmokeMode_ ? 320 : 160;
                const LONG halfHeight = gpuNv12SmokeMode_ ? 180 : 90;
                smokeTarget.region = RECT{centerX - halfWidth, centerY - halfHeight,
                                          centerX + halfWidth, centerY + halfHeight};
            }
        }
        captureSmokeFailed_ = !capture_.Start(smokeTarget, device_.Get());
        if (captureSmokeFailed_) PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (realtimeRecordSmokeMode_) {
        targetPicker_.Refresh();
        const auto& monitors = targetPicker_.Monitors();
        auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorEntry& monitor) { return monitor.primary; });
        if (primary == monitors.end() ||
            !targetPicker_.SelectMonitor(static_cast<std::size_t>(std::distance(monitors.begin(), primary))) ||
            !StartRecording(60, 1, ReadEnvironmentUtf8(L"OPENCAPTURE_VIDEO_ENCODER"))) {
            captureSmokeFailed_ = true;
            WriteSmokeFailure(recordingState_.Error());
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
    }
    return true;
}

int Win32D3D11App::Run() {
    MSG message{};
    while (message.message != WM_QUIT) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else {
            Render();
        }
    }
    return captureSmokeFailed_ ? 2 : static_cast<int>(message.wParam);
}

bool Win32D3D11App::CreateDeviceAndSwapChain() {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL selectedLevel{};
    constexpr std::array requestedLevels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, requestedLevels.data(),
        static_cast<UINT>(requestedLevels.size()), D3D11_SDK_VERSION, &description,
        &swapChain_, &device_, &selectedLevel, &context_);
    if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, requestedLevels.data(),
            static_cast<UINT>(requestedLevels.size()), D3D11_SDK_VERSION, &description,
            &swapChain_, &device_, &selectedLevel, &context_);
    }
    if (FAILED(result)) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(device_.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC adapterDescription{};
        if (SUCCEEDED(adapter->GetDesc(&adapterDescription))) {
            gpuName_ = ToUtf8(adapterDescription.Description);
            adapterVendorId_ = adapterDescription.VendorId;
        }
    }
    CreateRenderTarget();
    return renderTarget_ != nullptr;
}

void Win32D3D11App::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
}

void Win32D3D11App::CleanupRenderTarget() { renderTarget_.Reset(); }

void Win32D3D11App::HandleDeviceFailure(HRESULT result) {
    const HRESULT removedReason = device_ ? device_->GetDeviceRemovedReason() : result;
    std::ostringstream message;
    message << "D3D11 device lost (Present=0x" << std::hex << static_cast<unsigned long>(result)
            << ", reason=0x" << static_cast<unsigned long>(removedReason) << "). Restart capture after the device is restored.";
    frameProcessingError_ = message.str();
    if (RecordingActive()) FailRecording(frameProcessingError_);
    else capture_.Stop();
    if (captureSmokeMode_) {
        captureSmokeFailed_ = true;
        PostMessageW(window_, WM_CLOSE, 0, 0);
    }
}

bool Win32D3D11App::RecordingActive() const noexcept {
    const auto phase = recordingState_.Phase();
    return phase == SessionPhase::Starting || phase == SessionPhase::Recording ||
           phase == SessionPhase::Paused || phase == SessionPhase::Stopping;
}

std::string Win32D3D11App::MakeRecordingPath() const {
    if (const auto overridePath = ReadEnvironmentUtf8(L"OPENCAPTURE_RECORD_OUTPUT"); !overridePath.empty()) {
        return overridePath;
    }
    PWSTR videosPath{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &videosPath))) return {};
    std::filesystem::path directory(videosPath);
    CoTaskMemFree(videosPath);
    directory /= L"OpenCapture";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t fileName[96]{};
    swprintf_s(fileName, L"OpenCapture_%04u%02u%02u_%02u%02u%02u_%03u.mkv",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, time.wMilliseconds);
    directory /= fileName;
    return ToUtf8(directory.c_str());
}

bool Win32D3D11App::StartRecording(int framesPerSecond, int quality, std::string requestedEncoder) {
    if (recordingState_.Phase() == SessionPhase::Failed) recordingState_.Reset();
    if (!recordingState_.BeginStart()) return false;
    const auto candidates = encoderRegistry_.H264Candidates(requestedEncoder);
    if (!targetPicker_.Selected().IsValid() || candidates.empty()) {
        FailRecording(requestedEncoder.empty()
                          ? "Select a valid capture target and ensure an H.264 encoder is available."
                          : "The requested H.264 encoder is not available on the active adapter.");
        return false;
    }
    recordingPath_ = MakeRecordingPath();
    if (recordingPath_.empty()) {
        FailRecording("Could not create the OpenCapture folder under Videos.");
        return false;
    }
    recordingFramesPerSecond_ = std::clamp(framesPerSecond, 15, 120);
    recordingFrameCount_ = 0;
    recordingElapsedSeconds_ = 0.0;
    recordingStartQpc_ = 0;
    recordingLastPts_ = -1;
    requestedEncoderName_ = std::move(requestedEncoder);
    activeEncoderName_.clear();
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    recordingQpcFrequency_ = frequency.QuadPart;
    processedFrame_.reset();
    muxer_.Close();
    videoEncoder_.Close();
    capture_.Stop();
    if (!capture_.Start(targetPicker_.Selected(), device_.Get())) {
        FailRecording(capture_.LastError().empty() ? "Could not start Windows Graphics Capture." : capture_.LastError());
        return false;
    }
    static constexpr std::array<std::int64_t, 3> bitRates{6'000'000, 10'000'000, 16'000'000};
    recordingBitRate_ = bitRates[static_cast<std::size_t>(std::clamp(quality, 0, 2))];
    frameProcessingError_.clear();
    return true;
}

void Win32D3D11App::FailRecording(std::string error) {
    capture_.Stop();
    videoEncoder_.Close();
    muxer_.Close();
    frameProcessingError_ = error;
    recordingState_.Fail(std::move(error));
}

void Win32D3D11App::StopRecording() {
    if (!RecordingActive()) return;
    recordingState_.BeginStop();
    capture_.Stop();
    if (videoEncoder_.IsOpen() && !videoEncoder_.Flush()) {
        FailRecording(videoEncoder_.LastError());
        return;
    }
    if (muxer_.IsOpen() && !muxer_.Finalize()) {
        FailRecording(muxer_.LastError());
        return;
    }
    encodedPacketCount_ = videoEncoder_.PacketCount();
    muxer_.Close();
    videoEncoder_.Close();
    recordingState_.MarkStopped();
}

bool Win32D3D11App::ProcessRecordingFrame(CapturedFrame frame) {
    FrameProcessOptions options{};
    options.pixelFormat = FramePixelFormat::Nv12;
    auto processed = frameProcessor_.Process(frame, capture_.ActiveTarget(), options);
    if (!processed) {
        FailRecording(frameProcessor_.LastError());
        return false;
    }
    if (recordingState_.Phase() == SessionPhase::Starting) {
        std::ostringstream failures;
        for (const auto* candidate : encoderRegistry_.H264Candidates(requestedEncoderName_)) {
            if (videoEncoder_.Open(candidate->name, device_.Get(), processed->texture.Get(),
                                   processed->contentSize, recordingFramesPerSecond_, recordingBitRate_)) {
                activeEncoderName_ = candidate->displayName;
                break;
            }
            if (failures.tellp() > 0) failures << " | ";
            failures << candidate->name << ": " << videoEncoder_.LastError();
        }
        if (!videoEncoder_.IsOpen()) {
            FailRecording("No H.264 encoder accepted the D3D11 frame path. " + failures.str());
            return false;
        }
        if (!muxer_.Open(recordingPath_, videoEncoder_.CodecContext())) {
            FailRecording(muxer_.LastError());
            return false;
        }
        videoEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteVideoPacket(packet); });
        recordingStartQpc_ = frame.qpcTimestamp;
        recordingState_.MarkRecording();
    }
    const auto delta = std::max<std::int64_t>(0, frame.qpcTimestamp - recordingStartQpc_);
    std::int64_t pts = QpcDeltaToFramePts(delta, recordingQpcFrequency_, recordingFramesPerSecond_);
    if (processedFrame_) {
        while (recordingLastPts_ + 1 < pts) {
            if (!SendRecordingFrame(*processedFrame_, recordingLastPts_ + 1)) return false;
        }
    }
    if (pts <= recordingLastPts_) pts = recordingLastPts_ + 1;
    if (!SendRecordingFrame(*processed, pts)) return false;
    recordingElapsedSeconds_ = static_cast<double>(delta) / static_cast<double>(recordingQpcFrequency_);
    processedFrame_ = std::move(processed);
    return true;
}

bool Win32D3D11App::SendRecordingFrame(const ProcessedFrame& frame, std::int64_t presentationTimestamp) {
    if (!videoEncoder_.Send(frame, presentationTimestamp)) {
        FailRecording(videoEncoder_.LastError());
        return false;
    }
    recordingLastPts_ = presentationTimestamp;
    ++recordingFrameCount_;
    return true;
}

void Win32D3D11App::PumpRecordingClock() {
    if (recordingState_.Phase() != SessionPhase::Recording || !processedFrame_ || recordingQpcFrequency_ <= 0) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto delta = std::max<std::int64_t>(0, now.QuadPart - recordingStartQpc_);
    const auto desiredPts = QpcDeltaToFramePts(delta, recordingQpcFrequency_, recordingFramesPerSecond_);
    int emitted{};
    while (recordingLastPts_ < desiredPts && emitted < 4) {
        if (!SendRecordingFrame(*processedFrame_, recordingLastPts_ + 1)) return;
        ++emitted;
    }
    recordingElapsedSeconds_ = static_cast<double>(delta) / static_cast<double>(recordingQpcFrequency_);
}

bool Win32D3D11App::RunNvencSmoke(const ProcessedFrame& frame) {
    const auto* selected = encoderRegistry_.SelectedH264();
    if (!selected || selected->name != "h264_nvenc") {
        frameProcessingError_ = "H.264 NVENC is not available for the active D3D11 adapter.";
        return false;
    }
    if (!videoEncoder_.Open(selected->name, device_.Get(), frame.texture.Get(),
                            frame.contentSize, 60, 8'000'000)) {
        frameProcessingError_ = videoEncoder_.LastError();
        return false;
    }
    if (recordSmokeMode_) {
        if (recordSmokePath_.empty()) {
            frameProcessingError_ = "OPENCAPTURE_RECORD_SMOKE must specify the output MKV path.";
            return false;
        }
        if (!muxer_.Open(recordSmokePath_, videoEncoder_.CodecContext())) {
            frameProcessingError_ = muxer_.LastError();
            return false;
        }
        videoEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteVideoPacket(packet); });
    }

    const int frameCount = recordSmokeMode_ ? 60 : 1;
    for (int index = 0; index < frameCount; ++index) {
        if (!videoEncoder_.Send(frame, index)) {
            frameProcessingError_ = videoEncoder_.LastError();
            return false;
        }
    }
    if (!videoEncoder_.Flush()) {
        frameProcessingError_ = videoEncoder_.LastError();
        return false;
    }
    encodedPacketCount_ = videoEncoder_.PacketCount();
    if (encodedPacketCount_ == 0) {
        frameProcessingError_ = "NVENC did not produce an encoded packet.";
        return false;
    }
    if (recordSmokeMode_ && !muxer_.Finalize()) {
        frameProcessingError_ = muxer_.LastError();
        return false;
    }
    muxer_.Close();
    videoEncoder_.Close();
    return true;
}

bool Win32D3D11App::RunEncoderFallbackSmoke() {
    const auto requested = ReadEnvironmentUtf8(L"OPENCAPTURE_VIDEO_ENCODER");
    const auto outputPath = ReadEnvironmentUtf8(L"OPENCAPTURE_RECORD_OUTPUT");
    const auto candidates = encoderRegistry_.H264Candidates(requested);
    if (requested.empty() || outputPath.empty() || candidates.empty()) {
        frameProcessingError_ = "Encoder fallback smoke requires an available OPENCAPTURE_VIDEO_ENCODER and OPENCAPTURE_RECORD_OUTPUT.";
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = 320;
    description.Height = 180;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_NV12;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ProcessedFrame frame{};
    frame.contentSize = SIZE{320, 180};
    frame.textureFormat = DXGI_FORMAT_NV12;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &frame.texture))) {
        frameProcessingError_ = "Could not create the synthetic NV12 encoder smoke texture.";
        return false;
    }
    if (!videoEncoder_.Open(candidates.front()->name, device_.Get(), frame.texture.Get(), frame.contentSize,
                            60, 2'000'000)) {
        frameProcessingError_ = videoEncoder_.LastError();
        return false;
    }
    if (!muxer_.Open(outputPath, videoEncoder_.CodecContext())) {
        frameProcessingError_ = muxer_.LastError();
        videoEncoder_.Close();
        return false;
    }
    videoEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteVideoPacket(packet); });
    for (std::int64_t pts = 0; pts < 60; ++pts) {
        if (!videoEncoder_.Send(frame, pts)) {
            frameProcessingError_ = videoEncoder_.LastError();
            muxer_.Close();
            videoEncoder_.Close();
            return false;
        }
    }
    if (!videoEncoder_.Flush() || !muxer_.Finalize()) {
        frameProcessingError_ = videoEncoder_.LastError().empty() ? muxer_.LastError() : videoEncoder_.LastError();
        muxer_.Close();
        videoEncoder_.Close();
        return false;
    }
    muxer_.Close();
    videoEncoder_.Close();
    return true;
}

void Win32D3D11App::ProcessCaptureFrames() {
    if (RecordingActive()) {
        while (auto frame = capture_.TryPopFrame()) {
            if (!ProcessRecordingFrame(std::move(*frame))) break;
        }
        return;
    }
    std::optional<CapturedFrame> latest;
    while (auto frame = capture_.TryPopFrame()) latest = std::move(frame);
    if (!latest) return;
    FrameProcessOptions options{};
    if (gpuNv12SmokeMode_) {
        options.outputSize = SIZE{320, 180};
        options.pixelFormat = FramePixelFormat::Nv12;
    }
    const int processPasses = gpuNv12SmokeMode_ && !nvencSmokeMode_ ? 3 : 1;
    for (int pass = 0; pass < processPasses; ++pass) {
        auto processed = frameProcessor_.Process(*latest, capture_.ActiveTarget(), options);
        if (!processed) {
            frameProcessingError_ = frameProcessor_.LastError();
            capture_.Stop();
            if (gpuCropSmokeMode_) {
                captureSmokeFailed_ = true;
                PostMessageW(window_, WM_CLOSE, 0, 0);
            }
            return;
        }
        if (gpuCropSmokeMode_ &&
            (processed->contentSize.cx != 320 || processed->contentSize.cy != 180 ||
             (gpuNv12SmokeMode_ && processed->textureFormat != DXGI_FORMAT_NV12))) {
            captureSmokeFailed_ = true;
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }
        processedFrame_ = std::move(processed);
        ++processedFrameCount_;
        frameProcessingError_.clear();
        if (nvencSmokeMode_) {
            if (!RunNvencSmoke(*processedFrame_)) {
                WriteSmokeFailure(frameProcessingError_);
                captureSmokeFailed_ = true;
                PostMessageW(window_, WM_CLOSE, 0, 0);
                return;
            }
        }
    }
}

void Win32D3D11App::Render() {
    ProcessCaptureFrames();
    PumpRecordingClock();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const auto recordingPhase = recordingState_.Phase();
    const auto recordingError = recordingState_.Error();
    const RecordingUiState recordingUi{
        RecordingActive(), recordingPhase == SessionPhase::Starting, recordingPath_, recordingError,
        activeEncoderName_, recordingFrameCount_, recordingElapsedSeconds_};
    std::vector<EncoderUiChoice> encoderChoices;
    for (const auto& capability : encoderRegistry_.Capabilities()) {
        if (capability.codec == VideoCodecFamily::H264) {
            encoderChoices.push_back({capability.name, capability.displayName, capability.usable, capability.detail});
        }
    }
    const auto command = MainPanel::Draw(gpuName_, ffmpegVersion_, encoderSummary_, encoderChoices, frameProcessingError_,
                                         recordingUi, targetPicker_, capture_, window_, device_.Get());
    if (command.startRecording) StartRecording(command.framesPerSecond, command.quality, command.encoderName);
    if (command.stopRecording) StopRecording();

    if (captureSmokeMode_ &&
        ((!gpuCropSmokeMode_ && capture_.FrameCount() >= 1) ||
         (gpuCropSmokeMode_ && !gpuNv12SmokeMode_ && processedFrameCount_ >= 1) ||
         (gpuNv12SmokeMode_ && !nvencSmokeMode_ && processedFrameCount_ >= 3) ||
         (nvencSmokeMode_ && encodedPacketCount_ >= 1))) {
        PostMessageW(window_, WM_CLOSE, 0, 0);
    }
    if (realtimeRecordSmokeMode_ && !realtimeRecordSmokeComplete_) {
        if (recordingState_.Phase() == SessionPhase::Failed) {
            captureSmokeFailed_ = true;
            WriteSmokeFailure(recordingState_.Error());
            PostMessageW(window_, WM_CLOSE, 0, 0);
        } else if (recordingElapsedSeconds_ >= 1.0 && recordingFrameCount_ >= 55) {
            StopRecording();
            realtimeRecordSmokeComplete_ = true;
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
    }

    ImGui::Render();
    constexpr float clearColor[4]{0.055F, 0.065F, 0.08F, 1.0F};
    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT presentResult = swapChain_->Present(1, 0);
    if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET ||
        presentResult == DXGI_ERROR_DEVICE_HUNG) {
        HandleDeviceFailure(presentResult);
    }
}

void Win32D3D11App::Shutdown() {
    StopRecording();
    capture_.Stop();
    muxer_.Close();
    videoEncoder_.Close();
    processedFrame_.reset();
    frameProcessor_.Reset();
    if (initialized_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }
    CleanupRenderTarget();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (instance_) {
        UnregisterClassW(kWindowClass, instance_);
        instance_ = nullptr;
    }
}

LRESULT CALLBACK Win32D3D11App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return 1;
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            auto* app = reinterpret_cast<Win32D3D11App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (app && app->swapChain_) {
                app->CleanupRenderTarget();
                app->swapChain_->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                app->CreateRenderTarget();
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0U) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        break;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace opencapture
