#include "core/hotkey.h"

#include <array>
#include <cstdio>
#include <string>

namespace opencapture {
namespace {

constexpr unsigned kVkBack = 0x08;
constexpr unsigned kVkTab = 0x09;
constexpr unsigned kVkClear = 0x0C;
constexpr unsigned kVkReturn = 0x0D;
constexpr unsigned kVkShift = 0x10;
constexpr unsigned kVkControl = 0x11;
constexpr unsigned kVkMenu = 0x12;
constexpr unsigned kVkPause = 0x13;
constexpr unsigned kVkCapital = 0x14;
constexpr unsigned kVkEscape = 0x1B;
constexpr unsigned kVkSpace = 0x20;
constexpr unsigned kVkPrior = 0x21;
constexpr unsigned kVkNext = 0x22;
constexpr unsigned kVkEnd = 0x23;
constexpr unsigned kVkHome = 0x24;
constexpr unsigned kVkLeft = 0x25;
constexpr unsigned kVkUp = 0x26;
constexpr unsigned kVkRight = 0x27;
constexpr unsigned kVkDown = 0x28;
constexpr unsigned kVkSnapshot = 0x2C;
constexpr unsigned kVkInsert = 0x2D;
constexpr unsigned kVkDelete = 0x2E;
constexpr unsigned kVkHelp = 0x2F;
constexpr unsigned kVkLWin = 0x5B;
constexpr unsigned kVkRWin = 0x5C;
constexpr unsigned kVkApps = 0x5D;
constexpr unsigned kVkNumpad0 = 0x60;
constexpr unsigned kVkMultiply = 0x6A;
constexpr unsigned kVkAdd = 0x6B;
constexpr unsigned kVkSeparator = 0x6C;
constexpr unsigned kVkSubtract = 0x6D;
constexpr unsigned kVkDecimal = 0x6E;
constexpr unsigned kVkDivide = 0x6F;
constexpr unsigned kVkF1 = 0x70;
constexpr unsigned kVkF24 = 0x87;
constexpr unsigned kVkNumLock = 0x90;
constexpr unsigned kVkScroll = 0x91;
constexpr unsigned kVkLShift = 0xA0;
constexpr unsigned kVkRShift = 0xA1;
constexpr unsigned kVkLControl = 0xA2;
constexpr unsigned kVkRControl = 0xA3;
constexpr unsigned kVkLMenu = 0xA4;
constexpr unsigned kVkRMenu = 0xA5;
constexpr unsigned kVkOem1 = 0xBA;
constexpr unsigned kVkOemPlus = 0xBB;
constexpr unsigned kVkOemComma = 0xBC;
constexpr unsigned kVkOemMinus = 0xBD;
constexpr unsigned kVkOemPeriod = 0xBE;
constexpr unsigned kVkOem2 = 0xBF;
constexpr unsigned kVkOem3 = 0xC0;
constexpr unsigned kVkOem4 = 0xDB;
constexpr unsigned kVkOem5 = 0xDC;
constexpr unsigned kVkOem6 = 0xDD;
constexpr unsigned kVkOem7 = 0xDE;
constexpr unsigned kVkOem8 = 0xDF;
constexpr unsigned kVkOem102 = 0xE2;
constexpr unsigned kVkProcessKey = 0xE5;
constexpr unsigned kVkPacket = 0xE7;

} // namespace

bool IsModifierVirtualKey(unsigned virtualKey) noexcept {
    switch (virtualKey) {
    case kVkShift:
    case kVkControl:
    case kVkMenu:
    case kVkLWin:
    case kVkRWin:
    case kVkLShift:
    case kVkRShift:
    case kVkLControl:
    case kVkRControl:
    case kVkLMenu:
    case kVkRMenu:
        return true;
    default:
        return false;
    }
}

bool IsHotkeyCaptureCancelKey(unsigned virtualKey) noexcept {
    return virtualKey == kVkEscape;
}

bool IsAssignableHotkeyKey(unsigned virtualKey) noexcept {
    if (virtualKey == 0 || IsModifierVirtualKey(virtualKey)) return false;
    switch (virtualKey) {
    case kVkCapital:
    case kVkNumLock:
    case kVkScroll:
    case kVkProcessKey:
    case kVkPacket:
        return false;
    default:
        return true;
    }
}

bool IsUnboundHotkey(const HotkeyChord& chord) noexcept {
    return chord.virtualKey == 0;
}

bool IsValidHotkeyChord(const HotkeyChord& chord) noexcept {
    if (IsUnboundHotkey(chord)) return true;
    return IsAssignableHotkeyKey(chord.virtualKey) &&
           (chord.modifiers & kHotkeyRequiredModifiers) != 0;
}

HotkeyChord NormalizeHotkeyChord(HotkeyChord chord) noexcept {
    chord.modifiers &= (kHotkeyRequiredModifiers | kHotkeyModWin);
    if (chord.virtualKey == 0) return {};
    return chord;
}

std::string VirtualKeyLabel(unsigned virtualKey) {
    if (virtualKey >= kVkF1 && virtualKey <= kVkF24) {
        return "F" + std::to_string(virtualKey - kVkF1 + 1);
    }
    if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
        (virtualKey >= '0' && virtualKey <= '9')) {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= kVkNumpad0 && virtualKey <= kVkNumpad0 + 9) {
        return "NumPad " + std::to_string(virtualKey - kVkNumpad0);
    }

