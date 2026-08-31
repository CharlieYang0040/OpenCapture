#pragma once

#include "encoder/ffmpeg_d3d11_encoder.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace opencapture {

struct VideoEncoderQueueResult {
    bool accepted{};
    bool droppedOldest{};
};

// Owns all live Send/Flush calls after the encoder is opened. The bounded queue
// prevents an encoder slowdown from blocking capture or growing latency forever.
class VideoEncoderWorker final {
public:
    VideoEncoderWorker() = default;
    ~VideoEncoderWorker();

    VideoEncoderWorker(const VideoEncoderWorker&) = delete;
    VideoEncoderWorker& operator=(const VideoEncoderWorker&) = delete;

    bool Start(FFmpegD3D11Encoder* encoder, std::size_t capacity = 4);
    VideoEncoderQueueResult Enqueue(ProcessedFrame frame,
                                    std::int64_t presentationTimestamp);
    bool Stop(bool drainAndFlush) noexcept;

    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::uint64_t EncodedFrameCount() const noexcept {
        return encodedFrameCount_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t DroppedFrameCount() const noexcept {
        return droppedFrameCount_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t MaximumQueuedFrameCount() const noexcept {
        return maximumQueuedFrameCount_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] double MaximumSubmissionMilliseconds() const noexcept {
        return maximumSubmissionMicroseconds_.load(std::memory_order_relaxed) / 1000.0;
    }

private:
    struct QueuedFrame {
        ProcessedFrame frame;
        std::int64_t presentationTimestamp{};
    };

    void WorkerLoop() noexcept;
    void SetFailure(std::string error) noexcept;

    FFmpegD3D11Encoder* encoder_{};
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<QueuedFrame> queue_;
    std::thread thread_;
    std::size_t capacity_{4};
    bool stopping_{};
    bool drainAndFlush_{};
    bool failed_{};
    std::string lastError_;
    std::atomic_uint64_t encodedFrameCount_{};
    std::atomic_uint64_t droppedFrameCount_{};
    std::atomic_size_t maximumQueuedFrameCount_{};
    std::atomic_uint64_t maximumSubmissionMicroseconds_{};
};

} // namespace opencapture
