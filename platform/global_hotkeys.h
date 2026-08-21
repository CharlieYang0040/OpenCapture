#pragma once

#include "core/hotkey.h"

#include <Windows.h>

#include <array>
#include <string>

namespace opencapture {

enum class HotkeyAction : std::size_t {
    Screenshot,
    ToggleVideoRecording,
    ToggleGifRecording,
    QuickCapture,
    Count,
};

using HotkeyBinding = HotkeyChord;

class GlobalHotkeys final {
public:
    GlobalHotkeys();
    ~GlobalHotkeys();

    GlobalHotkeys(const GlobalHotkeys&) = delete;
    GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;

    bool Initialize(HWND owner);
    void Shutdown();
    bool SetBinding(HotkeyAction action, HotkeyBinding binding);
    bool ClearBinding(HotkeyAction action);
    bool ResetBinding(HotkeyAction action);
    bool ResetDefaults();
    void Suspend();
    bool Resume();

    [[nodiscard]] const std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)>&
    Bindings() const noexcept {
        return bindings_;
    }
    [[nodiscard]] HotkeyBinding DefaultBinding(HotkeyAction action) const noexcept;
    [[nodiscard]] std::string Label(HotkeyAction action) const;
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }
    [[nodiscard]] static int IdFor(HotkeyAction action) noexcept;
    [[nodiscard]] static bool ActionForId(int id, HotkeyAction& action) noexcept;

private:
    [[nodiscard]] static std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)>
    DefaultBindings() noexcept;
    bool Register(HotkeyAction action, HotkeyBinding binding);
    void Unregister(HotkeyAction action);
    bool RegisterAll();
    void UnregisterAll();
    bool Save() const;
    void Load();

    HWND owner_{};
    bool suspended_{};
    std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)> bindings_{};
    std::array<bool, static_cast<std::size_t>(HotkeyAction::Count)> registered_{};
    std::string lastError_;
};

} // namespace opencapture
