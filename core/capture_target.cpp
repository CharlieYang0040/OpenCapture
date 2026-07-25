#include "core/capture_target.h"

#include <algorithm>
#include <sstream>

namespace opencapture {

RECT ScaleRegionToClient(const CaptureRegionPreset& preset, SIZE currentClientSize) noexcept {
    if (preset.anchorType != RegionAnchorType::WindowClient ||
        preset.referenceClientSize.cx <= 0 || preset.referenceClientSize.cy <= 0 ||
        currentClientSize.cx <= 0 || currentClientSize.cy <= 0) {
        return preset.region;
    }
    const auto scaleX = [=](LONG value) {
        return static_cast<LONG>((static_cast<long long>(value) * currentClientSize.cx + preset.referenceClientSize.cx / 2) /
                                 preset.referenceClientSize.cx);
    };
    const auto scaleY = [=](LONG value) {
        return static_cast<LONG>((static_cast<long long>(value) * currentClientSize.cy + preset.referenceClientSize.cy / 2) /
                                 preset.referenceClientSize.cy);
    };
    return RECT{scaleX(preset.region.left), scaleY(preset.region.top),
                scaleX(preset.region.right), scaleY(preset.region.bottom)};
}

RECT ToLocalClampedRegion(RECT desktopRegion, RECT sourceDesktopBounds) noexcept {
    const LONG width = std::max<LONG>(0, sourceDesktopBounds.right - sourceDesktopBounds.left);
    const LONG height = std::max<LONG>(0, sourceDesktopBounds.bottom - sourceDesktopBounds.top);
    const LONG left = std::clamp(desktopRegion.left - sourceDesktopBounds.left, 0L, width);
    const LONG top = std::clamp(desktopRegion.top - sourceDesktopBounds.top, 0L, height);
    const LONG right = std::clamp(desktopRegion.right - sourceDesktopBounds.left, left, width);
    const LONG bottom = std::clamp(desktopRegion.bottom - sourceDesktopBounds.top, top, height);
    return RECT{left, top, right, bottom};
}

SIZE NormalizeOutputSize(SIZE requestedSize, SIZE sourceSize, bool requireEven) noexcept {
    LONG width = requestedSize.cx > 0 ? requestedSize.cx : sourceSize.cx;
    LONG height = requestedSize.cy > 0 ? requestedSize.cy : sourceSize.cy;
    width = std::max<LONG>(0, width);
    height = std::max<LONG>(0, height);
    if (requireEven) {
        width &= ~1L;
        height &= ~1L;
    }
    return SIZE{width, height};
}

SIZE FitOutputHeight(SIZE sourceSize, LONG maximumHeight, bool requireEven) noexcept {
    if (sourceSize.cx <= 0 || sourceSize.cy <= 0) return {};
    LONG height = maximumHeight > 0 ? std::min(sourceSize.cy, maximumHeight) : sourceSize.cy;
    LONG width = static_cast<LONG>(
        (static_cast<long long>(sourceSize.cx) * height + sourceSize.cy / 2) / sourceSize.cy);
    if (requireEven) {
        width = std::max<LONG>(2, width & ~1L);
        height = std::max<LONG>(2, height & ~1L);
    }
    return SIZE{width, height};
}

double GifDurationLimit(SIZE outputSize, int framesPerSecond,
                        double maximumSeconds, double pixelBudget) noexcept {
    if (outputSize.cx <= 0 || outputSize.cy <= 0 || framesPerSecond <= 0 ||
        maximumSeconds <= 0.0 || pixelBudget <= 0.0) {
        return 0.0;
    }
    const double pixelsPerSecond = static_cast<double>(outputSize.cx) *
        static_cast<double>(outputSize.cy) * framesPerSecond;
    return std::clamp(pixelBudget / pixelsPerSecond, 1.0, maximumSeconds);
}

bool CaptureTarget::IsValid() const noexcept {
    switch (type) {
    case CaptureTargetType::Window:
        return window != nullptr;
    case CaptureTargetType::Monitor:
        return monitor != nullptr;
    case CaptureTargetType::Region:
        return region.right > region.left && region.bottom > region.top;
    }
    return false;
}

std::wstring CaptureTarget::Description() const {
    std::wostringstream output;
    switch (type) {
    case CaptureTargetType::Window:
        output << L"Window (0x" << std::hex << reinterpret_cast<std::uintptr_t>(window) << L')';
        break;
    case CaptureTargetType::Monitor:
        output << L"Monitor (0x" << std::hex << reinterpret_cast<std::uintptr_t>(monitor) << L')';
        break;
    case CaptureTargetType::Region:
        output << L"Region " << region.left << L',' << region.top << L" - "
               << region.right << L',' << region.bottom;
        break;
    }
    return output.str();
}

} // namespace opencapture
