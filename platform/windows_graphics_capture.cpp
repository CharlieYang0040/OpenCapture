#include "platform/windows_graphics_capture.h"

#include "core/bounded_queue.h"

#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <mutex>
#include <utility>

namespace opencapture {
namespace {

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccess;
using winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

IDirect3DDevice CreateDirect3DDevice(ID3D11Device* device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

GraphicsCaptureItem CreateCaptureItem(const CaptureTarget& target) {
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    if (target.type == CaptureTargetType::Window) {
        winrt::check_hresult(interop->CreateForWindow(
            target.window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    } else {
        winrt::check_hresult(interop->CreateForMonitor(
            target.monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    }
    return item;
}

} // namespace

struct WindowsGraphicsCapture::Impl {
    ~Impl() {
        try {
            if (borderlessOperation) {
                borderlessOperation.Completed(nullptr);
                borderlessOperation.Cancel();
            }
        } catch (...) {
        }
    }

    void RequestBorderlessAccess() {
        if (borderlessRequestStarted.exchange(true, std::memory_order_acq_rel)) return;
        try {
            using winrt::Windows::Foundation::Metadata::ApiInformation;
            if (!ApiInformation::IsTypePresent(
                    L"Windows.Graphics.Capture.GraphicsCaptureAccess") ||
                !ApiInformation::IsPropertyPresent(
                    L"Windows.Graphics.Capture.GraphicsCaptureSession",
                    L"IsBorderRequired")) {
                SetBorderlessStatus("This Windows version does not support replacing the system capture border.");
                return;
            }
            borderlessOperation = GraphicsCaptureAccess::RequestAccessAsync(
                GraphicsCaptureAccessKind::Borderless);
            borderlessOperation.Completed(
                [this](const auto& operation, winrt::Windows::Foundation::AsyncStatus status) {
                    if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                        SetBorderlessStatus("Borderless capture permission was not granted; the Windows border remains visible.");
                        return;
                    }
                    try {
                        const auto result = operation.GetResults();
                        const bool allowed = result == AppCapabilityAccessStatus::Allowed;
                        borderlessAllowed.store(allowed, std::memory_order_release);
                        SetBorderlessStatus(allowed
                            ? "OpenCapture target border active; the Windows capture border is disabled."
                            : "Borderless capture permission was denied; the Windows border remains visible.");
                    } catch (...) {
                        SetBorderlessStatus("Borderless capture permission could not be read; the Windows border remains visible.");
                    }
                });
        } catch (const winrt::hresult_error& error) {
            SetBorderlessStatus(
                "Borderless capture permission is unavailable for this build: " +
                winrt::to_string(error.message()));
        }
    }

    bool Start(const CaptureTarget& target, ID3D11Device* device) {
        Stop();
        SetError({});
        if (!target.IsValid() || !device) {
            SetError("A valid capture target and D3D11 device are required.");
            return false;
        }
        if (target.type == CaptureTargetType::Region && !target.monitor) {
            SetError("The selected region is not associated with a monitor.");
            return false;
        }

        try {
            if (!GraphicsCaptureSession::IsSupported()) {
                SetError("Windows Graphics Capture is not supported on this system.");
                return false;
            }
            direct3DDevice = CreateDirect3DDevice(device);
            item = CreateCaptureItem(target);
            const auto initialSize = item.Size();
            if (initialSize.Width <= 0 || initialSize.Height <= 0) {
                SetError("The capture target has no drawable area.");
                Stop();
                return false;
            }
            contentSize = SIZE{initialSize.Width, initialSize.Height};
            activeTarget = target;
            frames.Clear();
            framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                direct3DDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, initialSize);
            frameToken = framePool.FrameArrived({this, &Impl::OnFrameArrived});
            closedToken = item.Closed({this, &Impl::OnTargetClosed});
            session = framePool.CreateCaptureSession(item);
            if (borderlessAllowed.load(std::memory_order_acquire)) {
                try {
                    session.IsBorderRequired(false);
                } catch (const winrt::hresult_error& error) {
                    SetBorderlessStatus(
                        "Windows kept its capture border: " +
                        winrt::to_string(error.message()));
                }
            }
            frameCount.store(0, std::memory_order_relaxed);
            droppedFrameCount.store(0, std::memory_order_relaxed);
            lastFrameQpc.store(0, std::memory_order_relaxed);
            running.store(true, std::memory_order_release);
            session.StartCapture();
            return true;
        } catch (const winrt::hresult_error& error) {
            SetError(winrt::to_string(error.message()));
            Stop();
            return false;
        }
    }

    void Stop() noexcept {
        running.store(false, std::memory_order_release);
        try {
            if (framePool) framePool.FrameArrived(frameToken);
            if (item) item.Closed(closedToken);
            if (session) session.Close();
            if (framePool) framePool.Close();
        } catch (...) {
        }
        frames.Clear();
        session = nullptr;
        framePool = nullptr;
        item = nullptr;
        direct3DDevice = nullptr;
        contentSize = {};
    }

    void OnFrameArrived(const Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&) noexcept {
        if (!running.load(std::memory_order_acquire)) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) return;
            const auto size = frame.ContentSize();
            auto access = frame.Surface().as<
                ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            CapturedFrame captured{};
            winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&captured.texture)));
            captured.contentSize = SIZE{size.Width, size.Height};
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            captured.qpcTimestamp = qpc.QuadPart;
            lastFrameQpc.store(captured.qpcTimestamp, std::memory_order_relaxed);
            frameCount.fetch_add(1, std::memory_order_relaxed);
            if (frames.PushDropOldest(std::move(captured))) {
                droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
            }

