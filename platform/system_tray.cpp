#include "platform/system_tray.h"

#include "app/resource.h"

#include <windowsx.h>

namespace opencapture {
namespace {

constexpr UINT kOpenCommand = 1;
constexpr UINT kQuickCaptureCommand = 2;
constexpr UINT kStopRecordingCommand = 3;
constexpr UINT kExitCommand = 4;

} // namespace

SystemTray::~SystemTray() {
    Shutdown();
}

bool SystemTray::Initialize(HWND owner, HINSTANCE instance) {
    Shutdown();
    if (!owner) return false;
    owner_ = owner;
    icon_ = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_OPENCAPTURE), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!icon_) icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner_;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = CallbackMessage();
    data_.hIcon = icon_;
    wcscpy_s(data_.szTip, L"OpenCapture");
    return AddIcon();
}

bool SystemTray::AddIcon() {
    if (!owner_) return false;
    available_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (available_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return available_;
}

bool SystemTray::Restore() {
    available_ = false;
    return AddIcon();
}

void SystemTray::Shutdown() {
    if (available_) Shell_NotifyIconW(NIM_DELETE, &data_);
    available_ = false;
    owner_ = nullptr;
    icon_ = nullptr;
    data_ = {};
    taskbarCreatedMessage_ = 0;
}

SystemTrayCommand SystemTray::HandleCallback(
    LPARAM eventData, bool recordingActive, bool quickCaptureAvailable) {
    const UINT event = LOWORD(eventData);
    if (event == NIN_SELECT || event == NIN_KEYSELECT ||
        event == WM_LBUTTONDBLCLK) {
        return SystemTrayCommand::Open;
    }
    if (event != WM_CONTEXTMENU && event != WM_RBUTTONUP) {
        return SystemTrayCommand::None;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) return SystemTrayCommand::None;
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kOpenCommand, L"Open OpenCapture");
    AppendMenuW(menu,
                MF_STRING | (quickCaptureAvailable ? MF_ENABLED : MF_GRAYED),
                kQuickCaptureCommand, L"Quick Capture");
    AppendMenuW(menu,
                MF_STRING | (recordingActive ? MF_ENABLED : MF_GRAYED),
                kStopRecordingCommand, L"Stop current recording");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit OpenCapture");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        cursor.x, cursor.y, 0, owner_, nullptr);
    DestroyMenu(menu);
    PostMessageW(owner_, WM_NULL, 0, 0);

    switch (command) {
    case kOpenCommand:
        return SystemTrayCommand::Open;
    case kQuickCaptureCommand:
        return SystemTrayCommand::QuickCapture;
    case kStopRecordingCommand:
        return SystemTrayCommand::StopRecording;
    case kExitCommand:
        return SystemTrayCommand::Exit;
    default:
        return SystemTrayCommand::None;
    }
}

} // namespace opencapture
