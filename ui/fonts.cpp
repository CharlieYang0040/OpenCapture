#include "ui/fonts.h"

#include "ui/i18n.h"

#include <imgui.h>

#include <Windows.h>

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace opencapture {
namespace {

std::string ToUtf8Path(const std::filesystem::path& path) {
    const auto wide = path.wstring();
    if (wide.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string utf8(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::filesystem::path FontsDirectory() {
    std::array<wchar_t, MAX_PATH> windows{};
    const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (length == 0 || length >= windows.size()) return {};
    return std::filesystem::path(windows.data()) / L"Fonts";
}

std::filesystem::path FirstExisting(std::initializer_list<const wchar_t*> names) {
    const auto directory = FontsDirectory();
    if (directory.empty()) return {};
    for (const wchar_t* name : names) {
        std::filesystem::path candidate = directory / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return {};
}

} // namespace

UiFontStatus LoadUiFonts() {
    UiFontStatus status{};
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig latin{};
    latin.OversampleH = 2;
    latin.OversampleV = 1;
    latin.PixelSnapH = true;

    const auto segoe = FirstExisting({L"segoeui.ttf", L"segoeuib.ttf", L"segoeuisl.ttf", L"tahoma.ttf"});
    ImFont* font = nullptr;
    if (!segoe.empty()) {
        const auto path = ToUtf8Path(segoe);
        font = io.Fonts->AddFontFromFileTTF(path.c_str(), 17.0F, &latin);
        status.latinLoaded = font != nullptr;
    }
    if (font == nullptr) {
        io.Fonts->AddFontDefault();
        status.latinLoaded = true;
    }

    ImFontConfig korean = latin;
    korean.MergeMode = true;
    const auto malgun = FirstExisting({L"malgun.ttf", L"malgunbd.ttf", L"malgunsl.ttf"});
    if (!malgun.empty()) {
        const auto path = ToUtf8Path(malgun);
        status.koreanLoaded =
            io.Fonts->AddFontFromFileTTF(path.c_str(), 17.0F, &korean, io.Fonts->GetGlyphRangesKorean()) != nullptr;
    }
    status.message = status.koreanLoaded ? Tr(Msg::font_ok) : Tr(Msg::font_korean_missing);
    return status;
}

} // namespace opencapture
