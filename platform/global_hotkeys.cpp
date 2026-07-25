#include "platform/global_hotkeys.h"

#include <ShlObj.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace opencapture {
namespace {

constexpr int kFirstHotkeyId = 0x4F00;

std::filesystem::path SettingsPath() {
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                    nullptr, &localAppData))) {
        return {};
    }
    std::filesystem::path directory(localAppData);
    CoTaskMemFree(localAppData);
    directory /= L"OpenCapture";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory / L"hotkeys.txt";
}

std::string KeyLabel(UINT virtualKey) {
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    }
    if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
        (virtualKey >= '0' && virtualKey <= '9')) {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey == VK_SNAPSHOT) return "Print Screen";
    return "Unknown";
}

} // namespace

GlobalHotkeys::GlobalHotkeys() : bindings_(DefaultBindings()) {
    Load();
}

GlobalHotkeys::~GlobalHotkeys() {
    Shutdown();
}

bool GlobalHotkeys::Initialize(HWND owner) {
    Shutdown();
    owner_ = owner;
    lastError_.clear();
    bool allRegistered = true;
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        if (!Register(static_cast<HotkeyAction>(index), bindings_[index])) {
            allRegistered = false;
        }
    }
    return allRegistered;
}

void GlobalHotkeys::Shutdown() {
    if (owner_) {
        for (std::size_t index = 0; index < registered_.size(); ++index) {
            Unregister(static_cast<HotkeyAction>(index));
        }
    }
    owner_ = nullptr;
}

bool GlobalHotkeys::SetBinding(HotkeyAction action, HotkeyBinding binding) {
    const auto index = static_cast<std::size_t>(action);
    if (index >= bindings_.size() || binding.virtualKey == 0 ||
        (binding.modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT)) == 0) {
        lastError_ = "A shortcut requires Ctrl, Alt, or Shift and a supported key.";
        return false;
    }
    for (std::size_t other = 0; other < bindings_.size(); ++other) {
        if (other != index &&
            bindings_[other].modifiers == binding.modifiers &&
            bindings_[other].virtualKey == binding.virtualKey) {
            lastError_ = "That shortcut is already assigned to another OpenCapture action.";
            return false;
        }
    }

    const auto previous = bindings_[index];
    const bool wasRegistered = registered_[index];
    if (wasRegistered) Unregister(action);
    if (owner_ && !Register(action, binding)) {
        if (wasRegistered) Register(action, previous);
        return false;
    }
    bindings_[index] = binding;
    if (!Save()) {
        Unregister(action);
        bindings_[index] = previous;
        if (wasRegistered) Register(action, previous);
        lastError_ = "The shortcut was not changed because its settings could not be saved.";
        return false;
    }
    lastError_.clear();
    return true;
}

bool GlobalHotkeys::ResetDefaults() {
    const auto defaults = DefaultBindings();
    const auto previous = bindings_;
    const HWND owner = owner_;
    Shutdown();
    bindings_ = defaults;
    if (!Save()) {
        bindings_ = previous;
        if (owner) Initialize(owner);
        lastError_ = "Default shortcuts could not be saved.";
        return false;
    }
    return owner ? Initialize(owner) : true;
}

std::string GlobalHotkeys::Label(HotkeyAction action) const {
    const auto& binding = bindings_[static_cast<std::size_t>(action)];
    std::string label;
    if ((binding.modifiers & MOD_CONTROL) != 0) label += "Ctrl+";
    if ((binding.modifiers & MOD_ALT) != 0) label += "Alt+";
    if ((binding.modifiers & MOD_SHIFT) != 0) label += "Shift+";
    label += KeyLabel(binding.virtualKey);
    return label;
}

int GlobalHotkeys::IdFor(HotkeyAction action) noexcept {
    return kFirstHotkeyId + static_cast<int>(action);
}

bool GlobalHotkeys::ActionForId(int id, HotkeyAction& action) noexcept {
    const int index = id - kFirstHotkeyId;
    if (index < 0 || index >= static_cast<int>(HotkeyAction::Count)) return false;
    action = static_cast<HotkeyAction>(index);
    return true;
}

std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)>
GlobalHotkeys::DefaultBindings() noexcept {
    return {{
        {MOD_CONTROL | MOD_SHIFT, VK_F9},
        {MOD_CONTROL | MOD_SHIFT, VK_F10},
        {MOD_CONTROL | MOD_SHIFT, VK_F11},
    }};
}

bool GlobalHotkeys::Register(HotkeyAction action, HotkeyBinding binding) {
    const auto index = static_cast<std::size_t>(action);
    if (!owner_ || !RegisterHotKey(owner_, IdFor(action),
                                   binding.modifiers | MOD_NOREPEAT,
                                   binding.virtualKey)) {
        registered_[index] = false;
        lastError_ = "Windows or another application is already using that shortcut. Choose a different shortcut.";
        return false;
    }
    registered_[index] = true;
    return true;
}

void GlobalHotkeys::Unregister(HotkeyAction action) {
    const auto index = static_cast<std::size_t>(action);
    if (owner_ && registered_[index]) UnregisterHotKey(owner_, IdFor(action));
    registered_[index] = false;
}

bool GlobalHotkeys::Save() const {
    const auto path = SettingsPath();
    if (path.empty()) return false;
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(std::filesystem::path(temporary), std::ios::trunc);
        if (!output) return false;
        for (std::size_t index = 0; index < bindings_.size(); ++index) {
            output << index << ' ' << bindings_[index].modifiers << ' '
                   << bindings_[index].virtualKey << '\n';
        }
        if (!output) return false;
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void GlobalHotkeys::Load() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    std::ifstream input(path);
    std::size_t index{};
    UINT modifiers{};
    UINT virtualKey{};
    while (input >> index >> modifiers >> virtualKey) {
        if (index < bindings_.size() && virtualKey != 0 &&
            (modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT)) != 0) {
            bindings_[index] = {modifiers, virtualKey};
        }
    }
}

} // namespace opencapture
