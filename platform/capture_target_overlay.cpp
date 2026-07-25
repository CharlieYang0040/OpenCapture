#include "platform/capture_target_overlay.h"

#include <dwmapi.h>

#include <algorithm>

namespace opencapture {
namespace {

constexpr wchar_t kOverlayWindowClass[] = L"OpenCaptureTargetOverlay";
constexpr int kBorderThickness = 3;
constexpr COLORREF kTransparentColor = RGB(1, 2, 3);

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
    if (window_) return true;
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

    constexpr DWORD extendedStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    window_ = CreateWindowExW(
        extendedStyle, kOverlayWindowClass, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance_, this);
    if (!window_) {
        lastError_ = "Could not create the capture target overlay window.";
        return false;
    }

    SetLayeredWindowAttributes(window_, kTransparentColor, 255, LWA_COLORKEY);
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    captureExclusionAvailable_ =
        SetWindowDisplayAffinity(window_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    if (!captureExclusionAvailable_) {
        lastError_ = "The capture target border could not be excluded from captured output.";
    }
    return true;
}

void CaptureTargetOverlay::Update(const CaptureTarget& target, CaptureOverlayState state) {
    if (!window_) return;
    const bool targetChanged = !SameTarget(target_, target);
    target_ = target;
    const bool stateChanged = state_ != state;
    state_ = state;
    const ULONGLONG now = GetTickCount64();
    const bool flashExpired = screenshotFlashUntil_ != 0 &&
                              now >= screenshotFlashUntil_;
    if (flashExpired) screenshotFlashUntil_ = 0;
    const bool repaintState = stateChanged || flashExpired ||
                              now < screenshotFlashUntil_;
    const bool dynamicTarget = target.type == CaptureTargetType::Window;
    if (!targetChanged && !dynamicTarget) {
        if (repaintState) InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (!targetChanged && dynamicTarget && now - lastGeometryUpdate_ < 50) {
        if (repaintState) InvalidateRect(window_, nullptr, FALSE);
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
        SetWindowPos(window_, HWND_TOPMOST, bounds_.left, bounds_.top,
                     bounds_.right - bounds_.left, bounds_.bottom - bounds_.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible_ = true;
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void CaptureTargetOverlay::FlashScreenshot(std::uint32_t durationMilliseconds) {
    screenshotFlashUntil_ = GetTickCount64() + std::max<std::uint32_t>(durationMilliseconds, 1);
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void CaptureTargetOverlay::Hide() {
    if (window_ && visible_) ShowWindow(window_, SW_HIDE);
    visible_ = false;
}

void CaptureTargetOverlay::Shutdown() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
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

void CaptureTargetOverlay::Paint() {
    PAINTSTRUCT paint{};
    HDC deviceContext = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    HBRUSH transparent = CreateSolidBrush(kTransparentColor);
    FillRect(deviceContext, &client, transparent);
    DeleteObject(transparent);

    const COLORREF color = CurrentColor();
    HBRUSH border = CreateSolidBrush(color);
    RECT top{client.left, client.top, client.right, std::min(client.bottom, client.top + kBorderThickness)};
    RECT bottom{client.left, std::max(client.top, client.bottom - kBorderThickness), client.right, client.bottom};
    RECT left{client.left, client.top, std::min(client.right, client.left + kBorderThickness), client.bottom};
    RECT right{std::max(client.left, client.right - kBorderThickness), client.top, client.right, client.bottom};
    FillRect(deviceContext, &top, border);
    FillRect(deviceContext, &bottom, border);
    FillRect(deviceContext, &left, border);
    FillRect(deviceContext, &right, border);
    DeleteObject(border);
    EndPaint(window_, &paint);
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
        if (overlay) overlay->Paint();
        return 0;
    case WM_TIMER:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace opencapture
