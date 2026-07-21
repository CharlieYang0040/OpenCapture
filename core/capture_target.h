#pragma once

#include <Windows.h>

#include <string>

namespace opencapture {

enum class CaptureTargetType {
    Window,
    Region,
    Monitor,
};

struct CaptureTarget {
    CaptureTargetType type{CaptureTargetType::Monitor};
    HWND window{};
    HMONITOR monitor{};
    RECT region{};

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::wstring Description() const;
};

} // namespace opencapture

