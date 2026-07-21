#include "core/capture_target.h"

#include <sstream>

namespace opencapture {

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

