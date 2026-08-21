#include "platform/global_hotkeys.h"

#include <ShlObj.h>

#include <filesystem>
#include <fstream>

namespace opencapture {
namespace {

constexpr int kFirstHotkeyId = 0x4F00;

static_assert(kHotkeyModAlt == MOD_ALT);
static_assert(kHotkeyModControl == MOD_CONTROL);
static_assert(kHotkeyModShift == MOD_SHIFT);
static_assert(kHotkeyModWin == MOD_WIN);

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
    return RegisterAll();
}

void GlobalHotkeys::Shutdown() {
    UnregisterAll();
    owner_ = nullptr;
    suspended_ = false;
}

bool GlobalHotkeys::SetBinding(HotkeyAction action, HotkeyBinding binding) {
    const auto index = static_cast<std::size_t>(action);
    binding = NormalizeHotkeyChord(binding);
    if (index >= bindings_.size() || !IsValidHotkeyChord(binding)) {
        lastError_ = "A shortcut requires Ctrl, Alt, or Shift and a supported key.";
        return false;
    }
    if (!IsUnboundHotkey(binding)) {
        for (std::size_t other = 0; other < bindings_.size(); ++other) {
            if (other != index && bindings_[other] == binding) {
                lastError_ = "That shortcut is already assigned to another OpenCapture action.";
                return false;
            }
        }
    }

    const auto previous = bindings_[index];
    const bool wasRegistered = registered_[index];
    if (wasRegistered) Unregister(action);
    if (!IsUnboundHotkey(binding) && owner_ && !suspended_ && !Register(action, binding)) {
        if (wasRegistered) Register(action, previous);
        return false;
    }
    bindings_[index] = binding;
    if (!Save()) {
        Unregister(action);
        bindings_[index] = previous;
        if (wasRegistered && owner_ && !suspended_) Register(action, previous);
        lastError_ = "The shortcut was not changed because its settings could not be saved.";
        return false;
    }
    lastError_.clear();
    return true;
}

bool GlobalHotkeys::ClearBinding(HotkeyAction action) {
    return SetBinding(action, {});
}

bool GlobalHotkeys::ResetBinding(HotkeyAction action) {
    return SetBinding(action, DefaultBinding(action));
}

bool GlobalHotkeys::ResetDefaults() {
    const auto defaults = DefaultBindings();
    const auto previous = bindings_;
    const HWND owner = owner_;
    const bool wasSuspended = suspended_;
    Shutdown();
    bindings_ = defaults;
    if (!Save()) {
        bindings_ = previous;
        owner_ = owner;
        suspended_ = wasSuspended;
        if (owner && !wasSuspended) RegisterAll();
        lastError_ = "Default shortcuts could not be saved.";
        return false;
    }
    owner_ = owner;
    suspended_ = wasSuspended;
    lastError_.clear();
    return owner && !wasSuspended ? RegisterAll() : true;
}

void GlobalHotkeys::Suspend() {
    if (!owner_ || suspended_) return;
    UnregisterAll();
    suspended_ = true;
}

bool GlobalHotkeys::Resume() {
    if (!owner_) {
        suspended_ = false;
        return true;
    }
    suspended_ = false;
    return RegisterAll();
}

HotkeyBinding GlobalHotkeys::DefaultBinding(HotkeyAction action) const noexcept {
    const auto defaults = DefaultBindings();
    const auto index = static_cast<std::size_t>(action);
    return index < defaults.size() ? defaults[index] : HotkeyBinding{};
}

std::string GlobalHotkeys::Label(HotkeyAction action) const {
    return HotkeyLabel(bindings_[static_cast<std::size_t>(action)]);
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
        {kHotkeyModControl | kHotkeyModShift, VK_F9},
        {kHotkeyModControl | kHotkeyModShift, VK_F10},
        {kHotkeyModControl | kHotkeyModShift, VK_F11},
        {kHotkeyModControl | kHotkeyModShift, VK_F8},
    }};
}

bool GlobalHotkeys::Register(HotkeyAction action, HotkeyBinding binding) {
    const auto index = static_cast<std::size_t>(action);
    if (IsUnboundHotkey(binding)) {
        registered_[index] = false;
        return true;
    }
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

bool GlobalHotkeys::RegisterAll() {
    bool allRegistered = true;
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        if (!Register(static_cast<HotkeyAction>(index), bindings_[index])) {
            allRegistered = false;
        }
    }
    return allRegistered;
}

void GlobalHotkeys::UnregisterAll() {
    if (!owner_) return;
    for (std::size_t index = 0; index < registered_.size(); ++index) {
        Unregister(static_cast<HotkeyAction>(index));
    }
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
    unsigned modifiers{};
    unsigned virtualKey{};
    while (input >> index >> modifiers >> virtualKey) {
        if (index >= bindings_.size()) continue;
        const HotkeyChord loaded = NormalizeHotkeyChord({modifiers, virtualKey});
        if (IsUnboundHotkey(loaded) || IsValidHotkeyChord(loaded)) {
            bindings_[index] = loaded;
        }
    }
}

} // namespace opencapture
