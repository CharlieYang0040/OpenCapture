#include "platform/capture_target_picker.h"

#include <ShlObj.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <sstream>

namespace opencapture {
namespace {

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::filesystem::path SettingsPath() {
    PWSTR path{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) return {};
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    result /= L"OpenCapture";
    std::filesystem::create_directories(result);
    return result / L"settings.ini";
}

std::filesystem::path PresetsPath() {
    auto result = SettingsPath();
    if (!result.empty()) result.replace_filename(L"region_presets.dat");
    return result;
}

std::filesystem::path SelectionSettingsPath() {
    auto result = SettingsPath();
    if (!result.empty()) result.replace_filename(L"selection_overlay_settings.txt");
    return result;
}

HICON FindWindowIcon(HWND window) {
    DWORD_PTR result{};
    constexpr UINT timeoutMs = 100;
    if (SendMessageTimeoutW(window, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            timeoutMs, &result) && result != 0) {
        return reinterpret_cast<HICON>(result);
    }
    if (SendMessageTimeoutW(window, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            timeoutMs, &result) && result != 0) {
        return reinterpret_cast<HICON>(result);
    }
    if (SendMessageTimeoutW(window, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            timeoutMs, &result) && result != 0) {
        return reinterpret_cast<HICON>(result);
    }
    auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    return icon;
}

bool ExtractWindowIcon(HWND window, std::array<std::uint32_t, WindowEntry::IconWidth * WindowEntry::IconHeight>& pixels) {
    HICON icon = FindWindowIcon(window);
    if (!icon) return false;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(WindowEntry::IconWidth);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(WindowEntry::IconHeight);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bitmapPixels{};
    HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc || !bitmapPixels) {
        if (dc) DeleteDC(dc);
        if (bitmap) DeleteObject(bitmap);
        return false;
    }

    HGDIOBJ previous = SelectObject(dc, bitmap);
    std::memset(bitmapPixels, 0, pixels.size() * sizeof(std::uint32_t));
    const BOOL drawn = DrawIconEx(dc, 0, 0, icon, static_cast<int>(WindowEntry::IconWidth),
                                  static_cast<int>(WindowEntry::IconHeight), 0, nullptr, DI_NORMAL);
    if (drawn) {
        std::memcpy(pixels.data(), bitmapPixels, pixels.size() * sizeof(std::uint32_t));
        const bool hasAlpha = std::any_of(pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
            return (pixel & 0xFF000000U) != 0;
        });
        if (!hasAlpha) {
            for (auto& pixel : pixels) {
                if ((pixel & 0x00FFFFFFU) != 0) pixel |= 0xFF000000U;
            }
        }
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    return drawn != FALSE;
}

struct OverlayState {
    POINT anchor{};
    POINT cursor{};
    RECT result{};
    RECT activeMonitor{};
    std::vector<RECT> monitors;
    std::array<HWND, 4> dimWindows{};
    HWND inputWindow{};
    HWND visualWindow{};
    int desktopX{};
    int desktopY{};
    int desktopWidth{};
    int desktopHeight{};
    int outsideDimmingPercent{30};
    bool dragging{};
    bool hasSelection{};
    bool accepted{};
};

RECT CurrentSelection(const OverlayState& state) {
    if (state.dragging) {
        return RECT{
            std::min(state.anchor.x, state.cursor.x),
            std::min(state.anchor.y, state.cursor.y),
            std::max(state.anchor.x, state.cursor.x),
            std::max(state.anchor.y, state.cursor.y),
        };
    }
    return state.result;
}

RECT MonitorBoundsAt(const OverlayState& state, POINT clientPoint) {
    POINT screenPoint{clientPoint.x + state.desktopX, clientPoint.y + state.desktopY};
    const HMONITOR monitor = MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        return RECT{0, 0, state.desktopWidth, state.desktopHeight};
    }
    OffsetRect(&info.rcMonitor, -state.desktopX, -state.desktopY);
    return info.rcMonitor;
}

