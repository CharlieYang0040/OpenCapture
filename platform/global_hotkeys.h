#pragma once

#include <Windows.h>

#include <array>
#include <string>

namespace opencapture {

enum class HotkeyAction : std::size_t {
    ScreenshotClipboard,
    ToggleVideoRecording,
    ToggleGifRecording,
    Count,
};

struct HotkeyBinding {
    UINT modifiers{};
    UINT virtualKey{};
};

class GlobalHotkeys final {
public:
    GlobalHotkeys();
    ~GlobalHotkeys();

    GlobalHotkeys(const GlobalHotkeys&) = delete;
    GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;

    bool Initialize(HWND owner);
    void Shutdown();
    bool SetBinding(HotkeyAction action, HotkeyBinding binding);
    bool ResetDefaults();

    [[nodiscard]] const std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)>&
    Bindings() const noexcept {
        return bindings_;
    }
    [[nodiscard]] std::string Label(HotkeyAction action) const;
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }
    [[nodiscard]] static int IdFor(HotkeyAction action) noexcept;
    [[nodiscard]] static bool ActionForId(int id, HotkeyAction& action) noexcept;

private:
    [[nodiscard]] static std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)>
    DefaultBindings() noexcept;
    bool Register(HotkeyAction action, HotkeyBinding binding);
    void Unregister(HotkeyAction action);
    bool Save() const;
    void Load();

    HWND owner_{};
    std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)> bindings_{};
    std::array<bool, static_cast<std::size_t>(HotkeyAction::Count)> registered_{};
    std::string lastError_;
};

} // namespace opencapture
