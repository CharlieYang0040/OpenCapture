#pragma once

namespace opencapture {

[[nodiscard]] int ClampUiScalePercent(int percent) noexcept;
[[nodiscard]] float ComputeUiScale(unsigned dpi, int userScalePercent) noexcept;

} // namespace opencapture
