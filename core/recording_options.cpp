#include "core/recording_options.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <string>

namespace opencapture {
namespace {

std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool ParseBool(std::string_view text, bool fallback) noexcept {
    if (text == "1" || text == "true" || text == "True") return true;
    if (text == "0" || text == "false" || text == "False") return false;
    return fallback;
}

bool ParseInt(std::string_view text, int& value) noexcept {
    if (text.empty()) return false;
    std::istringstream input{std::string(text)};
    int parsed{};
    if (!(input >> parsed)) return false;
    value = parsed;
    return true;
}

template <typename Enum>
Enum ClampEnum(int value, int maximum, Enum fallback) noexcept {
    return value >= 0 && value <= maximum ? static_cast<Enum>(value) : fallback;
}

} // namespace

RecordingPreferences DefaultRecordingPreferences() noexcept {
    return {};
}

int SnapToAllowedChoice(int value, const int* values, std::size_t count) noexcept {
    if (values == nullptr || count == 0) return value;
    int best = values[0];
    int bestDistance = std::abs(value - values[0]);
    for (std::size_t index = 1; index < count; ++index) {
        const int distance = std::abs(value - values[index]);
        if (distance < bestDistance) {
            best = values[index];
            bestDistance = distance;
        }
    }
    return best;
}

int GifFpsIndex(int framesPerSecond) noexcept {
    const int snapped = SnapToAllowedChoice(framesPerSecond, kGifFpsChoices.data(),
                                            kGifFpsChoices.size());
    return static_cast<int>(std::distance(
        kGifFpsChoices.begin(),
        std::find(kGifFpsChoices.begin(), kGifFpsChoices.end(), snapped)));
}

int GifHeightIndex(int height) noexcept {
    const int snapped =
        SnapToAllowedChoice(height, kGifHeightChoices.data(), kGifHeightChoices.size());
    return static_cast<int>(std::distance(
        kGifHeightChoices.begin(),
        std::find(kGifHeightChoices.begin(), kGifHeightChoices.end(), snapped)));
}

int GifColorIndex(int colors) noexcept {
    const int snapped =
        SnapToAllowedChoice(colors, kGifColorChoices.data(), kGifColorChoices.size());
    return static_cast<int>(std::distance(
        kGifColorChoices.begin(),
        std::find(kGifColorChoices.begin(), kGifColorChoices.end(), snapped)));
}

RecordingPreferences ClampRecordingPreferences(RecordingPreferences preferences) {
    preferences.framesPerSecond = std::clamp(preferences.framesPerSecond, 15, 120);
    preferences.quality = std::clamp(preferences.quality, 0, 2);
    preferences.gifFramesPerSecond = SnapToAllowedChoice(
        preferences.gifFramesPerSecond, kGifFpsChoices.data(), kGifFpsChoices.size());
    preferences.gifHeight = SnapToAllowedChoice(
        preferences.gifHeight, kGifHeightChoices.data(), kGifHeightChoices.size());
    preferences.gifColors = SnapToAllowedChoice(
        preferences.gifColors, kGifColorChoices.data(), kGifColorChoices.size());
    preferences.animationFormat = ClampEnum(static_cast<int>(preferences.animationFormat), 2,
                                            AnimationFormat::WebP);
    preferences.animationProfile = ClampEnum(static_cast<int>(preferences.animationProfile), 6,
                                              AnimationProfile::Balanced);
    preferences.animationQuality = std::clamp(preferences.animationQuality, 1, 100);
    preferences.avifCrf = std::clamp(preferences.avifCrf, 0, 63);
    preferences.profile = ClampEnum(static_cast<int>(preferences.profile), 4,
                                    RecordingProfile::Compatibility);
    preferences.codec = ClampEnum(static_cast<int>(preferences.codec), 3,
                                  VideoCodecPreference::H264);
    preferences.resolution = ClampEnum(static_cast<int>(preferences.resolution), 2,
                                       VideoResolutionLimit::Source);
    preferences.efficiency = ClampEnum(static_cast<int>(preferences.efficiency), 3,
                                       EncoderEfficiencyMode::Realtime);
    preferences.customBitRateMbps = std::clamp(preferences.customBitRateMbps, 1, 100);
    preferences.encoderName.erase(
        std::remove(preferences.encoderName.begin(), preferences.encoderName.end(), '\n'),
        preferences.encoderName.end());
    preferences.encoderName.erase(
        std::remove(preferences.encoderName.begin(), preferences.encoderName.end(), '\r'),
        preferences.encoderName.end());
    return preferences;
}

RecordingPreferences ParseRecordingPreferences(std::string_view text,
                                               RecordingPreferences fallback) {
    RecordingPreferences preferences = ClampRecordingPreferences(std::move(fallback));
    int schema{};
    bool sawAnimationFormat{};
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        const auto separator = trimmed.find('=');
        if (separator == std::string_view::npos) continue;
        const auto key = Trim(trimmed.substr(0, separator));
        const auto value = Trim(trimmed.substr(separator + 1));
        int parsed{};
        if (key == "profile_schema" && ParseInt(value, parsed)) {
            schema = parsed;
        } else if (key == "fps" && ParseInt(value, parsed)) {
            preferences.framesPerSecond = parsed;
        } else if (key == "quality" && ParseInt(value, parsed)) {
            preferences.quality = parsed;
        } else if (key == "remux_to_mp4") {
            preferences.remuxToMp4 = ParseBool(value, preferences.remuxToMp4);
        } else if (key == "system_audio") {
            preferences.systemAudio = ParseBool(value, preferences.systemAudio);
        } else if (key == "microphone") {
            preferences.microphone = ParseBool(value, preferences.microphone);
        } else if (key == "encoder") {
            preferences.encoderName = std::string(value);
        } else if (key == "gif_fps" && ParseInt(value, parsed)) {
            preferences.gifFramesPerSecond = parsed;
        } else if (key == "gif_height" && ParseInt(value, parsed)) {
            preferences.gifHeight = parsed;
        } else if (key == "gif_colors" && ParseInt(value, parsed)) {
            preferences.gifColors = parsed;
        } else if (key == "animation_format" && ParseInt(value, parsed)) {
            preferences.animationFormat = static_cast<AnimationFormat>(parsed);
            sawAnimationFormat = true;
        } else if (key == "animation_profile" && ParseInt(value, parsed)) {
            preferences.animationProfile = static_cast<AnimationProfile>(parsed);
        } else if (key == "animation_quality" && ParseInt(value, parsed)) {
            preferences.animationQuality = parsed;
        } else if (key == "avif_crf" && ParseInt(value, parsed)) {
            preferences.avifCrf = parsed;
        } else if (key == "profile" && ParseInt(value, parsed)) {
            preferences.profile = static_cast<RecordingProfile>(parsed);
        } else if (key == "codec" && ParseInt(value, parsed)) {
            preferences.codec = static_cast<VideoCodecPreference>(parsed);
        } else if (key == "resolution" && ParseInt(value, parsed)) {
            preferences.resolution = static_cast<VideoResolutionLimit>(parsed);
        } else if (key == "efficiency" && ParseInt(value, parsed)) {
            preferences.efficiency = static_cast<EncoderEfficiencyMode>(parsed);
        } else if (key == "custom_bitrate_mbps" && ParseInt(value, parsed)) {
            preferences.customBitRateMbps = parsed;
        } else if (key == "allow_codec_fallback") {
            preferences.allowCodecFallback = ParseBool(value, preferences.allowCodecFallback);
        } else if (key == "use_custom_bitrate") {
            preferences.useCustomBitRate = ParseBool(value, preferences.useCustomBitRate);
        }
    }
    if (schema > 0 && schema < 4 && !sawAnimationFormat) {
        preferences.animationFormat = AnimationFormat::Gif;
        preferences.animationProfile = AnimationProfile::Custom;
    }
    return ClampRecordingPreferences(std::move(preferences));
}

