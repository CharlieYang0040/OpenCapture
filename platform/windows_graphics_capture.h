#pragma once

#include "core/capture_target.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace opencapture {

struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    SIZE contentSize{};
    std::int64_t qpcTimestamp{};
};

class WindowsGraphicsCapture final {
public:
    WindowsGraphicsCapture();
    ~WindowsGraphicsCapture();

    WindowsGraphicsCapture(const WindowsGraphicsCapture&) = delete;
    WindowsGraphicsCapture& operator=(const WindowsGraphicsCapture&) = delete;

    bool Start(const CaptureTarget& target, ID3D11Device* device);
    void RequestBorderlessAccess();
    void Stop() noexcept;
    [[nodiscard]] std::optional<CapturedFrame> TryPopFrame();

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] std::uint64_t FrameCount() const noexcept;
    [[nodiscard]] std::uint64_t DroppedFrameCount() const noexcept;
    [[nodiscard]] std::size_t QueuedFrameCount() const noexcept;
    [[nodiscard]] std::int64_t LastFrameQpc() const noexcept;
    [[nodiscard]] CaptureTarget ActiveTarget() const noexcept;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::string BorderlessStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opencapture