void PositionOverlayWindow(HWND window, const OverlayState& state, const RECT& rectangle) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (!window || width <= 0 || height <= 0 || state.outsideDimmingPercent <= 0) {
        if (window) ShowWindow(window, SW_HIDE);
        return;
    }
    SetWindowPos(window, HWND_TOPMOST,
                 state.desktopX + rectangle.left, state.desktopY + rectangle.top,
                 width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void UpdateOverlayLayout(OverlayState& state) {
    const RECT desktop{0, 0, state.desktopWidth, state.desktopHeight};
    if (state.dragging || state.hasSelection) {
        const RECT selection = CurrentSelection(state);
        const std::array rectangles{
            RECT{desktop.left, desktop.top, desktop.right, selection.top},
            RECT{desktop.left, selection.bottom, desktop.right, desktop.bottom},
            RECT{desktop.left, selection.top, selection.left, selection.bottom},
            RECT{selection.right, selection.top, desktop.right, selection.bottom},
        };
        for (std::size_t index = 0; index < state.dimWindows.size(); ++index) {
            PositionOverlayWindow(state.dimWindows[index], state, rectangles[index]);
        }
    } else {
        PositionOverlayWindow(state.dimWindows[0], state, desktop);
        for (std::size_t index = 1; index < state.dimWindows.size(); ++index) {
            ShowWindow(state.dimWindows[index], SW_HIDE);
        }
    }
    if (state.visualWindow) {
        SetWindowPos(state.visualWindow, HWND_TOPMOST,
                     state.desktopX, state.desktopY,
                     state.desktopWidth, state.desktopHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(state.visualWindow, nullptr, FALSE);
        UpdateWindow(state.visualWindow);
    }
}

void FillFrame(HDC dc, RECT rectangle, int thickness, COLORREF color) {
    if (rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top) return;
    thickness = std::clamp(
        thickness, 1,
        static_cast<int>(std::max<LONG>(
            1, std::min(rectangle.right - rectangle.left,
                        rectangle.bottom - rectangle.top) / 2)));
    HBRUSH brush = CreateSolidBrush(color);
    const std::array edges{
        RECT{rectangle.left, rectangle.top, rectangle.right, rectangle.top + thickness},
        RECT{rectangle.left, rectangle.bottom - thickness, rectangle.right, rectangle.bottom},
        RECT{rectangle.left, rectangle.top + thickness, rectangle.left + thickness, rectangle.bottom - thickness},
        RECT{rectangle.right - thickness, rectangle.top + thickness, rectangle.right, rectangle.bottom - thickness},
    };
    for (const auto& edge : edges) FillRect(dc, &edge, brush);
    DeleteObject(brush);
}

void DrawOverlayText(HDC dc, RECT rectangle, const wchar_t* text) {
    HBRUSH background = CreateSolidBrush(RGB(22, 26, 34));
    FillRect(dc, &rectangle, background);
    DeleteObject(background);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    RECT textBounds = rectangle;
    InflateRect(&textBounds, -8, -5);
    DrawTextW(dc, text, -1, &textBounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void PaintVisualOverlay(HWND window, const OverlayState& state) {
    constexpr COLORREF transparentKey = RGB(1, 2, 3);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    HBRUSH transparentBrush = CreateSolidBrush(transparentKey);
    FillRect(dc, &client, transparentBrush);
    DeleteObject(transparentBrush);

    constexpr wchar_t instructions[] =
        L"Drag to select within one monitor | Arrow: 1px | Shift+Arrow: 10px | Enter: confirm | Esc: cancel";
    for (const RECT& monitor : state.monitors) {
        RECT label{monitor.left + 20, monitor.top + 20,
                   std::min(monitor.left + 760, monitor.right - 20), monitor.top + 54};
        if (label.right > label.left) DrawOverlayText(dc, label, instructions);
    }

    if (state.dragging || state.hasSelection) {
        RECT selection = CurrentSelection(state);
        RECT outer = selection;
        InflateRect(&outer, 1, 1);
        FillFrame(dc, outer, 4, RGB(255, 255, 255));
        FillFrame(dc, selection, 2, RGB(35, 145, 255));

        const std::wstring dimensions =
            std::to_wstring(selection.right - selection.left) + L" x " +
            std::to_wstring(selection.bottom - selection.top);
        constexpr int labelWidth = 150;
        constexpr int labelHeight = 32;
        LONG labelX = selection.left + 8;
        LONG labelY = selection.top + 8;
        if (state.activeMonitor.right > state.activeMonitor.left) {
            labelX = std::clamp(labelX, state.activeMonitor.left,
                                std::max(state.activeMonitor.left, state.activeMonitor.right - labelWidth));
            labelY = std::clamp(labelY, state.activeMonitor.top,
                                std::max(state.activeMonitor.top, state.activeMonitor.bottom - labelHeight));
        }
        RECT label{labelX, labelY, labelX + labelWidth, labelY + labelHeight};
        DrawOverlayText(dc, label, dimensions.c_str());
    }
    EndPaint(window, &paint);
}

LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    const bool inputWindow = window == state->inputWindow;
    const bool visualWindow = window == state->visualWindow;
    switch (message) {
    case WM_NCHITTEST:
        return inputWindow ? HTCLIENT : HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        if (!inputWindow) return 0;
        state->anchor = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        state->activeMonitor = MonitorBoundsAt(*state, state->anchor);
        state->anchor = ClampPointToRect(state->anchor, state->activeMonitor);
        state->cursor = state->anchor;
        state->dragging = true;
        SetCapture(window);
        UpdateOverlayLayout(*state);
        return 0;
    case WM_MOUSEMOVE:
        if (inputWindow && state->dragging) {
            state->cursor = ClampPointToRect(
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, state->activeMonitor);
            UpdateOverlayLayout(*state);
        }
        return 0;
    case WM_LBUTTONUP:
        if (inputWindow && state->dragging) {
            state->cursor = ClampPointToRect(
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, state->activeMonitor);
            state->dragging = false;
            ReleaseCapture();
            const int left = std::min(state->anchor.x, state->cursor.x);
            const int top = std::min(state->anchor.y, state->cursor.y);
            const int right = std::max(state->anchor.x, state->cursor.x);
            const int bottom = std::max(state->anchor.y, state->cursor.y);
            if (right > left && bottom > top) {
                state->result = RECT{left, top, right, bottom};
                state->hasSelection = true;
            }
            UpdateOverlayLayout(*state);
        }
        return 0;
    case WM_KEYDOWN:
        if (!inputWindow) return 0;
        if (wParam == VK_ESCAPE) DestroyWindow(window);
        else if (wParam == VK_RETURN && state->hasSelection) {
            OffsetRect(&state->result, state->desktopX, state->desktopY);
            state->accepted = true;
            DestroyWindow(window);
        } else if (state->hasSelection && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)) {
            const int amount = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 10 : 1;
            const LONG dx = wParam == VK_LEFT ? -amount : (wParam == VK_RIGHT ? amount : 0);
            const LONG dy = wParam == VK_UP ? -amount : (wParam == VK_DOWN ? amount : 0);
            state->result = MoveRectWithinBounds(
                state->result, dx, dy, state->activeMonitor);
            UpdateOverlayLayout(*state);
        }
        return 0;
    case WM_PAINT: {
        if (visualWindow) {
            PaintVisualOverlay(window, *state);
        } else {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(window, &paint);
        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RunRegionOverlay(HWND owner, const RECT* initialRegion,
                      const RegionSelectionSettings& settings, RECT& result) {
    constexpr wchar_t className[] = L"OpenCaptureRegionOverlay";
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = OverlayProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursor(nullptr, IDC_CROSS);
    cls.lpszClassName = className;
    RegisterClassExW(&cls);
    OverlayState state{};
    state.desktopX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    state.desktopY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    state.desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    state.desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    state.outsideDimmingPercent = std::clamp(settings.outsideDimmingPercent, 0, 70);
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT bounds, LPARAM parameter) -> BOOL {
        auto& overlayState = *reinterpret_cast<OverlayState*>(parameter);
        RECT local = *bounds;
        OffsetRect(&local, -overlayState.desktopX, -overlayState.desktopY);
        overlayState.monitors.push_back(local);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    if (initialRegion) {
        state.result = *initialRegion;
        OffsetRect(&state.result, -state.desktopX, -state.desktopY);
        state.hasSelection = true;
        const POINT center{
            (state.result.left + state.result.right) / 2,
            (state.result.top + state.result.bottom) / 2,
        };
        state.activeMonitor = MonitorBoundsAt(state, center);
        IntersectRect(&state.result, &state.result, &state.activeMonitor);
    }
    state.inputWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        className, L"Select capture region", WS_POPUP,
        state.desktopX, state.desktopY, state.desktopWidth, state.desktopHeight,
        owner, nullptr, cls.hInstance, &state);
    if (!state.inputWindow) return false;
    SetLayeredWindowAttributes(state.inputWindow, 0, 1, LWA_ALPHA);

    const BYTE dimAlpha = static_cast<BYTE>(
        std::clamp(state.outsideDimmingPercent * 255 / 100, 0, 255));
    for (auto& dimWindow : state.dimWindows) {
        dimWindow = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            className, L"", WS_POPUP, 0, 0, 0, 0,
            nullptr, nullptr, cls.hInstance, &state);
        if (dimWindow) SetLayeredWindowAttributes(dimWindow, 0, dimAlpha, LWA_ALPHA);
    }
    state.visualWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        className, L"", WS_POPUP,
        state.desktopX, state.desktopY, state.desktopWidth, state.desktopHeight,
        nullptr, nullptr, cls.hInstance, &state);
    if (state.visualWindow) {
        constexpr COLORREF transparentKey = RGB(1, 2, 3);
        SetLayeredWindowAttributes(state.visualWindow, transparentKey, 0, LWA_COLORKEY);
        ShowWindow(state.visualWindow, SW_SHOWNOACTIVATE);
    }
    ShowWindow(state.inputWindow, SW_SHOW);
    UpdateOverlayLayout(state);
    SetForegroundWindow(state.inputWindow);
    SetFocus(state.inputWindow);
    MSG message{};
    while (IsWindow(state.inputWindow) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state.visualWindow) DestroyWindow(state.visualWindow);
    for (auto& dimWindow : state.dimWindows) {
        if (dimWindow) DestroyWindow(dimWindow);
    }
    UnregisterClassW(className, cls.hInstance);
    if (!state.accepted) return false;
    result = state.result;
    return true;
}

} // namespace

CaptureTargetPicker::CaptureTargetPicker() {
    Refresh();
    Load();
    LoadSelectionSettings();
    LoadPresets();
}

void CaptureTargetPicker::Refresh() {
    windows_.clear();
    monitors_.clear();
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& entries = *reinterpret_cast<std::vector<WindowEntry>*>(parameter);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
        const int length = GetWindowTextLengthW(window);
        if (length <= 0) return TRUE;
        std::wstring title(static_cast<std::size_t>(length + 1), L'\0');
        GetWindowTextW(window, title.data(), length + 1);
        title.resize(static_cast<std::size_t>(length));
        DWORD processId{};
        GetWindowThreadProcessId(window, &processId);
        std::wstring processName = L"Unknown";
        if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)) {
            std::wstring path(32768, L'\0');
            DWORD size = static_cast<DWORD>(path.size());
            if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
                path.resize(size);
                processName = std::filesystem::path(path).filename().wstring();
            }
            CloseHandle(process);
        }
        WindowEntry entry{};
        entry.handle = window;
        entry.title = Utf8(title);
        entry.processName = Utf8(processName);
        entry.hasIcon = ExtractWindowIcon(window, entry.iconPixels);
        entries.push_back(std::move(entry));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows_));
    std::sort(windows_.begin(), windows_.end(), [](const auto& left, const auto& right) { return left.title < right.title; });

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM parameter) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto& entries = *reinterpret_cast<std::vector<MonitorEntry>*>(parameter);
            entries.push_back(MonitorEntry{monitor, Utf8(info.szDevice), info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors_));
}

