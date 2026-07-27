#include "core/ui_scale.h"

#include <algorithm>

namespace opencapture {

int ClampUiScalePercent(int percent) noexcept {
    return std::clamp(percent, 75, 200);
}

float ComputeUiScale(unsigned dpi, int userScalePercent) noexcept {
    const float windowsScale = static_cast<float>(dpi == 0 ? 96U : dpi) / 96.0F;
    const float userScale = static_cast<float>(ClampUiScalePercent(userScalePercent)) / 100.0F;
    return std::clamp(windowsScale * userScale, 0.75F, 4.0F);
}

} // namespace opencapture
