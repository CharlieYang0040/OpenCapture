#include "platform/win32_d3d11_app.h"

#include "ui/main_panel.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
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
#include <ShObjIdl.h>
#include <wincodec.h>

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

std::wstring ReadEnvironmentWide(const wchar_t* name) {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    return length > 0 && length < value.size() ? std::wstring(value.data(), length) : std::wstring{};
}

int ReadEnvironmentInt(const wchar_t* name, int fallback) {
    const auto value = ReadEnvironmentWide(name);
    if (value.empty()) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::uint64_t QpcTo100ns(std::int64_t qpc, std::int64_t frequency) {
    if (qpc <= 0 || frequency <= 0) return 0;
    const auto seconds = qpc / frequency;
    const auto remainder = qpc % frequency;
    return static_cast<std::uint64_t>(seconds) * 10'000'000ULL +
           static_cast<std::uint64_t>(remainder) * 10'000'000ULL /
               static_cast<std::uint64_t>(frequency);
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), required);
    return result;
}

std::string FFmpegError(int result) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(result, text.data(), text.size());
    return text.data();
}

std::filesystem::path OutputSettingsPath() {
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) return {};
    std::filesystem::path directory(localAppData);
    CoTaskMemFree(localAppData);
    directory /= L"OpenCapture";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory / L"output_directory.txt";
}

std::filesystem::path DefaultOutputDirectory() {
    PWSTR videos{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &videos))) return {};
    std::filesystem::path directory(videos);
    CoTaskMemFree(videos);
    return directory / L"OpenCapture";
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
    pauseRecordSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--pause-record-smoke") != nullptr;
    mp4RecordSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--mp4-record-smoke") != nullptr;
    recordFailureSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--record-failure-smoke") != nullptr;
    realtimeRecordSmokeMode_ = realtimeRecordSmokeMode_ || pauseRecordSmokeMode_ ||
                               mp4RecordSmokeMode_ || recordFailureSmokeMode_;
    encoderFallbackSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--encoder-fallback-smoke") != nullptr;
    avMuxSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--av-mux-smoke") != nullptr;
    encoderFallbackSmokeMode_ = encoderFallbackSmokeMode_ || avMuxSmokeMode_;
    screenshotSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--screenshot-smoke") != nullptr;
    recoverySmokeMode_ = std::wcsstr(GetCommandLineW(), L"--recovery-smoke") != nullptr;
    remuxSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--remux-smoke") != nullptr;
    gifConvertSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--gif-convert-smoke") != nullptr;
    gifCancelSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--gif-cancel-smoke") != nullptr;
    gifConvertSmokeMode_ = gifConvertSmokeMode_ || gifCancelSmokeMode_;
    gifRecordSmokeMode_ = std::wcsstr(GetCommandLineW(), L"--gif-record-smoke") != nullptr;
    realtimeRecordSmokeMode_ = realtimeRecordSmokeMode_ || gifRecordSmokeMode_;
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
                            CW_USEDEFAULT, CW_USEDEFAULT, 860, 800, nullptr, nullptr, instance_, this);
    if (!window_ || !CreateDeviceAndSwapChain() || !frameProcessor_.Initialize(device_.Get(), context_.Get())) return false;
    if (!targetOverlay_.Initialize(instance_)) {
        targetOverlayStatus_ = targetOverlay_.LastError();
    } else if (!targetOverlay_.CaptureExclusionAvailable()) {
        targetOverlayStatus_ = targetOverlay_.LastError();
    }
    globalHotkeys_.Initialize(window_);
    capture_.RequestBorderlessAccess();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(window_) || !ImGui_ImplDX11_Init(device_.Get(), context_.Get())) return false;

    ffmpegVersion_ = av_version_info();
    encoderRegistry_.Probe(adapterVendorId_);
    encoderSummary_ = encoderRegistry_.Summary();
    if (!InitializeOutputDirectory()) return false;
    ScanRecoverableRecordings();
    initialized_ = true;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    if (gifConvertSmokeMode_) {
        const auto input = ReadEnvironmentUtf8(L"OPENCAPTURE_GIF_INPUT");
        const auto output = ReadEnvironmentUtf8(L"OPENCAPTURE_GIF_OUTPUT");
        if (gifCancelSmokeMode_ && !input.empty() && !output.empty()) {
            std::stop_source cancellation;
            const bool converted = gifConverter_.Convert(
                input, output, {128}, cancellation.get_token(),
                [&](double progress) {
                    if (progress >= 0.05) cancellation.request_stop();
                });
            captureSmokeFailed_ = converted ||
                gifConverter_.LastError().find("cancelled") == std::string::npos;
        } else {
            captureSmokeFailed_ = input.empty() || output.empty() ||
                !gifConverter_.Convert(input, output, {128});
        }
        if (captureSmokeFailed_) WriteSmokeFailure(
            gifConverter_.LastError().empty()
                ? "OPENCAPTURE_GIF_INPUT and OPENCAPTURE_GIF_OUTPUT are required."
                : gifConverter_.LastError());
        PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (remuxSmokeMode_) {
        const auto input = ReadEnvironmentUtf8(L"OPENCAPTURE_REMUX_INPUT");
        captureSmokeFailed_ = input.empty() || !RemuxRecordingToMp4(input);
        if (captureSmokeFailed_) WriteSmokeFailure(
            frameProcessingError_.empty() ? "OPENCAPTURE_REMUX_INPUT is required." : frameProcessingError_);
        PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (recoverySmokeMode_) {
        captureSmokeFailed_ = recoverableRecordings_.empty() || !RecoverRecording(0);
        if (captureSmokeFailed_) WriteSmokeFailure(
            frameProcessingError_.empty() ? "No recoverable recording was found." : frameProcessingError_);
        PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (screenshotSmokeMode_) {
        captureSmokeFailed_ = !RunScreenshotSmoke();
        if (captureSmokeFailed_) WriteSmokeFailure(frameProcessingError_);
        PostMessageW(window_, WM_CLOSE, 0, 0);
    } else if (encoderFallbackSmokeMode_) {
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
            !StartRecording(gifRecordSmokeMode_ ? ReadEnvironmentInt(L"OPENCAPTURE_GIF_FPS", 12) : 60,
                            gifRecordSmokeMode_ ? 0 : 1,
                            ReadEnvironmentUtf8(L"OPENCAPTURE_VIDEO_ENCODER"),
                            !gifRecordSmokeMode_, false, mp4RecordSmokeMode_,
                            gifRecordSmokeMode_,
                            ReadEnvironmentInt(L"OPENCAPTURE_GIF_HEIGHT", 480),
                            ReadEnvironmentInt(L"OPENCAPTURE_GIF_COLORS", 128))) {
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
        const auto requested = std::filesystem::path(ToWide(overridePath));
        if (!std::filesystem::exists(requested)) return overridePath;
        for (int suffix = 1; suffix < 1000; ++suffix) {
            auto candidate = requested.parent_path() /
                (requested.stem().wstring() + L"_" + std::to_wstring(suffix) + requested.extension().wstring());
            if (!std::filesystem::exists(candidate)) return ToUtf8(candidate.c_str());
        }
        return {};
    }
    std::filesystem::path directory(outputDirectory_);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t fileName[96]{};
    swprintf_s(fileName, L"OpenCapture_%04u%02u%02u_%02u%02u%02u_%03u.mkv",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, time.wMilliseconds);
    const std::filesystem::path base = directory / fileName;
    if (!std::filesystem::exists(base)) return ToUtf8(base.c_str());
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const auto candidate = directory /
            (base.stem().wstring() + L"_" + std::to_wstring(suffix) + base.extension().wstring());
        if (!std::filesystem::exists(candidate)) return ToUtf8(candidate.c_str());
    }
    return {};
}

std::string Win32D3D11App::MakeWorkingRecordingPath(const std::string& finalPath) const {
    const std::filesystem::path final(ToWide(finalPath));
    const auto working = final.parent_path() /
        (final.stem().wstring() + L".part" + final.extension().wstring());
    return ToUtf8(working.c_str());
}

bool Win32D3D11App::HasRecordingSpace(const std::string& path, std::uint64_t minimumBytes) {
    std::error_code error;
    const auto information = std::filesystem::space(
        std::filesystem::path(ToWide(path)).parent_path(), error);
    if (error) {
        frameProcessingError_ = "Could not query free space for the recording folder.";
        return false;
    }
    if (information.available < minimumBytes) {
        std::ostringstream message;
        message << "Not enough free space to start recording. At least "
                << (minimumBytes / (1024 * 1024)) << " MiB is required.";
        frameProcessingError_ = message.str();
        return false;
    }
    return true;
}

bool Win32D3D11App::CommitRecordingFile() {
    if (recordingWorkingPath_.empty() || recordingPath_.empty()) return false;
    const std::filesystem::path working(ToWide(recordingWorkingPath_));
    const std::filesystem::path final(ToWide(recordingPath_));
    std::error_code error;
    if (!std::filesystem::exists(working, error) || error || std::filesystem::exists(final, error)) {
        frameProcessingError_ = "Could not finalize the recording name; the recoverable .part.mkv file was kept.";
        return false;
    }
    std::filesystem::rename(working, final, error);
    if (error) {
        frameProcessingError_ = "Could not finalize the recording name; the recoverable .part.mkv file was kept.";
        return false;
    }
    return true;
}

bool Win32D3D11App::InitializeOutputDirectory() {
    std::filesystem::path selected;
    if (const auto environment = ReadEnvironmentWide(L"OPENCAPTURE_OUTPUT_DIR"); !environment.empty()) {
        selected = environment;
    } else {
        const auto settingsPath = OutputSettingsPath();
        std::ifstream input(settingsPath, std::ios::binary);
        std::string saved((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        while (!saved.empty() && (saved.back() == '\r' || saved.back() == '\n')) saved.pop_back();
        if (!saved.empty()) selected = std::filesystem::path(ToWide(saved));
    }
    if (selected.empty()) selected = DefaultOutputDirectory();
    std::error_code error;
    std::filesystem::create_directories(selected, error);
    if (error) {
        selected = DefaultOutputDirectory();
        error.clear();
        std::filesystem::create_directories(selected, error);
    }
    if (selected.empty() || error) {
        frameProcessingError_ = "Could not create the output folder.";
        return false;
    }
    outputDirectory_ = selected.wstring();
    outputDirectoryUtf8_ = ToUtf8(outputDirectory_.c_str());
    return true;
}

bool Win32D3D11App::SaveOutputDirectory() const {
    const auto settingsPath = OutputSettingsPath();
    if (settingsPath.empty()) return false;
    const auto temporary = settingsPath.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << outputDirectoryUtf8_ << '\n';
    output.close();
    if (!output) return false;
    std::error_code error;
    std::filesystem::rename(temporary, settingsPath, error);
    if (error) {
        std::filesystem::remove(settingsPath, error);
        error.clear();
        std::filesystem::rename(temporary, settingsPath, error);
    }
    return !error;
}

bool Win32D3D11App::ChooseOutputDirectory() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        frameProcessingError_ = "Could not open the output folder picker.";
        return false;
    }
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose OpenCapture output folder");
    Microsoft::WRL::ComPtr<IShellItem> current;
    if (!outputDirectory_.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(outputDirectory_.c_str(), nullptr, IID_PPV_ARGS(&current)))) {
        dialog->SetFolder(current.Get());
    }
    result = dialog->Show(window_);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
    if (FAILED(result)) {
        frameProcessingError_ = "Could not select the output folder.";
        return false;
    }
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR selectedPath{};
    if (FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath)) || !selectedPath) {
        if (selectedPath) CoTaskMemFree(selectedPath);
        frameProcessingError_ = "The selected output location is not a filesystem folder.";
        return false;
    }
    std::filesystem::path selected(selectedPath);
    CoTaskMemFree(selectedPath);
    std::error_code error;
    std::filesystem::create_directories(selected, error);
    if (error) {
        frameProcessingError_ = "Could not use the selected output folder.";
        return false;
    }
    outputDirectory_ = selected.wstring();
    outputDirectoryUtf8_ = ToUtf8(outputDirectory_.c_str());
    if (!SaveOutputDirectory()) {
        frameProcessingError_ = "The output folder works, but the preference could not be saved.";
        return false;
    }
    frameProcessingError_.clear();
    ScanRecoverableRecordings();
    return true;
}

