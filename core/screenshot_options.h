#pragma once

#include <string_view>

namespace opencapture {

enum class ScreenshotDestination {
    File,
    Clipboard,
    FileAndClipboard,
};

[[nodiscard]] std::string_view ScreenshotDestinationSettingValue(
    ScreenshotDestination destination) noexcept;
[[nodiscard]] ScreenshotDestination ParseScreenshotDestination(
    std::string_view value,
    ScreenshotDestination fallback = ScreenshotDestination::Clipboard) noexcept;

} // namespace opencapture
