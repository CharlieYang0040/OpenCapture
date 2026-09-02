#pragma once

#include <string_view>

namespace opencapture {

enum class ScreenshotDestination {
    File,
    Clipboard,
    FileAndClipboard,
};

enum class ScreenshotProfile {
    PngLossless,
    WebpDocument,
    WebpBalanced,
    JpegCompatible,
    AvifSmallest,
};

[[nodiscard]] std::string_view ScreenshotProfileSettingValue(ScreenshotProfile profile) noexcept;
[[nodiscard]] ScreenshotProfile ParseScreenshotProfile(
    std::string_view value,
    ScreenshotProfile fallback = ScreenshotProfile::WebpBalanced) noexcept;
[[nodiscard]] std::wstring_view ScreenshotProfileExtension(ScreenshotProfile profile) noexcept;

[[nodiscard]] std::string_view ScreenshotDestinationSettingValue(
    ScreenshotDestination destination) noexcept;
[[nodiscard]] ScreenshotDestination ParseScreenshotDestination(
    std::string_view value,
    ScreenshotDestination fallback = ScreenshotDestination::Clipboard) noexcept;

} // namespace opencapture