void Win32D3D11App::ScanRecoverableRecordings() {
    recoveryStatus_.clear();
    recoverableRecordings_.clear();
    const std::filesystem::path directory(outputDirectory_);
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
         iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().filename().wstring().ends_with(L".part.mkv")) {
            const auto size = iterator->file_size(error);
            if (!error) {
                recoverableRecordings_.push_back({
                    ToUtf8(iterator->path().c_str()),
                    ToUtf8(iterator->path().filename().c_str()),
                    static_cast<std::uint64_t>(size)});
            }
        }
    }
    std::sort(recoverableRecordings_.begin(), recoverableRecordings_.end(),
              [](const auto& left, const auto& right) { return left.fileName < right.fileName; });
    if (!recoverableRecordings_.empty()) {
        recoveryStatus_ = std::to_string(recoverableRecordings_.size()) +
            " recoverable .part.mkv recording(s) found in the output folder.";
    }
}

bool Win32D3D11App::RecoverRecording(std::size_t index) {
    if (RecordingActive() || index >= recoverableRecordings_.size()) {
        frameProcessingError_ = RecordingActive()
            ? "Stop recording before recovering an incomplete file."
            : "The selected recovery file no longer exists.";
        return false;
    }
    const std::filesystem::path source(ToWide(recoverableRecordings_[index].path));
    const std::wstring fileName = source.filename().wstring();
    constexpr std::wstring_view partSuffix = L".part.mkv";
    std::error_code error;
    if (!fileName.ends_with(partSuffix) || !std::filesystem::is_regular_file(source, error) || error) {
        frameProcessingError_ = "The selected recovery file is invalid or no longer exists.";
        ScanRecoverableRecordings();
        return false;
    }

    AVFormatContext* input{};
    int result = avformat_open_input(&input, recoverableRecordings_[index].path.c_str(), nullptr, nullptr);
    if (result < 0) {
        frameProcessingError_ = "Recovery validation failed: " + FFmpegError(result) +
                                ". The .part.mkv file was kept.";
        return false;
    }
    result = avformat_find_stream_info(input, nullptr);
    bool hasVideo = false;
    if (result >= 0) {
        for (unsigned int stream = 0; stream < input->nb_streams; ++stream) {
            if (input->streams[stream]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                hasVideo = true;
                break;
            }
        }
    }
    avformat_close_input(&input);
    if (result < 0 || !hasVideo) {
        frameProcessingError_ = result < 0
            ? "Recovery stream validation failed: " + FFmpegError(result) + ". The .part.mkv file was kept."
            : "Recovery requires a readable video stream. The .part.mkv file was kept.";
        return false;
    }

    const std::wstring baseName = fileName.substr(0, fileName.size() - partSuffix.size());
    std::filesystem::path destination = source.parent_path() / (baseName + L".mkv");
    const bool destinationExists = std::filesystem::exists(destination, error);
    if (error) {
        frameProcessingError_ = "Could not inspect the final recording name. The .part.mkv file was kept.";
        return false;
    }
    if (destinationExists) {
        bool available = false;
        for (int suffix = 1; suffix < 1000; ++suffix) {
            const auto candidate = source.parent_path() /
                (baseName + L"_" + std::to_wstring(suffix) + L".mkv");
            const bool candidateExists = std::filesystem::exists(candidate, error);
            if (error) {
                frameProcessingError_ = "Could not inspect an alternate recording name. The .part.mkv file was kept.";
                return false;
            }
            if (!candidateExists) {
                destination = candidate;
                available = true;
                break;
            }
        }
        if (!available) {
            frameProcessingError_ = "Could not find an available final name. The .part.mkv file was kept.";
            return false;
        }
    }
    std::filesystem::rename(source, destination, error);
    if (error) {
        frameProcessingError_ = "Could not finalize the recovered recording: " + error.message() +
                                ". The .part.mkv file was kept.";
        return false;
    }
    const auto recoveredPath = ToUtf8(destination.c_str());
    frameProcessingError_.clear();
    ScanRecoverableRecordings();
    recoveryStatus_ = "Recovered recording: " + recoveredPath;
    return true;
}

