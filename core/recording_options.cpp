#include "core/recording_options.h"

#include <algorithm>
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
        if (key == "fps" && ParseInt(value, parsed)) {
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
        }
    }
    return ClampRecordingPreferences(std::move(preferences));
}

std::string SerializeRecordingPreferences(const RecordingPreferences& preferences) {
    const auto clamped = ClampRecordingPreferences(preferences);
    std::ostringstream output;
    output << "fps=" << clamped.framesPerSecond << '\n'
           << "quality=" << clamped.quality << '\n'
           << "remux_to_mp4=" << (clamped.remuxToMp4 ? 1 : 0) << '\n'
           << "system_audio=" << (clamped.systemAudio ? 1 : 0) << '\n'
           << "microphone=" << (clamped.microphone ? 1 : 0) << '\n'
           << "encoder=" << clamped.encoderName << '\n'
           << "gif_fps=" << clamped.gifFramesPerSecond << '\n'
           << "gif_height=" << clamped.gifHeight << '\n'
           << "gif_colors=" << clamped.gifColors << '\n';
    return output.str();
}

} // namespace opencapture
