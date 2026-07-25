#include "platform/capture_target_overlay.h"

#include <dwmapi.h>

#include <algorithm>

namespace opencapture {
namespace {

constexpr wchar_t kOverlayWindowClass[] = L"OpenCaptureTargetOverlay";
constexpr int kBorderThickness = 3;

bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

bool IsPositiveRect(const RECT& rectangle) noexcept {
    return rectangle.right > rectangle.left && rectangle.bottom > rectangle.top;
}

bool SameTarget(const CaptureTarget& left, const CaptureTarget& right) noexcept {
    if (left.type != right.type || left.window != right.window ||
        left.monitor != right.monitor) {
        return false;
    }
    return left.type != CaptureTargetType::Region ||
           SameRect(left.region, right.region);
}

} // namespace

CaptureTargetOverlay::~CaptureTargetOverlay() {
    Shutdown();
}

bool CaptureTargetOverlay::Initialize(HINSTANCE instance) {
    if (windows_[0]) return true;
    instance_ = instance;

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kOverlayWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        lastError_ = "Could not register the capture target overlay window.";
        return false;
    }

    constexpr DWORD extendedStyle = WS_EX_TOPMOST | WS_EX_TRANSPARENT |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    captureExclusionAvailable_ = true;
    for (auto& window : windows_) {
        window = CreateWindowExW(
            extendedStyle, kOverlayWindowClass, L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, instance_, this);
        if (!window) {
            lastError_ = "Could not create the capture target border windows.";
            Shutdown();
            return false;
        }
        SetWindowDisplayAffinity(window, WDA_NONE);
    }
    if (!captureExclusionAvailable_) {
        lastError_ = "The capture target border could not be excluded from captured output.";
    }
    return true;
}