bool Win32D3D11App::RemuxRecordingToMp4(std::string_view sourcePath) {
    const std::filesystem::path source(ToWide(sourcePath));
    std::error_code error;
    if (RecordingActive() || source.extension() != L".mkv" ||
        !std::filesystem::is_regular_file(source, error) || error) {
        frameProcessingError_ = RecordingActive()
            ? "Stop recording before creating an MP4 copy."
            : "MP4 remux requires an existing MKV source file.";
        return false;
    }
    const auto sourceSize = std::filesystem::file_size(source, error);
    const auto space = std::filesystem::space(source.parent_path(), error);
    constexpr std::uint64_t remuxReserve = 64ULL * 1024 * 1024;
    if (error || space.available < sourceSize + remuxReserve) {
        frameProcessingError_ = error
            ? "Could not query free space for MP4 remux."
            : "Not enough free space for an MP4 copy; the MKV source was kept.";
        return false;
    }
    std::filesystem::path destination;
    std::filesystem::path temporary;
    bool available = false;
    for (int suffix = 0; suffix < 1000; ++suffix) {
        const std::wstring suffixText = suffix == 0 ? L"" : L"_" + std::to_wstring(suffix);
        destination = source.parent_path() / (source.stem().wstring() + suffixText + L".mp4");
        temporary = source.parent_path() / (source.stem().wstring() + suffixText + L".part.mp4");
        const bool finalExists = std::filesystem::exists(destination, error);
        if (error) break;
        const bool temporaryExists = std::filesystem::exists(temporary, error);
        if (error) break;
        if (!finalExists && !temporaryExists) {
            available = true;
            break;
        }
    }
    if (!available || error) {
        frameProcessingError_ = "Could not find an available MP4 output name.";
        return false;
    }

    const auto sourceUtf8 = ToUtf8(source.c_str());
    const auto temporaryUtf8 = ToUtf8(temporary.c_str());
    AVFormatContext* input{};
    AVFormatContext* output{};
    AVPacket* packet{};
    bool outputFileOpen = false;
    auto cleanup = [&]() {
        if (packet) av_packet_free(&packet);
        if (output) {
            if (outputFileOpen && output->pb) avio_closep(&output->pb);
            avformat_free_context(output);
            output = nullptr;
        }
        if (input) avformat_close_input(&input);
    };
    auto fail = [&](std::string message) {
        cleanup();
        std::filesystem::remove(temporary, error);
        frameProcessingError_ = std::move(message) + " The source MKV was kept.";
        return false;
    };

    int result = avformat_open_input(&input, sourceUtf8.c_str(), nullptr, nullptr);
    if (result < 0) return fail("Could not open the MKV source: " + FFmpegError(result) + ".");
    result = avformat_find_stream_info(input, nullptr);
    if (result < 0) return fail("Could not read MKV stream information: " + FFmpegError(result) + ".");
    result = avformat_alloc_output_context2(&output, nullptr, "mp4", temporaryUtf8.c_str());
    if (result < 0 || !output) {
        return fail("Could not create the MP4 container: " +
                    FFmpegError(result < 0 ? result : AVERROR_UNKNOWN) + ".");
    }
    std::vector<int> streamMap(input->nb_streams, -1);
    bool hasVideo = false;
    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        const auto type = input->streams[index]->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) continue;
        AVStream* stream = avformat_new_stream(output, nullptr);
        if (!stream) return fail("Could not create an MP4 stream.");
        result = avcodec_parameters_copy(stream->codecpar, input->streams[index]->codecpar);
        if (result < 0) return fail("Could not copy stream parameters: " + FFmpegError(result) + ".");
        stream->codecpar->codec_tag = 0;
        stream->time_base = input->streams[index]->time_base;
        streamMap[index] = stream->index;
        hasVideo = hasVideo || type == AVMEDIA_TYPE_VIDEO;
    }
    if (!hasVideo) return fail("The MKV source does not contain a video stream.");
    result = avio_open(&output->pb, temporaryUtf8.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) return fail("Could not open the temporary MP4: " + FFmpegError(result) + ".");
    outputFileOpen = true;
    result = avformat_write_header(output, nullptr);
    if (result < 0) return fail("Could not write the MP4 header: " + FFmpegError(result) + ".");
    packet = av_packet_alloc();
    if (!packet) return fail("Could not allocate an FFmpeg packet.");
    while ((result = av_read_frame(input, packet)) >= 0) {
        if (packet->stream_index < 0 ||
            static_cast<std::size_t>(packet->stream_index) >= streamMap.size() ||
            streamMap[static_cast<std::size_t>(packet->stream_index)] < 0) {
            av_packet_unref(packet);
            continue;
        }
        AVStream* inputStream = input->streams[packet->stream_index];
        AVStream* outputStream = output->streams[streamMap[static_cast<std::size_t>(packet->stream_index)]];
        av_packet_rescale_ts(packet, inputStream->time_base, outputStream->time_base);
        packet->stream_index = outputStream->index;
        packet->pos = -1;
        const int writeResult = av_interleaved_write_frame(output, packet);
        av_packet_unref(packet);
        if (writeResult < 0) return fail("Could not write an MP4 packet: " + FFmpegError(writeResult) + ".");
    }
    if (result != AVERROR_EOF) return fail("Could not finish reading the MKV source: " + FFmpegError(result) + ".");
    result = av_write_trailer(output);
    if (result < 0) return fail("Could not finalize the MP4: " + FFmpegError(result) + ".");
    cleanup();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        frameProcessingError_ = "Could not finalize the MP4 name. The source MKV was kept.";
        return false;
    }
    frameProcessingError_.clear();
    remuxStatus_ = "MP4 copy created without re-encoding: " + ToUtf8(destination.c_str());
    return true;
}