bool CaptureTargetPicker::SelectWindow(std::size_t index) {
    if (index >= windows_.size()) return false;
    selected_ = CaptureTarget{CaptureTargetType::Window, windows_[index].handle, nullptr, {}};
    Save();
    return true;
}

bool CaptureTargetPicker::SelectMonitor(std::size_t index) {
    if (index >= monitors_.size()) return false;
    selected_ = CaptureTarget{CaptureTargetType::Monitor, nullptr, monitors_[index].handle, monitors_[index].bounds};
    Save();
    return true;
}

bool CaptureTargetPicker::SelectRegion(HWND owner) {
    CaptureTarget target{};
    if (!PickTemporaryRegion(owner, target)) return false;
    selected_ = target;
    Save();
    return true;
}

bool CaptureTargetPicker::PickTemporaryRegion(HWND owner, CaptureTarget& target) {
    RECT region{};
    if (!RunRegionOverlay(owner, nullptr, selectionSettings_, region)) return false;
    target = CaptureTarget{
        CaptureTargetType::Region,
        nullptr,
        MonitorFromRect(&region, MONITOR_DEFAULTTONEAREST),
        region,
    };
    return target.IsValid();
}

bool CaptureTargetPicker::ApplySelectionSettings(RegionSelectionSettings settings) {
    settings.outsideDimmingPercent = std::clamp(settings.outsideDimmingPercent, 0, 70);
    const auto previous = selectionSettings_;
    selectionSettings_ = settings;
    if (!SaveSelectionSettings()) {
        selectionSettings_ = previous;
        lastError_ = "Region selection appearance could not be saved.";
        return false;
    }
    lastError_.clear();
    return true;
}

