#pragma once

#include "core/bounded_queue.h"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace opencapture {

enum class AudioEndpointKind {
    SystemLoopback,
    Microphone,
};

struct AudioFormat {
    std::uint32_t sampleRate{};
    std::uint16_t channels{};
    std::uint16_t bitsPerSample{};
    std::uint16_t blockAlign{};
    bool floatingPoint{};
};

struct AudioPacket {
    std::vector<std::uint8_t> bytes;
    std::uint32_t frameCount{};
    std::uint64_t qpcPosition100ns{};
    bool silent{};
};

class WasapiCapture final {
public:
    WasapiCapture() = default;
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    bool Start(AudioEndpointKind endpoint);
    void Stop() noexcept;

    [[nodiscard]] std::optional<AudioPacket> TryPopPacket();
    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] AudioFormat Format() const;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::uint64_t PacketCount() const noexcept { return packetCount_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t DroppedPacketCount() const noexcept {
        return droppedPacketCount_.load(std::memory_order_relaxed);
    }

private:
    void CaptureThread(AudioEndpointKind endpoint) noexcept;
    void FinishSetup(bool success, std::string error = {});
    void SetError(std::string error);

    BoundedQueue<AudioPacket> packets_{128};
    std::thread thread_;
    HANDLE stopEvent_{};
    std::atomic_bool running_{};
    std::atomic_uint64_t packetCount_{};
    std::atomic_uint64_t droppedPacketCount_{};
    mutable std::mutex stateMutex_;
    std::condition_variable setupCondition_;
    AudioFormat format_{};
    std::string lastError_;
    bool setupDone_{};
    bool setupSuccess_{};
};

} // namespace opencapture