bool Win32D3D11App::ConvertRecordingToGif() {
    if (mediaJobRunning_) {
        gifStatus_ = "Another media conversion is already running.";
        return false;
    }
    const std::filesystem::path source(ToWide(recordingPath_));
    const std::filesystem::path destination(ToWide(gifOutputPath_));
    const auto temporary = destination.parent_path() /
        (destination.stem().wstring() + L".part.gif");
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        frameProcessingError_ = "The GIF source recording is missing.";
        gifStatus_ = frameProcessingError_;
        return false;
    }
    if (std::filesystem::exists(destination, error) || std::filesystem::exists(temporary, error)) {
        frameProcessingError_ = "The GIF output or temporary file already exists. The source MKV was kept.";
        gifStatus_ = frameProcessingError_;
        return false;
    }
    const auto sourceBytes = std::filesystem::file_size(source, error);
    if (error || !HasRecordingSpace(ToUtf8(temporary.c_str()),
                                    std::max<std::uint64_t>(128ULL * 1024 * 1024, sourceBytes * 2))) {
        gifStatus_ = frameProcessingError_.empty()
            ? "Not enough free space to create the GIF. The source MKV was kept."
            : frameProcessingError_ + " The source MKV was kept.";
        return false;
    }
    frameProcessingError_.clear();
    gifStatus_ = "Analyzing colors for GIF...";
    mediaProgress_ = 0.0;
    mediaJobFinished_ = false;
    mediaJobRunning_ = true;
    mediaJobDestination_ = gifOutputPath_;
    if (mediaWorker_.joinable()) mediaWorker_.join();
    const auto sourceUtf8 = ToUtf8(source.c_str());
    const auto temporaryUtf8 = ToUtf8(temporary.c_str());
    const auto destinationUtf8 = gifOutputPath_;
    const int colors = gifColors_;
    mediaWorker_ = std::jthread(
        [this, source, destination, temporary, sourceUtf8, temporaryUtf8, destinationUtf8, colors]
        (std::stop_token stopToken) {
            bool success = gifConverter_.Convert(
                sourceUtf8, temporaryUtf8, {colors}, stopToken,
                [this](double value) { mediaProgress_ = value; });
            std::string result;
            std::error_code workerError;
            if (success && stopToken.stop_requested()) {
                success = false;
                result = "GIF conversion cancelled. The source MKV was kept.";
            }
            if (success) {
                std::filesystem::rename(temporary, destination, workerError);
                if (workerError) {
                    success = false;
                    result = "Could not finalize the GIF name. The source MKV was kept.";
                }
            }
            if (success) {
                const auto gifBytes = std::filesystem::file_size(destination, workerError);
                const bool sizeKnown = !workerError;
                std::error_code removeError;
                std::filesystem::remove(source, removeError);
                std::ostringstream status;
                status << "GIF saved: " << destinationUtf8;
                if (sizeKnown) status << " (" << (gifBytes / 1024) << " KiB)";
                if (removeError) status << " The safe source MKV was also kept.";
                result = status.str();
            } else {
                std::filesystem::remove(temporary, workerError);
                if (result.empty()) {
                    result = gifConverter_.LastError() + " The source MKV was kept.";
                }
            }
            {
                std::lock_guard lock(mediaResultMutex_);
                mediaJobSucceeded_ = success;
                mediaJobResult_ = std::move(result);
            }
            mediaJobRunning_ = false;
            mediaJobFinished_ = true;
        });
    return true;
}

void Win32D3D11App::PollMediaJob() {
    if (!mediaJobFinished_.exchange(false)) return;
    if (mediaWorker_.joinable()) mediaWorker_.join();
    bool success{};
    std::string result;
    {
        std::lock_guard lock(mediaResultMutex_);
        success = mediaJobSucceeded_;
        result = mediaJobResult_;
    }
    gifStatus_ = result;
    if (success) {
        recordingPath_ = mediaJobDestination_;
        frameProcessingError_.clear();
    } else {
        frameProcessingError_ = result;
    }
    if (gifRecordSmokeMode_) {
        captureSmokeFailed_ = !success;
        if (!success) WriteSmokeFailure(result);
        realtimeRecordSmokeComplete_ = true;
        PostMessageW(window_, WM_CLOSE, 0, 0);
    }
}

void Win32D3D11App::CancelMediaJob() {
    if (!mediaJobRunning_ || !mediaWorker_.joinable()) return;
    mediaWorker_.request_stop();
    gifStatus_ = "Cancelling GIF conversion...";
}

std::wstring Win32D3D11App::MakeScreenshotPath() const {
    std::filesystem::path directory(outputDirectory_);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t fileName[96]{};
    swprintf_s(fileName, L"OpenCapture_%04u%02u%02u_%02u%02u%02u_%03u.png",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, time.wMilliseconds);
    const auto base = directory / fileName;
    if (!std::filesystem::exists(base)) return base.wstring();
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const auto candidate = directory /
            (base.stem().wstring() + L"_" + std::to_wstring(suffix) + base.extension().wstring());
        if (!std::filesystem::exists(candidate)) return candidate.wstring();
    }
    return {};
}

bool Win32D3D11App::StartScreenshot(ScreenshotDestination destination) {
    if (pendingScreenshot_) return false;
    if (!targetPicker_.Selected().IsValid()) {
        screenshotStatus_ = "Select a valid capture target first.";
        return false;
    }
    pendingScreenshot_ = destination;
    screenshotStatus_ = "Waiting for one capture frame...";
    screenshotOwnsCapture_ = false;
    if (!capture_.IsRunning()) {
        if (!capture_.Start(targetPicker_.Selected(), device_.Get())) {
            screenshotStatus_ = capture_.LastError().empty()
                                    ? "Could not start capture for the screenshot."
                                    : capture_.LastError();
            pendingScreenshot_.reset();
            return false;
        }
        screenshotOwnsCapture_ = true;
    }
    return true;
}

bool Win32D3D11App::ProcessScreenshotFrame(const CapturedFrame& frame) {
    if (!pendingScreenshot_) return true;
    FrameProcessOptions options{};
    options.pixelFormat = FramePixelFormat::Bgra;
    auto processed = frameProcessor_.Process(frame, capture_.ActiveTarget(), options);
    if (!processed) {
        screenshotStatus_ = frameProcessor_.LastError();
        pendingScreenshot_.reset();
        if (screenshotOwnsCapture_) capture_.Stop();
        screenshotOwnsCapture_ = false;
        return false;
    }
    const auto destination = *pendingScreenshot_;
    const auto path = destination == ScreenshotDestination::Clipboard ? std::wstring{} : MakeScreenshotPath();
    const bool success = screenshotService_.Capture(device_.Get(), context_.Get(), processed->texture.Get(),
                                                    processed->contentSize, destination, path, window_);
    if (success) {
        if (destination == ScreenshotDestination::Clipboard) {
            screenshotStatus_ = "Screenshot copied to the clipboard without creating a file.";
        } else if (destination == ScreenshotDestination::File) {
            screenshotStatus_ = "PNG saved: " + ToUtf8(path.c_str());
        } else {
            screenshotStatus_ = "PNG saved and copied: " + ToUtf8(path.c_str());
        }
    } else {
        screenshotStatus_ = screenshotService_.LastError();
    }
    pendingScreenshot_.reset();
    targetOverlay_.FlashScreenshot();
    if (screenshotOwnsCapture_) capture_.Stop();
    screenshotOwnsCapture_ = false;
    return success;
}