    switch (virtualKey) {
    case kVkBack:
        return "Backspace";
    case kVkTab:
        return "Tab";
    case kVkClear:
        return "Clear";
    case kVkReturn:
        return "Enter";
    case kVkPause:
        return "Pause";
    case kVkEscape:
        return "Esc";
    case kVkSpace:
        return "Space";
    case kVkPrior:
        return "Page Up";
    case kVkNext:
        return "Page Down";
    case kVkEnd:
        return "End";
    case kVkHome:
        return "Home";
    case kVkLeft:
        return "Left";
    case kVkUp:
        return "Up";
    case kVkRight:
        return "Right";
    case kVkDown:
        return "Down";
    case kVkSnapshot:
        return "Print Screen";
    case kVkInsert:
        return "Insert";
    case kVkDelete:
        return "Delete";
    case kVkHelp:
        return "Help";
    case kVkApps:
        return "Menu";
    case kVkMultiply:
        return "NumPad *";
    case kVkAdd:
        return "NumPad +";
    case kVkSeparator:
        return "NumPad ,";
    case kVkSubtract:
        return "NumPad -";
    case kVkDecimal:
        return "NumPad .";
    case kVkDivide:
        return "NumPad /";
    case kVkOem1:
        return ";";
    case kVkOemPlus:
        return "=";
    case kVkOemComma:
        return ",";
    case kVkOemMinus:
        return "-";
    case kVkOemPeriod:
        return ".";
    case kVkOem2:
        return "/";
    case kVkOem3:
        return "`";
    case kVkOem4:
        return "[";
    case kVkOem5:
        return "\\";
    case kVkOem6:
        return "]";
    case kVkOem7:
        return "'";
    case kVkOem8:
        return "OEM 8";
    case kVkOem102:
        return "OEM 102";
    default:
        break;
    }

    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "VK %02X", virtualKey);
    return text.data();
}

std::string HotkeyLabel(const HotkeyChord& chord) {
    if (IsUnboundHotkey(chord)) return "Not set";
    std::string label;
    if ((chord.modifiers & kHotkeyModControl) != 0) label += "Ctrl+";
    if ((chord.modifiers & kHotkeyModAlt) != 0) label += "Alt+";
    if ((chord.modifiers & kHotkeyModShift) != 0) label += "Shift+";
    if ((chord.modifiers & kHotkeyModWin) != 0) label += "Win+";
    label += VirtualKeyLabel(chord.virtualKey);
    return label;
}

} // namespace opencapture