bool CaptureTargetPicker::ResetSelectionSettings() {
    return ApplySelectionSettings(RegionSelectionSettings{});
}

bool CaptureTargetPicker::CreateRegionPreset(std::string name, RegionAnchorType anchorType, std::size_t windowIndex) {
    lastError_.clear();
    if (name.empty()) { lastError_ = "Preset name is required."; return false; }
    if (selected_.type != CaptureTargetType::Region || !selected_.IsValid()) {
        lastError_ = "Select a screen region before saving a preset.";
        return false;
    }
    CaptureRegionPreset preset;
    preset.id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    preset.name = std::move(name);
    preset.anchorType = anchorType;
    preset.region = selected_.region;
    if (anchorType == RegionAnchorType::WindowClient) {
        if (windowIndex >= windows_.size() || !IsWindow(windows_[windowIndex].handle)) {
            lastError_ = "Choose a valid anchor window.";
            return false;
        }
        const auto& entry = windows_[windowIndex];
        RECT client{};
        if (!GetClientRect(entry.handle, &client)) { lastError_ = "Cannot read the anchor window size."; return false; }
        POINT origin{0, 0};
        ClientToScreen(entry.handle, &origin);
        RECT clientOnScreen{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
        RECT intersection{};
        if (!IntersectRect(&intersection, &selected_.region, &clientOnScreen)) {
            lastError_ = "The selected region does not overlap the anchor window client area.";
            return false;
        }
        OffsetRect(&intersection, -origin.x, -origin.y);
        preset.region = intersection;
        preset.processName = entry.processName;
        preset.windowTitleHint = entry.title;
        preset.referenceClientSize = SIZE{client.right, client.bottom};
    }
    presets_.push_back(std::move(preset));
    if (!SavePresets()) { presets_.pop_back(); return false; }
    return true;
}

bool CaptureTargetPicker::ApplyRegionPreset(std::size_t index) {
    lastError_.clear();
    if (index >= presets_.size()) { lastError_ = "Preset no longer exists."; return false; }
    const auto& preset = presets_[index];
    RECT resolved = preset.region;
    if (preset.anchorType == RegionAnchorType::WindowClient) {
        Refresh();
        std::vector<const WindowEntry*> matches;
        for (const auto& window : windows_) if (window.processName == preset.processName) matches.push_back(&window);
        const WindowEntry* anchor{};
        const auto exact = std::find_if(matches.begin(), matches.end(), [&](const auto* window) {
            return window->title == preset.windowTitleHint;
        });
        if (exact != matches.end()) anchor = *exact;
        else if (matches.size() == 1) anchor = matches.front();
        if (!anchor) {
            lastError_ = matches.empty() ? "The preset anchor window is not open."
                                         : "Multiple matching windows are open; reselect the anchor window.";
            return false;
        }
        RECT client{};
        if (!GetClientRect(anchor->handle, &client) || client.right <= 0 || client.bottom <= 0) {
            lastError_ = "The preset anchor window is minimized or unavailable.";
            return false;
        }
        resolved = ScaleRegionToClient(preset, SIZE{client.right, client.bottom});
        POINT origin{0, 0};
        ClientToScreen(anchor->handle, &origin);
        OffsetRect(&resolved, origin.x, origin.y);
    }
    if (resolved.right <= resolved.left || resolved.bottom <= resolved.top) {
        lastError_ = "The preset resolved to an invalid region.";
        return false;
    }
    selected_ = CaptureTarget{CaptureTargetType::Region, nullptr,
                              MonitorFromRect(&resolved, MONITOR_DEFAULTTONEAREST), resolved};
    Save();
    return true;
}

bool CaptureTargetPicker::DeleteRegionPreset(std::size_t index) {
    lastError_.clear();
    if (index >= presets_.size()) return false;
    const auto removed = presets_[index];
    presets_.erase(presets_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!SavePresets()) {
        presets_.insert(presets_.begin() + static_cast<std::ptrdiff_t>(index), removed);
        return false;
    }
    return true;
}

bool CaptureTargetPicker::RenameRegionPreset(std::size_t index, std::string name) {
    lastError_.clear();
    if (index >= presets_.size()) return false;
    if (name.empty()) { lastError_ = "Preset name is required."; return false; }
    const std::string previous = presets_[index].name;
    presets_[index].name = std::move(name);
    if (!SavePresets()) { presets_[index].name = previous; return false; }
    return true;
}

bool CaptureTargetPicker::DuplicateRegionPreset(std::size_t index) {
    lastError_.clear();
    if (index >= presets_.size()) return false;
    CaptureRegionPreset copy = presets_[index];
    copy.id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    copy.name += " (Copy)";
    presets_.insert(presets_.begin() + static_cast<std::ptrdiff_t>(index + 1), copy);
    if (!SavePresets()) {
        presets_.erase(presets_.begin() + static_cast<std::ptrdiff_t>(index + 1));
        return false;
    }
    return true;
}

bool CaptureTargetPicker::MoveRegionPreset(std::size_t index, int direction) {
    lastError_.clear();
    if (index >= presets_.size() || (direction != -1 && direction != 1)) return false;
    const auto destination = static_cast<std::ptrdiff_t>(index) + direction;
    if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(presets_.size())) return false;
    std::swap(presets_[index], presets_[static_cast<std::size_t>(destination)]);
    if (!SavePresets()) {
        std::swap(presets_[index], presets_[static_cast<std::size_t>(destination)]);
        return false;
    }
    return true;
}

