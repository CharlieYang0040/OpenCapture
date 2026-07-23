#pragma once

#include <Windows.h>

#include <string>

namespace opencapture {

enum class CaptureTargetType {
    Window,
    Region,
    Monitor,
};

enum class RegionAnchorType {
    VirtualDesktop,
    WindowClient,
};

struct CaptureRegionPreset {
    std::string id;
    std::string name;
    RegionAnchorType anchorType{RegionAnchorType::VirtualDesktop};
    RECT region{};
    std::string processName;
    std::string windowTitleHint;
    SIZE referenceClientSize{};
};

[[nodiscard]] RECT ScaleRegionToClient(const CaptureRegionPreset& preset, SIZE currentClientSize) noexcept;
[[nodiscard]] RECT ToLocalClampedRegion(RECT desktopRegion, RECT sourceDesktopBounds) noexcept;
[[nodiscard]] SIZE NormalizeOutputSize(SIZE requestedSize, SIZE sourceSize, bool requireEven) noexcept;
[[nodiscard]] SIZE FitOutputHeight(SIZE sourceSize, LONG maximumHeight, bool requireEven) noexcept;
[[nodiscard]] double GifDurationLimit(SIZE outputSize, int framesPerSecond,
                                      double maximumSeconds = 30.0,
                                      double pixelBudget = 500'000'000.0) noexcept;

struct CaptureTarget {
    CaptureTargetType type{CaptureTargetType::Monitor};
    HWND window{};
    HMONITOR monitor{};
    RECT region{};

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::wstring Description() const;
};

} // namespace opencapture