std::string SerializeRecordingPreferences(const RecordingPreferences& preferences) {
    const auto clamped = ClampRecordingPreferences(preferences);
    std::ostringstream output;
    output << "profile_schema=4\n"
           << "fps=" << clamped.framesPerSecond << '\n'
           << "quality=" << clamped.quality << '\n'
           << "remux_to_mp4=" << (clamped.remuxToMp4 ? 1 : 0) << '\n'
           << "system_audio=" << (clamped.systemAudio ? 1 : 0) << '\n'
           << "microphone=" << (clamped.microphone ? 1 : 0) << '\n'
           << "encoder=" << clamped.encoderName << '\n'
           << "gif_fps=" << clamped.gifFramesPerSecond << '\n'
           << "gif_height=" << clamped.gifHeight << '\n'
           << "gif_colors=" << clamped.gifColors << '\n'
           << "animation_format=" << static_cast<int>(clamped.animationFormat) << '\n'
           << "animation_profile=" << static_cast<int>(clamped.animationProfile) << '\n'
           << "animation_quality=" << clamped.animationQuality << '\n'
           << "avif_crf=" << clamped.avifCrf << '\n'
           << "profile=" << static_cast<int>(clamped.profile) << '\n'
           << "codec=" << static_cast<int>(clamped.codec) << '\n'
           << "resolution=" << static_cast<int>(clamped.resolution) << '\n'
           << "efficiency=" << static_cast<int>(clamped.efficiency) << '\n'
           << "custom_bitrate_mbps=" << clamped.customBitRateMbps << '\n'
           << "allow_codec_fallback=" << (clamped.allowCodecFallback ? 1 : 0) << '\n'
           << "use_custom_bitrate=" << (clamped.useCustomBitRate ? 1 : 0) << '\n';
    return output.str();
}