bool Win32D3D11App::StartRecording(int framesPerSecond, int quality, std::string requestedEncoder,
                                   bool systemAudio, bool microphone, bool remuxToMp4,
                                   bool gif, int gifHeight, int gifColors) {
    if (recordingState_.Phase() == SessionPhase::Failed) recordingState_.Reset();
    if (!recordingState_.BeginStart()) return false;
    const auto candidates = encoderRegistry_.H264Candidates(requestedEncoder);
    if (!targetPicker_.Selected().IsValid() || candidates.empty()) {
        FailRecording(requestedEncoder.empty()
                          ? "Select a valid capture target and ensure an H.264 encoder is available."
                          : "The requested H.264 encoder is not available on the active adapter.");
        return false;
    }
    static constexpr std::array<std::int64_t, 3> bitRates{6'000'000, 10'000'000, 16'000'000};
    recordingBitRate_ = bitRates[static_cast<std::size_t>(std::clamp(quality, 0, 2))];
    recordingPath_ = MakeRecordingPath();
    if (recordingPath_.empty()) {
        FailRecording("Could not create a recording path in the output folder.");
        return false;
    }
    recordingGif_ = gif;
    recordingMaximumHeight_ = gif ? std::clamp(gifHeight, 360, 1080) : 0;
    gifColors_ = std::clamp(gifColors, 32, 256);
    gifDurationLimit_ = 30.0;
    gifOutputPath_.clear();
    gifStatus_.clear();
    if (recordingGif_) {
        std::filesystem::path source(ToWide(recordingPath_));
        const auto baseStem = source.stem().wstring();
        gifOutputPath_ = ToUtf8((source.parent_path() / (baseStem + L".gif")).c_str());
        source = source.parent_path() / (baseStem + L".gif-source.mkv");
        recordingPath_ = ToUtf8(source.c_str());
        if (std::filesystem::exists(source) ||
            std::filesystem::exists(std::filesystem::path(ToWide(gifOutputPath_)))) {
            FailRecording("A GIF source or output with the same name already exists.");
            return false;
        }
    }
    recordingWorkingPath_ = MakeWorkingRecordingPath(recordingPath_);
    constexpr std::uint64_t minimumFreeSpace = 512ULL * 1024 * 1024;
    if (recordingWorkingPath_.empty() || !HasRecordingSpace(recordingWorkingPath_, minimumFreeSpace)) {
        FailRecording(frameProcessingError_.empty() ? "Could not prepare the temporary recording path."
                                                    : frameProcessingError_);
        return false;
    }
    if (std::filesystem::exists(std::filesystem::path(ToWide(recordingWorkingPath_)))) {
        FailRecording("A recording with the same temporary name already exists.");
        return false;
    }
    recordingFramesPerSecond_ = std::clamp(framesPerSecond, 1, 120);
    recordingFrameCount_ = 0;
    recordingElapsedSeconds_ = 0.0;
    recordingStartQpc_ = 0;
    recordingPausedQpc_ = 0;
    recordingPauseStartQpc_ = 0;
    recordingLastPts_ = -1;
    recordingRemuxToMp4_ = remuxToMp4;
    remuxStatus_.clear();
    requestedEncoderName_ = std::move(requestedEncoder);
    activeEncoderName_.clear();
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    recordingQpcFrequency_ = frequency.QuadPart;
    processedFrame_.reset();
    muxer_.Close();
    videoEncoder_.Close();
    audioEncoder_.Close();
    systemAudioCapture_.Stop();
    microphoneCapture_.Stop();
    systemAudio = systemAudio && !recordingGif_;
    microphone = microphone && !recordingGif_;
    systemAudioEnabled_ = systemAudio && systemAudioCapture_.Start(AudioEndpointKind::SystemLoopback);
    microphoneEnabled_ = microphone && microphoneCapture_.Start(AudioEndpointKind::Microphone);
    audioStatus_.clear();
    if (systemAudio && !systemAudioEnabled_) {
        audioStatus_ = "System audio unavailable: " + systemAudioCapture_.LastError();
    }
    if (microphone && !microphoneEnabled_) {
        if (!audioStatus_.empty()) audioStatus_ += " | ";
        audioStatus_ += "Microphone unavailable: " + microphoneCapture_.LastError();
    }
    if (systemAudioEnabled_ || microphoneEnabled_) {
        if (!audioStatus_.empty()) audioStatus_ += " | ";
        audioStatus_ += systemAudioEnabled_ && microphoneEnabled_
                            ? "System audio + microphone ready"
                            : (systemAudioEnabled_ ? "System audio ready" : "Microphone ready");
    } else if (!systemAudio && !microphone) {
        audioStatus_ = recordingGif_ ? "GIF does not include audio" : "Audio disabled";
    }
    capture_.Stop();
    if (!capture_.Start(targetPicker_.Selected(), device_.Get())) {
        FailRecording(capture_.LastError().empty() ? "Could not start Windows Graphics Capture." : capture_.LastError());
        return false;
    }
    frameProcessingError_.clear();
    return true;
}

bool Win32D3D11App::PauseRecording() {
    if (recordingState_.Phase() != SessionPhase::Recording) return false;
    PumpRecordingClock();
    PumpRecordingAudio(true);
    if (recordingState_.Phase() == SessionPhase::Failed || !recordingState_.Pause()) return false;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    recordingPauseStartQpc_ = now.QuadPart;
    systemAudioCapture_.Stop();
    microphoneCapture_.Stop();
    audioStatus_ = "Recording paused; audio capture is suspended.";
    return true;
}

bool Win32D3D11App::ResumeRecording() {
    if (recordingState_.Phase() != SessionPhase::Paused || recordingPauseStartQpc_ <= 0) return false;
    const bool resumeSystemAudio = systemAudioEnabled_;
    const bool resumeMicrophone = microphoneEnabled_;
    if (resumeSystemAudio) {
        systemAudioEnabled_ = systemAudioCapture_.Start(AudioEndpointKind::SystemLoopback);
    }
    if (resumeMicrophone) {
        microphoneEnabled_ = microphoneCapture_.Start(AudioEndpointKind::Microphone);
    }
    while (capture_.TryPopFrame()) {
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    recordingPausedQpc_ += std::max<std::int64_t>(0, now.QuadPart - recordingPauseStartQpc_);
    recordingPauseStartQpc_ = 0;
    if (!recordingState_.Resume()) return false;
    if (systemAudioEnabled_ || microphoneEnabled_) {
        audioStatus_ = systemAudioEnabled_ && microphoneEnabled_
            ? "System audio + microphone resumed"
            : (systemAudioEnabled_ ? "System audio resumed" : "Microphone resumed");
    } else if (resumeSystemAudio || resumeMicrophone) {
        audioStatus_ = "Recording resumed without audio; the selected WASAPI source could not restart.";
    } else {
        audioStatus_ = "Audio disabled";
    }
    return true;
}

void Win32D3D11App::FailRecording(std::string error) {
    capture_.Stop();
    systemAudioCapture_.Stop();
    microphoneCapture_.Stop();
    videoEncoder_.Close();
    audioEncoder_.Close();
    muxer_.Close();
    frameProcessingError_ = error;
    recordingState_.Fail(std::move(error));
}

void Win32D3D11App::StopRecording() {
    if (!RecordingActive()) return;
    if (recordingState_.Phase() == SessionPhase::Paused && recordingPauseStartQpc_ > 0) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        recordingPausedQpc_ += std::max<std::int64_t>(0, now.QuadPart - recordingPauseStartQpc_);
        recordingPauseStartQpc_ = 0;
    }
    recordingState_.BeginStop();
    capture_.Stop();
    systemAudioCapture_.Stop();
    microphoneCapture_.Stop();
    PumpRecordingAudio(true);
    if (videoEncoder_.IsOpen() && !videoEncoder_.Flush()) {
        FailRecording(videoEncoder_.LastError());
        return;
    }
    if (audioEncoder_.IsOpen() && !audioEncoder_.Flush()) {
        FailRecording(audioEncoder_.LastError());
        return;
    }
    if (muxer_.IsOpen() && !muxer_.Finalize()) {
        FailRecording(muxer_.LastError());
        return;
    }
    encodedPacketCount_ = videoEncoder_.PacketCount();
    muxer_.Close();
    videoEncoder_.Close();
    audioEncoder_.Close();
    if (recordingFrameCount_ == 0 &&
        !std::filesystem::exists(std::filesystem::path(ToWide(recordingWorkingPath_)))) {
        recordingState_.MarkStopped();
        return;
    }
    if (!CommitRecordingFile()) {
        recordingState_.Fail(frameProcessingError_);
        ScanRecoverableRecordings();
        return;
    }
    ScanRecoverableRecordings();
    recordingState_.MarkStopped();
    if (recordingGif_) ConvertRecordingToGif();
    else if (recordingRemuxToMp4_) RemuxRecordingToMp4(recordingPath_);
}

