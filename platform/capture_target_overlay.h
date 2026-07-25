#pragma once

#include "core/capture_target.h"

#include <Windows.h>

#include <cstdint>
#include <string>

namespace opencapture {

enum class CaptureOverlayState {
    Idle,
    Capturing,
    Paused,
    Error,
};

class CaptureTargetOverlay final {
public:
    CaptureTargetOverlay() = default;
    ~CaptureTargetOverlay();

    CaptureTargetOverlay(const CaptureTargetOverlay&) = delete;
    CaptureTargetOverlay& operator=(const CaptureTargetOverlay&) = delete;

    bool Initialize(HINSTANCE instance);
    void Update(const CaptureTarget& target, CaptureOverlayState state);
    void FlashScreenshot(std::uint32_t durationMilliseconds = 400);
    void Hide();
    void Shutdown();

    [[nodiscard]] bool CaptureExclusionAvailable() const noexcept {
        return captureExclusionAvailable_;
    }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool ResolveTargetRect(const CaptureTarget& target, RECT& bounds) const;
    [[nodiscard]] COLORREF CurrentColor() const noexcept;
    void Paint();

    HINSTANCE instance_{};
    HWND window_{};
    CaptureTarget target_{};
    CaptureOverlayState state_{CaptureOverlayState::Idle};
    RECT bounds_{};
    ULONGLONG lastGeometryUpdate_{};
    ULONGLONG screenshotFlashUntil_{};
    bool captureExclusionAvailable_{};
    bool visible_{};
    std::string lastError_;
};

} // namespace opencapture