std::string CaptureTargetPicker::SelectedLabel() const {
    if (!selected_.IsValid()) return "No target selected";
    if (selected_.type == CaptureTargetType::Region) {
        return "Region " + std::to_string(selected_.region.left) + "," + std::to_string(selected_.region.top) + " " +
               std::to_string(selected_.region.right - selected_.region.left) + "x" +
               std::to_string(selected_.region.bottom - selected_.region.top);
    }
    if (selected_.type == CaptureTargetType::Window) {
        for (const auto& item : windows_) if (item.handle == selected_.window) return item.title + " (" + item.processName + ")";
        return "Window unavailable";
    }
    for (const auto& item : monitors_) if (item.handle == selected_.monitor) return item.deviceName + (item.primary ? " (Primary)" : "");
    return "Monitor unavailable";
}

void CaptureTargetPicker::Save() const {
    const auto path = SettingsPath();
    if (path.empty()) return;
    std::ofstream output(path, std::ios::trunc);
    output << "type=" << static_cast<int>(selected_.type) << '\n';
    output << "region=" << selected_.region.left << ',' << selected_.region.top << ',' << selected_.region.right << ',' << selected_.region.bottom << '\n';
    if (selected_.type == CaptureTargetType::Monitor) {
        for (const auto& item : monitors_) if (item.handle == selected_.monitor) output << "monitor=" << item.deviceName << '\n';
    } else if (selected_.type == CaptureTargetType::Window) {
        for (const auto& item : windows_) if (item.handle == selected_.window) {
            output << "window_title=" << item.title << '\n';
            output << "window_process=" << item.processName << '\n';
        }
    }
}

