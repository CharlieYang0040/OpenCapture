#include "audio/audio_timeline_mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace opencapture {

void AudioTimelineMixer::Reset(std::uint64_t sessionStartQpc100ns) {
    sessionStartQpc100ns_ = sessionStartQpc100ns;
    nextOutputSample_ = 0;
    systemBlocks_.clear();
    microphoneBlocks_.clear();
    lastError_.clear();
}

bool AudioTimelineMixer::Push(AudioSource source, const AudioPacket& packet,
                              const AudioFormat& format, float volume) {
    Block block;
    if (!Normalize(packet, format, block)) return false;
    block.volume = std::clamp(volume, 0.0F, 2.0F);
    auto& blocks = source == AudioSource::System ? systemBlocks_ : microphoneBlocks_;
    blocks.push_back(std::move(block));
    return true;
}

bool AudioTimelineMixer::Pop(std::int64_t availableThroughSample, std::uint32_t frameCount,
                             MixedAudioChunk& output) {
    if (frameCount == 0 || nextOutputSample_ + frameCount > availableThroughSample) return false;
    output.presentationTimestamp = nextOutputSample_;
    output.stereoSamples.assign(static_cast<std::size_t>(frameCount) * Channels, 0.0F);
    MixBlocks(systemBlocks_, nextOutputSample_, frameCount, output.stereoSamples);
    MixBlocks(microphoneBlocks_, nextOutputSample_, frameCount, output.stereoSamples);
    for (auto& sample : output.stereoSamples) sample = std::clamp(sample, -1.0F, 1.0F);
    nextOutputSample_ += frameCount;
    return true;
}

bool AudioTimelineMixer::Normalize(const AudioPacket& packet, const AudioFormat& format, Block& block) {
    if (format.sampleRate == 0 || format.channels == 0 || format.blockAlign == 0 ||
        packet.frameCount == 0 || packet.bytes.size() <
            static_cast<std::size_t>(packet.frameCount) * format.blockAlign) {
        lastError_ = "Invalid WASAPI packet or mix format.";
        return false;
    }
    if (!format.floatingPoint && format.bitsPerSample != 16 && format.bitsPerSample != 32) {
        lastError_ = "Only float32, PCM16, and PCM32 WASAPI formats are supported.";
        return false;
    }
    const auto qpcDelta = static_cast<std::int64_t>(packet.qpcPosition100ns) -
                          static_cast<std::int64_t>(sessionStartQpc100ns_);
    block.startSample = (qpcDelta * SampleRate) / 10'000'000;
    const auto outputFrames = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(packet.frameCount) * SampleRate +
                                       format.sampleRate / 2) / format.sampleRate));
    block.stereoSamples.resize(static_cast<std::size_t>(outputFrames) * Channels);
    for (std::uint32_t outputFrame = 0; outputFrame < outputFrames; ++outputFrame) {
        const double sourcePosition = static_cast<double>(outputFrame) * format.sampleRate / SampleRate;
        const auto first = std::min<std::uint32_t>(static_cast<std::uint32_t>(sourcePosition),
                                                   packet.frameCount - 1);
        const auto second = std::min<std::uint32_t>(first + 1, packet.frameCount - 1);
        const float fraction = static_cast<float>(sourcePosition - first);
        const auto* firstFrame = packet.bytes.data() + static_cast<std::size_t>(first) * format.blockAlign;
        const auto* secondFrame = packet.bytes.data() + static_cast<std::size_t>(second) * format.blockAlign;
        for (std::uint16_t outputChannel = 0; outputChannel < Channels; ++outputChannel) {
            const std::uint16_t sourceChannel = format.channels == 1 ? 0 :
                std::min<std::uint16_t>(outputChannel, format.channels - 1);
            const float a = ReadSample(firstFrame, sourceChannel, format);
            const float b = ReadSample(secondFrame, sourceChannel, format);
            block.stereoSamples[static_cast<std::size_t>(outputFrame) * Channels + outputChannel] =
                a + (b - a) * fraction;
        }
    }
    return true;
}

float AudioTimelineMixer::ReadSample(const std::uint8_t* frame, std::uint16_t channel,
                                     const AudioFormat& format) {
    const auto bytesPerSample = format.bitsPerSample / 8;
    const auto* sample = frame + static_cast<std::size_t>(channel) * bytesPerSample;
    if (format.floatingPoint && format.bitsPerSample == 32) {
        float value{};
        std::memcpy(&value, sample, sizeof(value));
        return std::clamp(value, -1.0F, 1.0F);
    }
    if (format.bitsPerSample == 16) {
        std::int16_t value{};
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value) / 32768.0F;
    }
    std::int32_t value{};
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value / 2147483648.0);
}

void AudioTimelineMixer::MixBlocks(std::deque<Block>& blocks, std::int64_t firstSample,
                                   std::uint32_t frameCount, std::span<float> destination) {
    const auto endSample = firstSample + frameCount;
    while (!blocks.empty()) {
        const auto blockFrames = static_cast<std::int64_t>(blocks.front().stereoSamples.size() / Channels);
        if (blocks.front().startSample + blockFrames > firstSample) break;
        blocks.pop_front();
    }
    for (const auto& block : blocks) {
        const auto blockFrames = static_cast<std::int64_t>(block.stereoSamples.size() / Channels);
        const auto overlapStart = std::max(firstSample, block.startSample);
        const auto overlapEnd = std::min(endSample, block.startSample + blockFrames);
        if (overlapStart >= overlapEnd) {
            if (block.startSample >= endSample) break;
            continue;
        }
        for (auto sample = overlapStart; sample < overlapEnd; ++sample) {
            const auto destinationFrame = static_cast<std::size_t>(sample - firstSample);
            const auto sourceFrame = static_cast<std::size_t>(sample - block.startSample);
            destination[destinationFrame * Channels] += block.stereoSamples[sourceFrame * Channels] * block.volume;
            destination[destinationFrame * Channels + 1] +=
                block.stereoSamples[sourceFrame * Channels + 1] * block.volume;
        }
    }
}

} // namespace opencapture