RecordingPreferences ApplyAnimationProfile(RecordingPreferences preferences,
                                            AnimationProfile profile) noexcept {
    preferences.animationProfile = profile;
    switch (profile) {
    case AnimationProfile::Share:
        preferences.animationFormat = AnimationFormat::WebP; preferences.gifHeight = 480;
        preferences.gifFramesPerSecond = 10; preferences.animationQuality = 72; break;
    case AnimationProfile::Balanced:
        preferences.animationFormat = AnimationFormat::WebP; preferences.gifHeight = 720;
        preferences.gifFramesPerSecond = 12; preferences.animationQuality = 82; break;
    case AnimationProfile::Smooth:
        preferences.animationFormat = AnimationFormat::WebP; preferences.gifHeight = 720;
        preferences.gifFramesPerSecond = 20; preferences.animationQuality = 80; break;
    case AnimationProfile::Quality:
        preferences.animationFormat = AnimationFormat::WebP; preferences.gifHeight = 1080;
        preferences.gifFramesPerSecond = 15; preferences.animationQuality = 90; break;
    case AnimationProfile::Compatibility:
        preferences.animationFormat = AnimationFormat::Gif; preferences.gifHeight = 480;
        preferences.gifFramesPerSecond = 10; preferences.gifColors = 128; break;
    case AnimationProfile::Smallest:
        preferences.animationFormat = AnimationFormat::Avif; preferences.gifHeight = 720;
        preferences.gifFramesPerSecond = 12; preferences.avifCrf = 34; break;
    case AnimationProfile::Custom: break;
    }
    return ClampRecordingPreferences(std::move(preferences));
}

AnimationSizeEstimate EstimateAnimationSize(AnimationFormat format, int width, int height,
                                             int framesPerSecond, double seconds, int quality,
                                             int colors, int avifCrf) noexcept {
    if (width <= 0 || height <= 0 || framesPerSecond <= 0 || seconds <= 0.0) return {};
    double lowBits = 0.08;
    double highBits = 0.45;
    if (format == AnimationFormat::Gif) {
        const double scale = std::sqrt(static_cast<double>(std::clamp(colors, 32, 256)) / 256.0);
        lowBits = 0.25 * scale; highBits = 1.50 * scale;
    } else if (format == AnimationFormat::WebP) {
        const double scale = 0.6 + static_cast<double>(std::clamp(quality, 1, 100)) / 100.0 * 0.8;
        lowBits *= scale; highBits *= scale;
    } else {
        const double scale = 0.6 + static_cast<double>(63 - std::clamp(avifCrf, 0, 63)) / 63.0 * 0.8;
        lowBits = 0.04 * scale; highBits = 0.25 * scale;
    }
    const double pixelFrames = static_cast<double>(width) * height * framesPerSecond * seconds;
    return {static_cast<std::uint64_t>(pixelFrames * lowBits / 8.0),
            static_cast<std::uint64_t>(pixelFrames * highBits / 8.0)};
}

RecordingPreferences ApplyRecordingProfile(RecordingPreferences preferences,
                                            RecordingProfile profile) noexcept {
    preferences.profile = profile;
    if (profile != RecordingProfile::Custom) preferences.useCustomBitRate = false;
    switch (profile) {
    case RecordingProfile::Compatibility:
        preferences.framesPerSecond = 60;
        preferences.codec = VideoCodecPreference::H264;
        preferences.resolution = VideoResolutionLimit::Height1080;
        preferences.efficiency = EncoderEfficiencyMode::Realtime;
        preferences.quality = 1;
        preferences.allowCodecFallback = true;
        break;
    case RecordingProfile::Balanced:
        preferences.framesPerSecond = 60;
        preferences.codec = VideoCodecPreference::Auto;
        preferences.resolution = VideoResolutionLimit::Height1080;
        preferences.efficiency = EncoderEfficiencyMode::Balanced;
        preferences.quality = 1;
        preferences.allowCodecFallback = true;
        break;
    case RecordingProfile::Compact:
        preferences.framesPerSecond = 30;
        preferences.codec = VideoCodecPreference::Auto;
        preferences.resolution = VideoResolutionLimit::Height1080;
        preferences.efficiency = EncoderEfficiencyMode::Balanced;
        preferences.quality = 0;
        preferences.allowCodecFallback = true;
        break;
    case RecordingProfile::Quality:
        preferences.framesPerSecond = 60;
        preferences.codec = VideoCodecPreference::Auto;
        preferences.resolution = VideoResolutionLimit::Source;
        preferences.efficiency = EncoderEfficiencyMode::Quality;
        preferences.quality = 2;
        preferences.allowCodecFallback = true;
        break;
    case RecordingProfile::Custom:
        break;
    }
    return ClampRecordingPreferences(std::move(preferences));
}

