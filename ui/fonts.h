#pragma once

#include <string>

namespace opencapture {

struct UiFontStatus {
    bool latinLoaded{};
    bool koreanLoaded{};
    std::string message;
};

[[nodiscard]] UiFontStatus LoadUiFonts();

} // namespace opencapture