void CaptureTargetPicker::Load() {
    std::ifstream input(SettingsPath());
    if (!input) return;
    int type = static_cast<int>(CaptureTargetType::Monitor);
    std::string monitorName;
    std::string windowTitle;
    std::string windowProcess;
    char comma{};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream value(line.substr(line.find('=') + 1));
        if (line.rfind("type=", 0) == 0) value >> type;
        else if (line.rfind("region=", 0) == 0) value >> selected_.region.left >> comma >> selected_.region.top >> comma >> selected_.region.right >> comma >> selected_.region.bottom;
        else if (line.rfind("monitor=", 0) == 0) monitorName = line.substr(8);
        else if (line.rfind("window_title=", 0) == 0) windowTitle = line.substr(13);
        else if (line.rfind("window_process=", 0) == 0) windowProcess = line.substr(15);
    }
    selected_.type = static_cast<CaptureTargetType>(type);
    if (selected_.type == CaptureTargetType::Monitor) {
        const auto found = std::find_if(monitors_.begin(), monitors_.end(), [&](const auto& item) { return item.deviceName == monitorName; });
        if (found != monitors_.end()) { selected_.monitor = found->handle; selected_.region = found->bounds; }
    } else if (selected_.type == CaptureTargetType::Window) {
        const auto found = std::find_if(windows_.begin(), windows_.end(), [&](const auto& item) {
            return item.title == windowTitle && item.processName == windowProcess;
        });
        if (found != windows_.end()) selected_.window = found->handle;
    } else if (selected_.type == CaptureTargetType::Region &&
               selected_.region.right > selected_.region.left &&
               selected_.region.bottom > selected_.region.top) {
        selected_.monitor = MonitorFromRect(&selected_.region, MONITOR_DEFAULTTONEAREST);
    }
    if (!selected_.IsValid() && !monitors_.empty()) {
        const auto primary = std::find_if(monitors_.begin(), monitors_.end(), [](const auto& item) { return item.primary; });
        const auto& fallback = primary != monitors_.end() ? *primary : monitors_.front();
        selected_ = CaptureTarget{CaptureTargetType::Monitor, nullptr, fallback.handle, fallback.bounds};
    }
}

