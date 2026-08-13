#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct AVCodecContext;
struct AVFormatContext;
struct AVPacket;
struct AVStream;

namespace opencapture {

class FFmpegMuxer final {
public:
    FFmpegMuxer() = default;
    ~FFmpegMuxer();

    FFmpegMuxer(const FFmpegMuxer&) = delete;
    FFmpegMuxer& operator=(const FFmpegMuxer&) = delete;

    bool Open(const std::string& path, const AVCodecContext* videoEncoder,
              const AVCodecContext* audioEncoder = nullptr);
    bool WriteVideoPacket(AVPacket* packet);
    bool WriteAudioPacket(AVPacket* packet);
    bool Finalize();
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return formatContext_ != nullptr; }
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::size_t MaxQueuedPacketCount() const noexcept {
        return maxQueuedPacketCount_.load(std::memory_order_relaxed);
    }

private:
    enum class PacketKind { Video, Audio };
    struct QueuedPacket {
        AVPacket* packet{};
        PacketKind kind{PacketKind::Video};
    };

    bool EnqueuePacket(AVPacket* packet, PacketKind kind);
    bool WritePacketNow(AVPacket* packet, PacketKind kind);
    void WriterLoop() noexcept;
    bool DrainWriter();
    void ClearQueuedPackets() noexcept;
    void SetError(std::string operation, int error);

    AVFormatContext* formatContext_{};
    AVStream* videoStream_{};
    AVStream* audioStream_{};
    int videoTimeBaseNumerator_{};
    int videoTimeBaseDenominator_{};
    int audioTimeBaseNumerator_{};
    int audioTimeBaseDenominator_{};
    bool headerWritten_{};
    bool finalized_{};
    static constexpr std::size_t kMaximumQueuedPackets = 2048;
    mutable std::mutex stateMutex_;
    std::mutex queueMutex_;
    std::condition_variable queueChanged_;
    std::deque<QueuedPacket> packetQueue_;
    std::thread writerThread_;
    bool writerStopping_{};
    bool writerFailed_{};
    std::string writerError_;
    std::atomic_size_t maxQueuedPacketCount_{};
    std::string lastError_;
};

} // namespace opencapture