void CaptureTargetOverlay::Update(const CaptureTarget& target, CaptureOverlayState state) {
    if (!windows_[0]) return;
    const bool targetChanged = !SameTarget(target_, target);
    target_ = target;
    const bool stateChanged = state_ != state;
    state_ = state;
    const bool monitorCaptureActive =
        target.type == CaptureTargetType::Monitor &&
        (state == CaptureOverlayState::Capturing ||
         state == CaptureOverlayState::Paused);
    UpdateDisplayAffinity(monitorCaptureActive);
    const ULONGLONG now = GetTickCount64();
    const bool flashExpired = screenshotFlashUntil_ != 0 &&
                              now >= screenshotFlashUntil_;
    if (flashExpired) screenshotFlashUntil_ = 0;
    const bool repaintState = stateChanged || flashExpired ||
                              now < screenshotFlashUntil_;
    const bool dynamicTarget = target.type == CaptureTargetType::Window;
    if (!targetChanged && !dynamicTarget) {
        if (repaintState) {
            for (const auto window : windows_) InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (!targetChanged && dynamicTarget && now - lastGeometryUpdate_ < 50) {
        if (repaintState) {
            for (const auto window : windows_) InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    lastGeometryUpdate_ = now;

    RECT resolved{};
    if (!ResolveTargetRect(target_, resolved)) {
        Hide();
        return;
    }

    const bool moved = !SameRect(bounds_, resolved);
    bounds_ = resolved;
    if (moved || !visible_) {
        std::array<RECT, 4> borderRects{};
        if (target.type == CaptureTargetType::Monitor) {
            const LONG verticalHeight =
                std::max<LONG>(1, bounds_.bottom - bounds_.top -
                                  2 * kBorderThickness);
            borderRects = {{
                {bounds_.left, bounds_.top, bounds_.right,
                 bounds_.top + kBorderThickness},
                {bounds_.left, bounds_.bottom - kBorderThickness,
                 bounds_.right, bounds_.bottom},
                {bounds_.left, bounds_.top + kBorderThickness,
                 bounds_.left + kBorderThickness,
                 bounds_.top + kBorderThickness + verticalHeight},
                {bounds_.right - kBorderThickness,
                 bounds_.top + kBorderThickness, bounds_.right,
                 bounds_.top + kBorderThickness + verticalHeight},
            }};
        } else {
            borderRects = {{
                {bounds_.left, bounds_.top - kBorderThickness,
                 bounds_.right, bounds_.top},
                {bounds_.left, bounds_.bottom,
                 bounds_.right, bounds_.bottom + kBorderThickness},
                {bounds_.left - kBorderThickness, bounds_.top,
                 bounds_.left, bounds_.bottom},
                {bounds_.right, bounds_.top,
                 bounds_.right + kBorderThickness, bounds_.bottom},
            }};
        }
        for (std::size_t index = 0; index < windows_.size(); ++index) {
            const auto& rectangle = borderRects[index];
            SetWindowPos(windows_[index], HWND_TOPMOST,
                         rectangle.left, rectangle.top,
                         rectangle.right - rectangle.left,
                         rectangle.bottom - rectangle.top,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        visible_ = true;
    }
    for (const auto window : windows_) {
        RedrawWindow(window, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

void CaptureTargetOverlay::FlashScreenshot(std::uint32_t durationMilliseconds) {
    screenshotFlashUntil_ = GetTickCount64() + std::max<std::uint32_t>(durationMilliseconds, 1);
    for (const auto window : windows_) {
        if (window) {
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        }
    }
}

void CaptureTargetOverlay::Hide() {
    if (visible_) {
        for (const auto window : windows_) {
            if (window) ShowWindow(window, SW_HIDE);
        }
    }
    visible_ = false;
}

void CaptureTargetOverlay::Shutdown() {
    for (auto& window : windows_) {
        if (window) {
            DestroyWindow(window);
            window = nullptr;
        }
    }
    if (instance_) {
        UnregisterClassW(kOverlayWindowClass, instance_);
        instance_ = nullptr;
    }
    visible_ = false;
}

bool CaptureTargetOverlay::ResolveTargetRect(const CaptureTarget& target, RECT& bounds) const {
    if (!target.IsValid()) return false;
    switch (target.type) {
    case CaptureTargetType::Region:
        bounds = target.region;
        return IsPositiveRect(bounds);
    case CaptureTargetType::Monitor: {
        MONITORINFO information{};
        information.cbSize = sizeof(information);
        if (!GetMonitorInfoW(target.monitor, &information)) return false;
        bounds = information.rcMonitor;
        return IsPositiveRect(bounds);
    }
    case CaptureTargetType::Window: {
        if (!target.window || !IsWindow(target.window) || !IsWindowVisible(target.window) ||
            IsIconic(target.window)) {
            return false;
        }
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(target.window, DWMWA_CLOAKED,
                                            &cloaked, sizeof(cloaked))) &&
            cloaked) {
            return false;
        }
        if (FAILED(DwmGetWindowAttribute(target.window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                         &bounds, sizeof(bounds)))) {
            if (!GetWindowRect(target.window, &bounds)) return false;
        }
        return IsPositiveRect(bounds);
    }
    }
    return false;
}

COLORREF CaptureTargetOverlay::CurrentColor() const noexcept {
    if (GetTickCount64() < screenshotFlashUntil_) return RGB(255, 205, 0);
    switch (state_) {
    case CaptureOverlayState::Capturing:
        return RGB(255, 205, 0);
    case CaptureOverlayState::Paused:
        return RGB(255, 145, 35);
    case CaptureOverlayState::Error:
        return RGB(235, 70, 70);
    case CaptureOverlayState::Idle:
    default:
        return RGB(45, 145, 255);
    }
}

void CaptureTargetOverlay::UpdateDisplayAffinity(bool excludeFromCapture) {
    if (excludedFromCapture_ == excludeFromCapture) return;
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    const DWORD affinity = excludeFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    bool succeeded = true;
    for (const auto window : windows_) {
        if (window) {
            succeeded = SetWindowDisplayAffinity(window, affinity) != FALSE &&
                        succeeded;
        }
    }
    captureExclusionAvailable_ = succeeded;
    if (succeeded) {
        excludedFromCapture_ = excludeFromCapture;
    } else {
        lastError_ = excludeFromCapture
            ? "The monitor border could not be excluded from captured output."
            : "The target border display mode could not be restored.";
    }
}

void CaptureTargetOverlay::Paint(HWND window) {
    PAINTSTRUCT paint{};
    HDC deviceContext = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    HBRUSH border = CreateSolidBrush(CurrentColor());
    FillRect(deviceContext, &client, border);
    DeleteObject(border);
    EndPaint(window, &paint);
}

LRESULT CALLBACK CaptureTargetOverlay::WindowProc(HWND window, UINT message,
                                                  WPARAM wParam, LPARAM lParam) {
    auto* overlay = reinterpret_cast<CaptureTargetOverlay*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        overlay = static_cast<CaptureTargetOverlay*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_PAINT:
        if (overlay) overlay->Paint(window);
        return 0;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace opencapture
