#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

struct AVCodecContext;
struct AVPacket;

namespace opencapture {

class FFmpegAudioEncoder final {
public:
    using PacketCallback = std::function<bool(AVPacket*)>;

    ~FFmpegAudioEncoder();
    FFmpegAudioEncoder(const FFmpegAudioEncoder&) = delete;
    FFmpegAudioEncoder& operator=(const FFmpegAudioEncoder&) = delete;
    FFmpegAudioEncoder() = default;

    bool Open(std::int64_t bitRate = 192'000);
    bool Send(std::span<const float> interleavedStereo, std::int64_t presentationTimestamp);
    bool Flush();
    void Close() noexcept;
    void SetPacketCallback(PacketCallback callback) { packetCallback_ = std::move(callback); }

    [[nodiscard]] bool IsOpen() const noexcept { return codecContext_ != nullptr; }
    [[nodiscard]] int FrameSize() const noexcept;
    [[nodiscard]] const AVCodecContext* CodecContext() const noexcept { return codecContext_; }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    bool ReceivePackets(bool flushing);
    void SetError(std::string operation, int error);

    AVCodecContext* codecContext_{};
    PacketCallback packetCallback_;
    std::string lastError_;
};

} // namespace opencapture
