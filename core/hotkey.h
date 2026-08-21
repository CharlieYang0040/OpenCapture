#pragma once

#include <string>

namespace opencapture {

constexpr unsigned kHotkeyModAlt = 0x0001;
constexpr unsigned kHotkeyModControl = 0x0002;
constexpr unsigned kHotkeyModShift = 0x0004;
constexpr unsigned kHotkeyModWin = 0x0008;
constexpr unsigned kHotkeyRequiredModifiers =
    kHotkeyModAlt | kHotkeyModControl | kHotkeyModShift;

struct HotkeyChord {
    unsigned modifiers{};
    unsigned virtualKey{};

    friend bool operator==(const HotkeyChord&, const HotkeyChord&) = default;
};

[[nodiscard]] bool IsModifierVirtualKey(unsigned virtualKey) noexcept;
[[nodiscard]] bool IsHotkeyCaptureCancelKey(unsigned virtualKey) noexcept;
[[nodiscard]] bool IsAssignableHotkeyKey(unsigned virtualKey) noexcept;
[[nodiscard]] bool IsUnboundHotkey(const HotkeyChord& chord) noexcept;
[[nodiscard]] bool IsValidHotkeyChord(const HotkeyChord& chord) noexcept;
[[nodiscard]] HotkeyChord NormalizeHotkeyChord(HotkeyChord chord) noexcept;
[[nodiscard]] std::string VirtualKeyLabel(unsigned virtualKey);
[[nodiscard]] std::string HotkeyLabel(const HotkeyChord& chord);

} // namespace opencapture
