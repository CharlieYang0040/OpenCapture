#include "core/screenshot_options.h"

namespace opencapture {

std::string_view ScreenshotDestinationSettingValue(
    ScreenshotDestination destination) noexcept {
    switch (destination) {
    case ScreenshotDestination::File:
        return "file";
    case ScreenshotDestination::FileAndClipboard:
        return "file_and_clipboard";
    case ScreenshotDestination::Clipboard:
    default:
        return "clipboard";
    }
}

ScreenshotDestination ParseScreenshotDestination(
    std::string_view value, ScreenshotDestination fallback) noexcept {
    if (value == "clipboard") return ScreenshotDestination::Clipboard;
    if (value == "file") return ScreenshotDestination::File;
    if (value == "file_and_clipboard") return ScreenshotDestination::FileAndClipboard;
    return fallback;
}

} // namespace opencapture
