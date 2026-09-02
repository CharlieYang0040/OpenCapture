#include "ui/i18n.h"

#include <array>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace opencapture {
namespace {

Language g_language = Language::English;

constexpr auto kEnglish = std::to_array<const char*>({
#define OPENCAPTURE_STRING_EN(id, en, ko) en,
    OPENCAPTURE_STRINGS(OPENCAPTURE_STRING_EN)
#undef OPENCAPTURE_STRING_EN
});

constexpr auto kKorean = std::to_array<const char*>({
#define OPENCAPTURE_STRING_KO(id, en, ko) ko,
    OPENCAPTURE_STRINGS(OPENCAPTURE_STRING_KO)
#undef OPENCAPTURE_STRING_KO
});

static_assert(kEnglish.size() == static_cast<std::size_t>(Msg::Count));
static_assert(kKorean.size() == static_cast<std::size_t>(Msg::Count));

[[nodiscard]] const std::array<const char*, static_cast<std::size_t>(Msg::Count)>& Table() noexcept {
    return g_language == Language::Korean ? kKorean : kEnglish;
}

} // namespace

Language DetectOsLanguage() noexcept {
#ifdef _WIN32
    const LANGID language = PRIMARYLANGID(GetUserDefaultUILanguage());
    if (language == LANG_KOREAN) return Language::Korean;
#endif
    return Language::English;
}

Language ParseLanguage(std::string_view text, Language fallback) noexcept {
    if (text == "ko" || text == "korean") return Language::Korean;
    if (text == "en" || text == "english") return Language::English;
    return fallback;
}

const char* LanguageSettingValue(Language language) noexcept {
    return language == Language::Korean ? "ko" : "en";
}

const char* Tr(Msg id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= Table().size()) return "";
    const char* text = Table()[index];
    return text != nullptr ? text : "";
}

std::wstring TrW(Msg id) {
    const char* text = Tr(id);
    if (text == nullptr || text[0] == '\0') return {};
#ifdef _WIN32
    const int required = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (required <= 1) return {};
    std::wstring wide(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), required);
    return wide;
#else
    std::wstring wide;
    while (*text) {
        wide.push_back(static_cast<wchar_t>(*text++));
    }
    return wide;
#endif
}

void SetLanguage(Language language) noexcept { g_language = language; }

Language CurrentLanguage() noexcept { return g_language; }

std::size_t StringTableCount() noexcept { return static_cast<std::size_t>(Msg::Count); }

bool StringTablesComplete() noexcept {
    if (kEnglish.size() != kKorean.size()) return false;
    for (std::size_t index = 0; index < kEnglish.size(); ++index) {
        if (kEnglish[index] == nullptr || kEnglish[index][0] == '\0') return false;
        if (kKorean[index] == nullptr || kKorean[index][0] == '\0') return false;
    }
    return true;
}

std::string JoinStatus(Msg prefix, std::string_view extra) {
    std::string text = Tr(prefix);
    if (!extra.empty()) {
        text += ' ';
        text.append(extra.data(), extra.size());
    }
    return text;
}

} // namespace opencapture