            if (size.Width > 0 && size.Height > 0 &&
                (size.Width != contentSize.cx || size.Height != contentSize.cy)) {
                contentSize = SIZE{size.Width, size.Height};
                sender.Recreate(direct3DDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            }
        } catch (const winrt::hresult_error& error) {
            SetError(winrt::to_string(error.message()));
            running.store(false, std::memory_order_release);
        } catch (...) {
            SetError("Windows Graphics Capture stopped after an unexpected frame error.");
            running.store(false, std::memory_order_release);
        }
    }

    void OnTargetClosed(const GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&) noexcept {
        SetError("The selected capture target was closed.");
        running.store(false, std::memory_order_release);
    }

    void SetError(std::string message) {
        std::scoped_lock lock(errorMutex);
        lastError = std::move(message);
    }

    [[nodiscard]] std::string Error() const {
        std::scoped_lock lock(errorMutex);
        return lastError;
    }

    void SetBorderlessStatus(std::string message) {
        std::scoped_lock lock(borderlessStatusMutex);
        borderlessStatus = std::move(message);
    }

    [[nodiscard]] std::string GetBorderlessStatus() const {
        std::scoped_lock lock(borderlessStatusMutex);
        return borderlessStatus;
    }

    IDirect3DDevice direct3DDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frameToken{};
    winrt::event_token closedToken{};
    BoundedQueue<CapturedFrame> frames{3};
    CaptureTarget activeTarget{};
    SIZE contentSize{};
    std::atomic_bool running{};
    std::atomic_uint64_t frameCount{};
    std::atomic_uint64_t droppedFrameCount{};
    std::atomic_int64_t lastFrameQpc{};
    std::atomic_bool borderlessRequestStarted{};
    std::atomic_bool borderlessAllowed{};
    winrt::Windows::Foundation::IAsyncOperation<AppCapabilityAccessStatus>
        borderlessOperation{nullptr};
    mutable std::mutex errorMutex;
    std::string lastError;
    mutable std::mutex borderlessStatusMutex;
    std::string borderlessStatus{"Requesting permission to replace the Windows capture border..."};
};

WindowsGraphicsCapture::WindowsGraphicsCapture() : impl_(std::make_unique<Impl>()) {}
WindowsGraphicsCapture::~WindowsGraphicsCapture() { Stop(); }

bool WindowsGraphicsCapture::Start(const CaptureTarget& target, ID3D11Device* device) { return impl_->Start(target, device); }
void WindowsGraphicsCapture::RequestBorderlessAccess() { impl_->RequestBorderlessAccess(); }

void WindowsGraphicsCapture::Stop() noexcept { impl_->Stop(); }
std::optional<CapturedFrame> WindowsGraphicsCapture::TryPopFrame() { return impl_->frames.TryPop(); }
bool WindowsGraphicsCapture::IsRunning() const noexcept { return impl_->running.load(std::memory_order_acquire); }
std::uint64_t WindowsGraphicsCapture::FrameCount() const noexcept { return impl_->frameCount.load(std::memory_order_relaxed); }
std::uint64_t WindowsGraphicsCapture::DroppedFrameCount() const noexcept { return impl_->droppedFrameCount.load(std::memory_order_relaxed); }
std::size_t WindowsGraphicsCapture::QueuedFrameCount() const noexcept { return impl_->frames.Size(); }
std::int64_t WindowsGraphicsCapture::LastFrameQpc() const noexcept { return impl_->lastFrameQpc.load(std::memory_order_relaxed); }
CaptureTarget WindowsGraphicsCapture::ActiveTarget() const noexcept { return impl_->activeTarget; }
std::string WindowsGraphicsCapture::LastError() const { return impl_->Error(); }
std::string WindowsGraphicsCapture::BorderlessStatus() const { return impl_->GetBorderlessStatus(); }

} // namespace opencapture
