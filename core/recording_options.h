#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace opencapture {

constexpr std::array kGifFpsChoices{6, 10, 12, 15, 20, 30};
constexpr std::array kGifHeightChoices{360, 480, 720, 1080};
constexpr std::array kGifColorChoices{64, 128, 192, 256};

struct RecordingPreferences {
    int framesPerSecond{60};
    int quality{1};
    bool remuxToMp4{};
    bool systemAudio{true};
    bool microphone{};
    std::string encoderName;
    int gifFramesPerSecond{12};
    int gifHeight{720};
    int gifColors{256};

    friend bool operator==(const RecordingPreferences&, const RecordingPreferences&) = default;
};

[[nodiscard]] RecordingPreferences DefaultRecordingPreferences() noexcept;
[[nodiscard]] int SnapToAllowedChoice(int value, const int* values, std::size_t count) noexcept;
[[nodiscard]] int GifFpsIndex(int framesPerSecond) noexcept;
[[nodiscard]] int GifHeightIndex(int height) noexcept;
[[nodiscard]] int GifColorIndex(int colors) noexcept;
[[nodiscard]] RecordingPreferences ClampRecordingPreferences(RecordingPreferences preferences);
[[nodiscard]] RecordingPreferences ParseRecordingPreferences(
    std::string_view text, RecordingPreferences fallback = DefaultRecordingPreferences());
[[nodiscard]] std::string SerializeRecordingPreferences(const RecordingPreferences& preferences);

} // namespace opencapture
