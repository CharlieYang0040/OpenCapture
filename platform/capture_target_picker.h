#pragma once

#include "core/capture_target.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opencapture {

struct WindowEntry {
    static constexpr std::size_t IconWidth = 32;
    static constexpr std::size_t IconHeight = 32;

    HWND handle{};
    std::string title;
    std::string processName;
    std::array<std::uint32_t, IconWidth * IconHeight> iconPixels{};
    bool hasIcon{};
};

struct MonitorEntry {
    HMONITOR handle{};
    std::string deviceName;
    RECT bounds{};
    bool primary{};
};

class CaptureTargetPicker final {
public:
    CaptureTargetPicker();

    void Refresh();
    [[nodiscard]] const std::vector<WindowEntry>& Windows() const noexcept { return windows_; }
    [[nodiscard]] const std::vector<MonitorEntry>& Monitors() const noexcept { return monitors_; }
    [[nodiscard]] const CaptureTarget& Selected() const noexcept { return selected_; }
    [[nodiscard]] const std::vector<CaptureRegionPreset>& Presets() const noexcept { return presets_; }
    [[nodiscard]] std::string SelectedLabel() const;
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

    bool SelectWindow(std::size_t index);
    bool SelectMonitor(std::size_t index);
    bool SelectRegion(HWND owner);
    bool CreateRegionPreset(std::string name, RegionAnchorType anchorType, std::size_t windowIndex = 0);
    bool ApplyRegionPreset(std::size_t index);
    bool DeleteRegionPreset(std::size_t index);
    bool RenameRegionPreset(std::size_t index, std::string name);
    bool DuplicateRegionPreset(std::size_t index);
    bool MoveRegionPreset(std::size_t index, int direction);

private:
    void Load();
    void Save() const;
    void LoadPresets();
    bool SavePresets();

    std::vector<WindowEntry> windows_;
    std::vector<MonitorEntry> monitors_;
    CaptureTarget selected_{};
    std::vector<CaptureRegionPreset> presets_;
    std::string lastError_;
};

} // namespace opencapture