bool Win32D3D11App::ProcessRecordingFrame(CapturedFrame frame) {
    FrameProcessOptions options{};
    options.pixelFormat = FramePixelFormat::Nv12;
    options.maximumOutputHeight = recordingMaximumHeight_;
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
        if ((systemAudioEnabled_ || microphoneEnabled_) && !audioEncoder_.Open()) {
            FailRecording(audioEncoder_.LastError());
            return false;
        }
        if (!muxer_.Open(recordingWorkingPath_, videoEncoder_.CodecContext(), audioEncoder_.CodecContext())) {
            FailRecording(muxer_.LastError());
            return false;
        }
        videoEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteVideoPacket(packet); });
        audioEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteAudioPacket(packet); });
        recordingStartQpc_ = frame.qpcTimestamp;
        audioMixer_.Reset(QpcTo100ns(recordingStartQpc_, recordingQpcFrequency_));
        if (recordingGif_) {
            gifDurationLimit_ = GifDurationLimit(
                processed->contentSize, recordingFramesPerSecond_);
            std::ostringstream status;
            status << "GIF recording: " << processed->contentSize.cx << 'x' << processed->contentSize.cy
                   << " at " << recordingFramesPerSecond_ << " fps; auto-stop "
                   << static_cast<int>(gifDurationLimit_) << " s.";
            gifStatus_ = status.str();
        }
        recordingState_.MarkRecording();
    }
    const auto delta = EffectiveRecordingDelta(frame.qpcTimestamp);
    std::int64_t pts = QpcDeltaToFramePts(delta, recordingQpcFrequency_, recordingFramesPerSecond_);
    if (processedFrame_) {
        while (recordingLastPts_ + 1 < pts) {
            if (!SendRecordingFrame(*processedFrame_, recordingLastPts_ + 1)) return false;
        }
    }
    if (pts <= recordingLastPts_) {
        recordingElapsedSeconds_ = static_cast<double>(delta) / static_cast<double>(recordingQpcFrequency_);
        return true;
    }
    if (!SendRecordingFrame(*processed, pts)) return false;
    recordingElapsedSeconds_ = static_cast<double>(delta) / static_cast<double>(recordingQpcFrequency_);
    processedFrame_ = std::move(processed);
    return true;
}

std::int64_t Win32D3D11App::EffectiveRecordingDelta(std::int64_t qpc) const noexcept {
    std::int64_t paused = recordingPausedQpc_;
    if (recordingPauseStartQpc_ > 0 && qpc > recordingPauseStartQpc_) {
        paused += qpc - recordingPauseStartQpc_;
    }
    return ActiveQpcDelta(qpc - recordingStartQpc_, paused);
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
    const auto delta = EffectiveRecordingDelta(now.QuadPart);
    const auto desiredPts = QpcDeltaToFramePts(delta, recordingQpcFrequency_, recordingFramesPerSecond_);
    int emitted{};
    while (recordingLastPts_ < desiredPts && emitted < 4) {
        if (!SendRecordingFrame(*processedFrame_, recordingLastPts_ + 1)) return;
        ++emitted;
    }
    recordingElapsedSeconds_ = static_cast<double>(delta) / static_cast<double>(recordingQpcFrequency_);
}

void Win32D3D11App::PumpRecordingAudio(bool finalDrain) {
    if (!audioEncoder_.IsOpen() || recordingStartQpc_ <= 0) return;
    const auto paused100ns = QpcTo100ns(recordingPausedQpc_, recordingQpcFrequency_);
    if (systemAudioEnabled_) {
        while (auto packet = systemAudioCapture_.TryPopPacket()) {
            packet->qpcPosition100ns = packet->qpcPosition100ns > paused100ns
                ? packet->qpcPosition100ns - paused100ns : 0;
            if (!audioMixer_.Push(AudioSource::System, *packet, systemAudioCapture_.Format())) {
                FailRecording(audioMixer_.LastError());
                return;
            }
        }
    }
    if (microphoneEnabled_) {
        while (auto packet = microphoneCapture_.TryPopPacket()) {
            packet->qpcPosition100ns = packet->qpcPosition100ns > paused100ns
                ? packet->qpcPosition100ns - paused100ns : 0;
            if (!audioMixer_.Push(AudioSource::Microphone, *packet, microphoneCapture_.Format())) {
                FailRecording(audioMixer_.LastError());
                return;
            }
        }
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto activeDelta100ns = QpcTo100ns(EffectiveRecordingDelta(now.QuadPart), recordingQpcFrequency_);
    auto availableThrough = static_cast<std::int64_t>(
        activeDelta100ns * AudioTimelineMixer::SampleRate / 10'000'000ULL);
    if (!finalDrain) availableThrough = std::max<std::int64_t>(0, availableThrough - 2'400);
    MixedAudioChunk chunk;
    const auto frameSize = static_cast<std::uint32_t>(audioEncoder_.FrameSize());
    while (audioMixer_.Pop(availableThrough, frameSize, chunk)) {
        if (!audioEncoder_.Send(chunk.stereoSamples, chunk.presentationTimestamp)) {
            FailRecording(audioEncoder_.LastError());
            return;
        }
    }
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
    if (avMuxSmokeMode_ && !audioEncoder_.Open()) {
        frameProcessingError_ = audioEncoder_.LastError();
        videoEncoder_.Close();
        return false;
    }
    if (!muxer_.Open(outputPath, videoEncoder_.CodecContext(), audioEncoder_.CodecContext())) {
        frameProcessingError_ = muxer_.LastError();
        videoEncoder_.Close();
        audioEncoder_.Close();
        return false;
    }
    videoEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteVideoPacket(packet); });
    audioEncoder_.SetPacketCallback([this](AVPacket* packet) { return muxer_.WriteAudioPacket(packet); });
    for (std::int64_t pts = 0; pts < 60; ++pts) {
        if (!videoEncoder_.Send(frame, pts)) {
            frameProcessingError_ = videoEncoder_.LastError();
            muxer_.Close();
            videoEncoder_.Close();
            return false;
        }
    }
    if (avMuxSmokeMode_) {
        const int audioFrameSize = audioEncoder_.FrameSize();
        std::vector<float> silence(static_cast<std::size_t>(audioFrameSize) * 2);
        for (std::int64_t audioFrame = 0; audioFrame < 47; ++audioFrame) {
            if (!audioEncoder_.Send(silence, audioFrame * audioFrameSize)) {
                frameProcessingError_ = audioEncoder_.LastError();
                muxer_.Close();
                videoEncoder_.Close();
                audioEncoder_.Close();
                return false;
            }
        }
    }
    if (!videoEncoder_.Flush() || (audioEncoder_.IsOpen() && !audioEncoder_.Flush()) || !muxer_.Finalize()) {
        frameProcessingError_ = videoEncoder_.LastError().empty() ? muxer_.LastError() : videoEncoder_.LastError();
        if (frameProcessingError_.empty()) frameProcessingError_ = audioEncoder_.LastError();
        muxer_.Close();
        videoEncoder_.Close();
        audioEncoder_.Close();
        return false;
    }
    muxer_.Close();
    videoEncoder_.Close();
    audioEncoder_.Close();
    return true;
}

