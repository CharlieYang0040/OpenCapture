#pragma once

#include <Windows.h>
#include <shellapi.h>

namespace opencapture {

enum class SystemTrayCommand {
    None,
    Open,
    QuickCapture,
    StopRecording,
    Exit,
};

class SystemTray final {
public:
    SystemTray() = default;
    ~SystemTray();

    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    bool Initialize(HWND owner, HINSTANCE instance);
    bool Restore();
    void Shutdown();
    [[nodiscard]] SystemTrayCommand HandleCallback(
        LPARAM eventData, bool recordingActive, bool quickCaptureAvailable);

    [[nodiscard]] bool Available() const noexcept { return available_; }
    [[nodiscard]] UINT TaskbarCreatedMessage() const noexcept {
        return taskbarCreatedMessage_;
    }
    [[nodiscard]] static constexpr UINT CallbackMessage() noexcept {
        return WM_APP + 0x3A1;
    }

private:
    bool AddIcon();

    HWND owner_{};
    HICON icon_{};
    NOTIFYICONDATAW data_{};
    UINT taskbarCreatedMessage_{};
    bool available_{};
};

} // namespace opencapture
