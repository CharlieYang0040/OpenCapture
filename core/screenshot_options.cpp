#include "core/screenshot_options.h"

namespace opencapture {

std::string_view ScreenshotProfileSettingValue(ScreenshotProfile profile) noexcept {
    switch (profile) {
    case ScreenshotProfile::PngLossless: return "png_lossless";
    case ScreenshotProfile::WebpDocument: return "webp_document";
    case ScreenshotProfile::WebpBalanced: return "webp_balanced";
    case ScreenshotProfile::JpegCompatible: return "jpeg_compatible";
    case ScreenshotProfile::AvifSmallest: return "avif_smallest";
    }
    return "webp_balanced";
}

ScreenshotProfile ParseScreenshotProfile(std::string_view value, ScreenshotProfile fallback) noexcept {
    if (value == "png_lossless") return ScreenshotProfile::PngLossless;
    if (value == "webp_document") return ScreenshotProfile::WebpDocument;
    if (value == "webp_balanced") return ScreenshotProfile::WebpBalanced;
    if (value == "jpeg_compatible") return ScreenshotProfile::JpegCompatible;
    if (value == "avif_smallest") return ScreenshotProfile::AvifSmallest;
    return fallback;
}

std::wstring_view ScreenshotProfileExtension(ScreenshotProfile profile) noexcept {
    switch (profile) {
    case ScreenshotProfile::PngLossless: return L".png";
    case ScreenshotProfile::WebpDocument:
    case ScreenshotProfile::WebpBalanced: return L".webp";
    case ScreenshotProfile::JpegCompatible: return L".jpg";
    case ScreenshotProfile::AvifSmallest: return L".avif";
    }
    return L".webp";
}

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