bool Win32D3D11App::RunScreenshotSmoke() {
    constexpr UINT width = 64;
    constexpr UINT height = 32;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (UINT row = 0; row < height; ++row) {
        for (UINT column = 0; column < width; ++column) {
            const auto offset = (static_cast<std::size_t>(row) * width + column) * 4;
            pixels[offset] = static_cast<std::uint8_t>(column * 4);
            pixels[offset + 1] = static_cast<std::uint8_t>(row * 8);
            pixels[offset + 2] = 0xC0;
            pixels[offset + 3] = 0xFF;
        }
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels.data();
    initialData.SysMemPitch = width * 4;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&description, &initialData, &texture))) {
        frameProcessingError_ = "Could not create the synthetic screenshot texture.";
        return false;
    }
    const auto outputPath = ReadEnvironmentWide(L"OPENCAPTURE_SCREENSHOT_OUTPUT");
    if (outputPath.empty() ||
        !screenshotService_.Capture(device_.Get(), context_.Get(), texture.Get(),
                                    SIZE{static_cast<LONG>(width), static_cast<LONG>(height)},
                                    ScreenshotDestination::FileAndClipboard, outputPath, window_)) {
        frameProcessingError_ = outputPath.empty()
                                    ? "OPENCAPTURE_SCREENSHOT_OUTPUT is required."
                                    : screenshotService_.LastError();
        return false;
    }
    if (!screenshotService_.Capture(device_.Get(), context_.Get(), texture.Get(),
                                    SIZE{static_cast<LONG>(width), static_cast<LONG>(height)},
                                    ScreenshotDestination::Clipboard, {}, window_)) {
        frameProcessingError_ = screenshotService_.LastError();
        return false;
    }
    Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> decodedFrame;
    UINT decodedWidth{};
    UINT decodedHeight{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&imagingFactory))) ||
        FAILED(imagingFactory->CreateDecoderFromFilename(outputPath.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &decodedFrame)) ||
        FAILED(decodedFrame->GetSize(&decodedWidth, &decodedHeight)) ||
        decodedWidth != width || decodedHeight != height) {
        frameProcessingError_ = "WIC could not reopen the screenshot PNG at the expected dimensions.";
        return false;
    }
    bool clipboardOpen{};
    for (int attempt = 0; attempt < 10 && !clipboardOpen; ++attempt) {
        clipboardOpen = OpenClipboard(window_) != FALSE;
        if (!clipboardOpen) Sleep(10);
    }
    if (!clipboardOpen) {
        frameProcessingError_ = "Could not reopen the clipboard for screenshot validation.";
        return false;
    }
    const HANDLE clipboardData = GetClipboardData(CF_DIBV5);
    const auto* header = clipboardData
        ? static_cast<const BITMAPV5HEADER*>(GlobalLock(clipboardData))
        : nullptr;
    const bool valid = header && header->bV5Size == sizeof(BITMAPV5HEADER) &&
                       header->bV5Width == static_cast<LONG>(width) &&
                       header->bV5Height == -static_cast<LONG>(height) &&
                       header->bV5BitCount == 32;
    if (header) GlobalUnlock(clipboardData);
    CloseClipboard();
    if (!valid) {
        frameProcessingError_ = "CF_DIBV5 clipboard validation failed.";
        return false;
    }
    return true;
}

