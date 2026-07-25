#pragma once

#include "core/capture_target.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>

namespace opencapture {

enum class CaptureOverlayState {
    Idle,
    Capturing,
    Paused,
    Error,
};

struct CaptureBorderSettings {
    bool visible{true};
    int thickness{3};
    int opacityPercent{85};
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
    bool ApplySettings(CaptureBorderSettings settings);
    bool ResetSettings();
    void Hide();
    void Shutdown();

    [[nodiscard]] bool CaptureExclusionAvailable() const noexcept {
        return captureExclusionAvailable_;
    }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }
    [[nodiscard]] const CaptureBorderSettings& Settings() const noexcept {
        return settings_;
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    [[nodiscard]] bool ResolveTargetRect(const CaptureTarget& target, RECT& bounds) const;
    [[nodiscard]] COLORREF CurrentColor() const noexcept;
    void UpdateDisplayAffinity(bool excludeFromCapture);
    void LoadSettings();
    bool SaveSettings() const;
    void ApplyOpacity();
    void Paint(HWND window);

    HINSTANCE instance_{};
    std::array<HWND, 4> windows_{};
    CaptureTarget target_{};
    CaptureOverlayState state_{CaptureOverlayState::Idle};
    CaptureBorderSettings settings_{};
    RECT bounds_{};
    ULONGLONG lastGeometryUpdate_{};
    ULONGLONG screenshotFlashUntil_{};
    bool captureExclusionAvailable_{};
    bool excludedFromCapture_{};
    bool visible_{};
    bool layoutDirty_{true};
    std::string lastError_;
};

} // namespace opencapture
