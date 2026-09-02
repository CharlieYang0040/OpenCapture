#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace opencapture {

// Keep the persisted values stable. Quality was added after Custom.
enum class RecordingProfile { Compatibility = 0, Balanced = 1, Compact = 2, Custom = 3, Quality = 4 };
enum class VideoCodecPreference { Auto, H264, Hevc, Av1 };
enum class VideoResolutionLimit { Source, Height1080, Height720 };
enum class EncoderEfficiencyMode { Realtime, Balanced, Efficient, Quality };
enum class RecordingGpuPressure { Low, Moderate, High, VeryHigh };
enum class AnimationFormat { Gif = 0, WebP = 1, Avif = 2 };
enum class AnimationProfile { Share = 0, Balanced = 1, Smooth = 2, Quality = 3,
                              Compatibility = 4, Smallest = 5, Custom = 6 };

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
    AnimationFormat animationFormat{AnimationFormat::WebP};
    AnimationProfile animationProfile{AnimationProfile::Balanced};
    int animationQuality{82};
    int avifCrf{34};
    RecordingProfile profile{RecordingProfile::Compatibility};
    VideoCodecPreference codec{VideoCodecPreference::H264};
    VideoResolutionLimit resolution{VideoResolutionLimit::Height1080};
    EncoderEfficiencyMode efficiency{EncoderEfficiencyMode::Realtime};
    int customBitRateMbps{10};
    bool allowCodecFallback{true};
    bool useCustomBitRate{};

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
[[nodiscard]] RecordingPreferences ApplyRecordingProfile(RecordingPreferences preferences,
                                                          RecordingProfile profile) noexcept;
[[nodiscard]] RecordingPreferences ApplyAnimationProfile(RecordingPreferences preferences,
                                                          AnimationProfile profile) noexcept;
struct AnimationSizeEstimate { std::uint64_t minimumBytes{}; std::uint64_t maximumBytes{}; };
[[nodiscard]] AnimationSizeEstimate EstimateAnimationSize(AnimationFormat format, int width, int height,
                                                           int framesPerSecond, double seconds,
                                                           int quality, int colors, int avifCrf) noexcept;
[[nodiscard]] int ResolutionHeightLimit(VideoResolutionLimit resolution) noexcept;
[[nodiscard]] std::int64_t RecommendedVideoBitRate(const RecordingPreferences& preferences,
                                                   int outputWidth, int outputHeight,
                                                   VideoCodecPreference resolvedCodec) noexcept;
[[nodiscard]] std::uint64_t EstimatedRecordingBytesPerHour(std::int64_t videoBitRate,
                                                           bool hasAudio,
                                                           std::int64_t audioBitRate = 192'000) noexcept;
[[nodiscard]] RecordingGpuPressure PredictRecordingGpuPressure(
    const RecordingPreferences& preferences, int estimatedOutputHeight) noexcept;

} // namespace opencapture