void Win32D3D11App::ProcessCaptureFrames() {
    if (recordingState_.Phase() == SessionPhase::Paused) {
        std::optional<CapturedFrame> latest;
        while (auto frame = capture_.TryPopFrame()) latest = std::move(frame);
        if (latest && pendingScreenshot_) ProcessScreenshotFrame(*latest);
        return;
    }
    if (RecordingActive()) {
        while (auto frame = capture_.TryPopFrame()) {
            if (pendingScreenshot_) ProcessScreenshotFrame(*frame);
            if (!ProcessRecordingFrame(std::move(*frame))) break;
        }
        return;
    }
    std::optional<CapturedFrame> latest;
    while (auto frame = capture_.TryPopFrame()) latest = std::move(frame);
    if (!latest) return;
    if (pendingScreenshot_) {
        ProcessScreenshotFrame(*latest);
        if (!captureSmokeMode_ && !capture_.IsRunning()) return;
    }
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
    PollMediaJob();
    ProcessCaptureFrames();
    PumpRecordingClock();
    PumpRecordingAudio();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const auto recordingPhase = recordingState_.Phase();
    const auto recordingError = recordingState_.Error();
    std::error_code recordingPathError;
    const bool canRemux = recordingPhase == SessionPhase::Idle &&
        std::filesystem::path(ToWide(recordingPath_)).extension() == L".mkv" &&
        std::filesystem::is_regular_file(std::filesystem::path(ToWide(recordingPath_)), recordingPathError) &&
        !recordingPathError;
    const RecordingUiState recordingUi{
        RecordingActive(), recordingPhase == SessionPhase::Starting,
        recordingPhase == SessionPhase::Paused, RecordingActive() && recordingGif_,
        canRemux, mediaJobRunning_.load(),
        mediaWorker_.joinable() && mediaWorker_.get_stop_token().stop_requested(),
        mediaProgress_.load(), recordingPath_, recordingError,
        activeEncoderName_, recordingFrameCount_, recordingElapsedSeconds_};
    std::vector<EncoderUiChoice> encoderChoices;
    for (const auto& capability : encoderRegistry_.Capabilities()) {
        if (capability.codec == VideoCodecFamily::H264) {
            encoderChoices.push_back({capability.name, capability.displayName, capability.usable, capability.detail});
        }
    }
    HotkeyUiState hotkeyUi{};
    const auto& hotkeyBindings = globalHotkeys_.Bindings();
    for (std::size_t index = 0; index < hotkeyBindings.size(); ++index) {
        hotkeyUi.modifiers[index] = hotkeyBindings[index].modifiers;
        hotkeyUi.virtualKeys[index] = hotkeyBindings[index].virtualKey;
        hotkeyUi.labels[index] = globalHotkeys_.Label(static_cast<HotkeyAction>(index));
    }
    hotkeyUi.error = globalHotkeys_.LastError();
    std::string overlayStatus = targetOverlayStatus_;
    const auto borderlessStatus = capture_.BorderlessStatus();
    if (!borderlessStatus.empty()) {
        if (!overlayStatus.empty()) overlayStatus += " | ";
        overlayStatus += borderlessStatus;
    }
    const auto command = MainPanel::Draw(gpuName_, ffmpegVersion_, encoderSummary_, encoderChoices, frameProcessingError_,
                                         screenshotStatus_, overlayStatus, audioStatus_, recoveryStatus_, remuxStatus_, gifStatus_,
                                         outputDirectoryUtf8_,
                                         recoverableRecordings_,
                                         recordingUi, hotkeyUi, targetPicker_, capture_, window_, device_.Get());
    if (command.changeHotkeyAction >= 0 &&
        command.changeHotkeyAction < static_cast<int>(HotkeyAction::Count)) {
        globalHotkeys_.SetBinding(
            static_cast<HotkeyAction>(command.changeHotkeyAction),
            {command.hotkeyModifiers, command.hotkeyVirtualKey});
    }
    if (command.resetHotkeys) globalHotkeys_.ResetDefaults();
    if (command.chooseOutputDirectory && !RecordingActive() && !mediaJobRunning_) ChooseOutputDirectory();
    if (command.recoverRecordingIndex >= 0) {
        RecoverRecording(static_cast<std::size_t>(command.recoverRecordingIndex));
    }
    if (command.startRecording && !mediaJobRunning_) {
        StartRecording(command.framesPerSecond, command.quality, command.encoderName,
                       command.systemAudio, command.microphone, command.remuxToMp4);
    }
    if (command.startGif && !mediaJobRunning_) {
        StartRecording(command.gifFramesPerSecond, 0, command.encoderName,
                       false, false, false, true, command.gifHeight, command.gifColors);
    }
    if (command.stopRecording) StopRecording();
    if (command.pauseRecording) PauseRecording();
    if (command.resumeRecording) ResumeRecording();
    if (command.remuxLastRecording) RemuxRecordingToMp4(recordingPath_);
    if (command.cancelMediaJob) CancelMediaJob();
    if (command.copyScreenshot) StartScreenshot(ScreenshotDestination::Clipboard);
    if (command.saveScreenshot) StartScreenshot(ScreenshotDestination::File);
    if (command.saveAndCopyScreenshot) StartScreenshot(ScreenshotDestination::FileAndClipboard);

    const std::uint32_t hotkeyActions = pendingHotkeyActions_;
    pendingHotkeyActions_ = 0;
    if ((hotkeyActions & (1U << static_cast<unsigned>(HotkeyAction::ScreenshotClipboard))) != 0) {
        StartScreenshot(ScreenshotDestination::Clipboard);
    }
    if ((hotkeyActions & (1U << static_cast<unsigned>(HotkeyAction::ToggleVideoRecording))) != 0) {
        if (RecordingActive()) {
            StopRecording();
        } else if (!mediaJobRunning_) {
            StartRecording(command.framesPerSecond, command.quality, command.encoderName,
                           command.systemAudio, command.microphone, command.remuxToMp4);
        }
    }
    if ((hotkeyActions & (1U << static_cast<unsigned>(HotkeyAction::ToggleGifRecording))) != 0) {
        if (RecordingActive()) {
            StopRecording();
        } else if (!mediaJobRunning_) {
            StartRecording(command.gifFramesPerSecond, 0, command.encoderName,
                           false, false, false, true, command.gifHeight, command.gifColors);
        }
    }
    if (recordingGif_ && recordingState_.Phase() == SessionPhase::Recording &&
        recordingElapsedSeconds_ >= gifDurationLimit_) {
        gifStatus_ = "GIF safety limit reached; creating the GIF...";
        StopRecording();
    }

    CaptureOverlayState overlayState = CaptureOverlayState::Idle;
    const auto currentPhase = recordingState_.Phase();
    if (currentPhase == SessionPhase::Paused) {
        overlayState = CaptureOverlayState::Paused;
    } else if (currentPhase == SessionPhase::Starting ||
               currentPhase == SessionPhase::Recording ||
               currentPhase == SessionPhase::Stopping ||
               pendingScreenshot_) {
        overlayState = CaptureOverlayState::Capturing;
    } else if (currentPhase == SessionPhase::Failed) {
        overlayState = CaptureOverlayState::Error;
    }
    targetOverlay_.Update(targetPicker_.Selected(), overlayState);

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
        } else if (pauseRecordSmokeMode_ && !pauseSmokeStarted_ &&
                   recordingElapsedSeconds_ >= 0.5 && recordingFrameCount_ >= 25) {
            if (!PauseRecording()) {
                captureSmokeFailed_ = true;
                WriteSmokeFailure("Pause recording smoke could not pause.");
                PostMessageW(window_, WM_CLOSE, 0, 0);
            } else {
                LARGE_INTEGER now{};
                QueryPerformanceCounter(&now);
                pauseSmokeWallStartQpc_ = now.QuadPart;
                pauseSmokeStarted_ = true;
            }
        } else if (pauseRecordSmokeMode_ && pauseSmokeStarted_ && !pauseSmokeResumed_ &&
                   recordingState_.Phase() == SessionPhase::Paused) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            if (now.QuadPart - pauseSmokeWallStartQpc_ >= recordingQpcFrequency_ / 2) {
                if (!ResumeRecording()) {
                    captureSmokeFailed_ = true;
                    WriteSmokeFailure("Pause recording smoke could not resume.");
                    PostMessageW(window_, WM_CLOSE, 0, 0);
                } else {
                    pauseSmokeResumed_ = true;
                }
            }
        } else if ((!pauseRecordSmokeMode_ || pauseSmokeResumed_) &&
                   recordingElapsedSeconds_ >= 1.0 &&
                   recordingFrameCount_ >= (gifRecordSmokeMode_ ? 10ULL : 55ULL)) {
            if (recordFailureSmokeMode_) {
                FailRecording("Forced recording failure smoke.");
                captureSmokeFailed_ = true;
                WriteSmokeFailure(recordingState_.Error());
            } else {
                StopRecording();
                realtimeRecordSmokeComplete_ = !gifRecordSmokeMode_;
            }
            if (!gifRecordSmokeMode_) PostMessageW(window_, WM_CLOSE, 0, 0);
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
    CancelMediaJob();
    if (mediaWorker_.joinable()) mediaWorker_.join();
    capture_.Stop();
    muxer_.Close();
    videoEncoder_.Close();
    audioEncoder_.Close();
    systemAudioCapture_.Stop();
    microphoneCapture_.Stop();
    processedFrame_.reset();
    frameProcessor_.Reset();
    globalHotkeys_.Shutdown();
    targetOverlay_.Shutdown();
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

void Win32D3D11App::HandleHotkey(HotkeyAction action) {
    pendingHotkeyActions_ |= 1U << static_cast<unsigned>(action);
}

LRESULT CALLBACK Win32D3D11App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return 1;
    switch (message) {
    case WM_HOTKEY: {
        HotkeyAction action{};
        if (GlobalHotkeys::ActionForId(static_cast<int>(wParam), action)) {
            auto* app = reinterpret_cast<Win32D3D11App*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (app) app->HandleHotkey(action);
            return 0;
        }
        break;
    }
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