void CaptureTargetPicker::LoadSelectionSettings() {
    std::ifstream input(SelectionSettingsPath());
    if (!input) return;
    int outsideDimmingPercent = selectionSettings_.outsideDimmingPercent;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("outside_dimming_percent=", 0) == 0) {
            std::istringstream value(line.substr(24));
            value >> outsideDimmingPercent;
        }
    }
    selectionSettings_.outsideDimmingPercent = std::clamp(outsideDimmingPercent, 0, 70);
}

bool CaptureTargetPicker::SaveSelectionSettings() const {
    const auto path = SelectionSettingsPath();
    if (path.empty()) return false;
    const std::filesystem::path temporary(path.wstring() + L".tmp");
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
        output << "outside_dimming_percent="
               << selectionSettings_.outsideDimmingPercent << '\n';
        if (!output) return false;
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void CaptureTargetPicker::LoadPresets() {
    presets_.clear();
    std::ifstream input(PresetsPath());
    CaptureRegionPreset preset;
    int anchor{};
    while (input >> std::quoted(preset.id) >> std::quoted(preset.name) >> anchor
                 >> preset.region.left >> preset.region.top >> preset.region.right >> preset.region.bottom
                 >> std::quoted(preset.processName) >> std::quoted(preset.windowTitleHint)
                 >> preset.referenceClientSize.cx >> preset.referenceClientSize.cy) {
        preset.anchorType = static_cast<RegionAnchorType>(anchor);
        if (!preset.id.empty() && !preset.name.empty() && preset.region.right > preset.region.left && preset.region.bottom > preset.region.top) {
            presets_.push_back(preset);
        }
    }
}

bool CaptureTargetPicker::SavePresets() {
    const auto path = PresetsPath();
    if (path.empty()) { lastError_ = "Cannot locate the local settings folder."; return false; }
    const auto temporary = path.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) { lastError_ = "Cannot write the preset file."; return false; }
    for (const auto& preset : presets_) {
        output << std::quoted(preset.id) << ' ' << std::quoted(preset.name) << ' '
               << static_cast<int>(preset.anchorType) << ' '
               << preset.region.left << ' ' << preset.region.top << ' ' << preset.region.right << ' ' << preset.region.bottom << ' '
               << std::quoted(preset.processName) << ' ' << std::quoted(preset.windowTitleHint) << ' '
               << preset.referenceClientSize.cx << ' ' << preset.referenceClientSize.cy << '\n';
    }
    output.close();
    if (!output) { lastError_ = "Failed while writing the preset file."; return false; }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) { lastError_ = "Cannot replace the preset file."; return false; }
    return true;
}

} // namespace opencapture
