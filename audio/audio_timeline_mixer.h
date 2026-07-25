#pragma once

#include "audio/wasapi_capture.h"

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace opencapture {

enum class AudioSource {
    System,
    Microphone,
};

struct MixedAudioChunk {
    std::vector<float> stereoSamples;
    std::int64_t presentationTimestamp{};
};

class AudioTimelineMixer final {
public:
    static constexpr int SampleRate = 48'000;
    static constexpr int Channels = 2;

    void Reset(std::uint64_t sessionStartQpc100ns);
    bool Push(AudioSource source, const AudioPacket& packet, const AudioFormat& format, float volume = 1.0F);
    bool Pop(std::int64_t availableThroughSample, std::uint32_t frameCount, MixedAudioChunk& output);

    [[nodiscard]] std::int64_t NextOutputSample() const noexcept { return nextOutputSample_; }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    struct Block {
        std::int64_t startSample{};
        std::vector<float> stereoSamples;
        float volume{1.0F};
    };

    bool Normalize(const AudioPacket& packet, const AudioFormat& format, Block& block);
    static float ReadSample(const std::uint8_t* frame, std::uint16_t channel, const AudioFormat& format);
    static void MixBlocks(std::deque<Block>& blocks, std::int64_t firstSample,
                          std::uint32_t frameCount, std::span<float> destination);

    std::uint64_t sessionStartQpc100ns_{};
    std::int64_t nextOutputSample_{};
    std::deque<Block> systemBlocks_;
    std::deque<Block> microphoneBlocks_;
    std::string lastError_;
};

} // namespace opencapture
