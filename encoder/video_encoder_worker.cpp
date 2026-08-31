#include "encoder/video_encoder_worker.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace opencapture {

VideoEncoderWorker::~VideoEncoderWorker() { Stop(false); }

bool VideoEncoderWorker::Start(FFmpegD3D11Encoder* encoder, std::size_t capacity) {
    Stop(false);
    if (!encoder || !encoder->IsOpen() || capacity == 0) {
        std::scoped_lock lock(mutex_);
        lastError_ = "An open video encoder and a non-zero queue capacity are required.";
        failed_ = true;
        return false;
    }
    {
        std::scoped_lock lock(mutex_);
        encoder_ = encoder;
        capacity_ = capacity;
        queue_.clear();
        stopping_ = false;
        drainAndFlush_ = false;
        failed_ = false;
        lastError_.clear();
    }
    encodedFrameCount_.store(0, std::memory_order_relaxed);
    droppedFrameCount_.store(0, std::memory_order_relaxed);
    maximumQueuedFrameCount_.store(0, std::memory_order_relaxed);
    maximumSubmissionMicroseconds_.store(0, std::memory_order_relaxed);
    try {
        thread_ = std::thread(&VideoEncoderWorker::WorkerLoop, this);
    } catch (const std::exception& error) {
        SetFailure(std::string("Could not start the video encoder worker: ") + error.what());
        encoder_ = nullptr;
        return false;
    }
    return true;
}

VideoEncoderQueueResult VideoEncoderWorker::Enqueue(
    ProcessedFrame frame, std::int64_t presentationTimestamp) {
    std::size_t queued{};
    bool dropped{};
    {
        std::scoped_lock lock(mutex_);
        if (!thread_.joinable() || stopping_ || failed_) return {};
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
            dropped = true;
            droppedFrameCount_.fetch_add(1, std::memory_order_relaxed);
        }
        queue_.push_back({std::move(frame), presentationTimestamp});
        queued = queue_.size();
    }
    auto previous = maximumQueuedFrameCount_.load(std::memory_order_relaxed);
    while (previous < queued && !maximumQueuedFrameCount_.compare_exchange_weak(
               previous, queued, std::memory_order_relaxed)) {
    }
    changed_.notify_one();
    return {true, dropped};
}

bool VideoEncoderWorker::Stop(bool drainAndFlush) noexcept {
    {
        std::scoped_lock lock(mutex_);
        if (!thread_.joinable()) {
            encoder_ = nullptr;
            if (!drainAndFlush) {
                failed_ = false;
                lastError_.clear();
            }
            return !failed_;
        }
        stopping_ = true;
        drainAndFlush_ = drainAndFlush;
        if (!drainAndFlush) queue_.clear();
    }
    changed_.notify_one();
    try {
        thread_.join();
    } catch (...) {
        SetFailure("Could not join the video encoder worker.");
    }
    std::scoped_lock lock(mutex_);
    encoder_ = nullptr;
    queue_.clear();
    if (!drainAndFlush) {
        failed_ = false;
        lastError_.clear();
    }
    return !failed_;
}

bool VideoEncoderWorker::Running() const noexcept {
    std::scoped_lock lock(mutex_);
    return thread_.joinable() && !stopping_ && !failed_;
}

bool VideoEncoderWorker::Failed() const noexcept {
    std::scoped_lock lock(mutex_);
    return failed_;
}

std::string VideoEncoderWorker::LastError() const {
    std::scoped_lock lock(mutex_);
    return lastError_;
}

void VideoEncoderWorker::WorkerLoop() noexcept {
    while (true) {
        QueuedFrame queued{};
        bool shouldFlush{};
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (!stopping_) continue;
                shouldFlush = drainAndFlush_ && !failed_;
            } else {
                queued = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (!queued.frame.texture) {
            if (shouldFlush && encoder_ && !encoder_->Flush()) {
                SetFailure(encoder_->LastError());
            }
            break;
        }

        const auto before = std::chrono::steady_clock::now();
        if (!encoder_ || !encoder_->Send(queued.frame, queued.presentationTimestamp)) {
            SetFailure(encoder_ ? encoder_->LastError() : "The video encoder worker lost its encoder.");
            break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - before).count();
        const auto microseconds = static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
        auto previous = maximumSubmissionMicroseconds_.load(std::memory_order_relaxed);
        while (previous < microseconds && !maximumSubmissionMicroseconds_.compare_exchange_weak(
                   previous, microseconds, std::memory_order_relaxed)) {
        }
        encodedFrameCount_.fetch_add(1, std::memory_order_relaxed);
    }
}

void VideoEncoderWorker::SetFailure(std::string error) noexcept {
    std::scoped_lock lock(mutex_);
    failed_ = true;
    stopping_ = true;
    queue_.clear();
    lastError_ = error.empty() ? "The asynchronous video encoder failed." : std::move(error);
}

} // namespace opencapture
