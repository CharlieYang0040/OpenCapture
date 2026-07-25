#include "audio/audio_timeline_mixer.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

int failures{};

void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}

opencapture::AudioPacket FloatPacket(float value, std::uint32_t frames, std::uint64_t qpc) {
    opencapture::AudioPacket packet;
    packet.frameCount = frames;
    packet.qpcPosition100ns = qpc;
    packet.bytes.resize(static_cast<std::size_t>(frames) * sizeof(float));
    for (std::uint32_t index = 0; index < frames; ++index) {
        std::memcpy(packet.bytes.data() + static_cast<std::size_t>(index) * sizeof(float),
                    &value, sizeof(value));
    }
    return packet;
}

} // namespace

int main() {
    opencapture::AudioTimelineMixer mixer;
    mixer.Reset(1'000'000);
    const opencapture::AudioFormat monoFloat{48'000, 1, 32, 4, true};
    Check(mixer.Push(opencapture::AudioSource::System, FloatPacket(0.25F, 1024, 1'000'000), monoFloat),
          "system packet normalizes");
    Check(mixer.Push(opencapture::AudioSource::Microphone, FloatPacket(0.5F, 1024, 1'000'000), monoFloat),
          "microphone packet normalizes");
    opencapture::MixedAudioChunk chunk;
    Check(mixer.Pop(1024, 1024, chunk), "one AAC-sized chunk becomes available");
    Check(chunk.presentationTimestamp == 0, "first audio PTS is zero");
    Check(chunk.stereoSamples.size() == 2048, "mono input becomes stereo");
    Check(std::abs(chunk.stereoSamples.front() - 0.75F) < 0.0001F, "system and microphone mix");
    Check(!mixer.Pop(1024, 1024, chunk), "mixer does not run ahead of the clock");
    return failures == 0 ? 0 : 1;
}