int ResolutionHeightLimit(VideoResolutionLimit resolution) noexcept {
    switch (resolution) {
    case VideoResolutionLimit::Height1080: return 1080;
    case VideoResolutionLimit::Height720: return 720;
    case VideoResolutionLimit::Source: return 0;
    }
    return 0;
}

std::int64_t RecommendedVideoBitRate(const RecordingPreferences& preferences,
                                     int outputWidth, int outputHeight,
                                     VideoCodecPreference resolvedCodec) noexcept {
    if (preferences.useCustomBitRate) {
        return static_cast<std::int64_t>(std::clamp(preferences.customBitRateMbps, 1, 100)) * 1'000'000;
    }
    if (preferences.profile == RecordingProfile::Compatibility) {
        constexpr std::array<std::int64_t, 3> legacy{6'000'000, 10'000'000, 16'000'000};
        return legacy[static_cast<std::size_t>(std::clamp(preferences.quality, 0, 2))];
    }

    const double pixels = static_cast<double>(std::max(outputWidth, 2)) * std::max(outputHeight, 2);
    const double resolutionScale = std::clamp(pixels / (1920.0 * 1080.0), 0.20, 4.0);
    const double frameScale = std::clamp(static_cast<double>(preferences.framesPerSecond) / 30.0, 0.5, 2.0);
    double codecScale = 1.0;
    if (resolvedCodec == VideoCodecPreference::Hevc) codecScale = 0.72;
    if (resolvedCodec == VideoCodecPreference::Av1) codecScale = 0.62;
    double profileBase = 6'000'000.0;
    if (preferences.profile == RecordingProfile::Compact) profileBase = 3'500'000.0;
    if (preferences.profile == RecordingProfile::Quality) profileBase = 10'000'000.0;
    const auto calculated = static_cast<std::int64_t>(profileBase * resolutionScale * frameScale * codecScale);
    return std::clamp<std::int64_t>(calculated, 1'000'000, 50'000'000);
}

std::uint64_t EstimatedRecordingBytesPerHour(std::int64_t videoBitRate, bool hasAudio,
                                             std::int64_t audioBitRate) noexcept {
    const auto total = std::max<std::int64_t>(0, videoBitRate) +
                       (hasAudio ? std::max<std::int64_t>(0, audioBitRate) : 0);
    return static_cast<std::uint64_t>(total) * 3600ULL / 8ULL;
}

RecordingGpuPressure PredictRecordingGpuPressure(
    const RecordingPreferences& preferences, int estimatedOutputHeight) noexcept {
    int score{};
    switch (preferences.efficiency) {
    case EncoderEfficiencyMode::Realtime: break;
    case EncoderEfficiencyMode::Balanced: score += 1; break;
    case EncoderEfficiencyMode::Efficient: score += 2; break;
    case EncoderEfficiencyMode::Quality: score += 3; break;
    }
    if (preferences.framesPerSecond > 90) score += 2;
    else if (preferences.framesPerSecond > 60) score += 1;
    if (estimatedOutputHeight > 2160) score += 2;
    else if (estimatedOutputHeight > 1080) score += 1;
    if (preferences.codec == VideoCodecPreference::Hevc ||
        preferences.codec == VideoCodecPreference::Av1 ||
        preferences.codec == VideoCodecPreference::Auto) {
        score += 1;
    }
    if (preferences.profile == RecordingProfile::Quality) score += 1;

    if (score == 0) return RecordingGpuPressure::Low;
    if (score <= 2) return RecordingGpuPressure::Moderate;
    if (score <= 4) return RecordingGpuPressure::High;
    return RecordingGpuPressure::VeryHigh;
}

} // namespace opencapture
