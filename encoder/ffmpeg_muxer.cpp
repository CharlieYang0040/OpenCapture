#include "encoder/ffmpeg_muxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <array>
#include <algorithm>
#include <exception>
#include <utility>

namespace opencapture {

FFmpegMuxer::~FFmpegMuxer() { Close(); }

bool FFmpegMuxer::Open(const std::string& path, const AVCodecContext* videoEncoder,
                       const AVCodecContext* audioEncoder) {
    Close();
    if (path.empty() || !videoEncoder || !videoEncoder->codec) {
        lastError_ = "A path and an open FFmpeg video encoder are required.";
        return false;
    }
    int result = avformat_alloc_output_context2(&formatContext_, nullptr, nullptr, path.c_str());
    if (result < 0 || !formatContext_) {
        SetError("Could not create the output container", result < 0 ? result : AVERROR_UNKNOWN);
        Close();
        return false;
    }
    videoStream_ = avformat_new_stream(formatContext_, nullptr);
    if (!videoStream_) {
        lastError_ = "Could not create the output video stream.";
        Close();
        return false;
    }
    result = avcodec_parameters_from_context(videoStream_->codecpar, videoEncoder);
    if (result < 0) {
        SetError("Could not copy encoder parameters to the output stream", result);
        Close();
        return false;
    }
    videoStream_->codecpar->codec_tag = 0;
    videoStream_->codecpar->format = AV_PIX_FMT_NV12;
    videoStream_->time_base = videoEncoder->time_base;
    videoStream_->avg_frame_rate = videoEncoder->framerate;
    videoTimeBaseNumerator_ = videoEncoder->time_base.num;
    videoTimeBaseDenominator_ = videoEncoder->time_base.den;
    if (audioEncoder) {
        audioStream_ = avformat_new_stream(formatContext_, nullptr);
        if (!audioStream_) {
            lastError_ = "Could not create the output audio stream.";
            Close();
            return false;
        }
        result = avcodec_parameters_from_context(audioStream_->codecpar, audioEncoder);
        if (result < 0) {
            SetError("Could not copy AAC parameters to the output stream", result);
            Close();
            return false;
        }
        audioStream_->codecpar->codec_tag = 0;
        audioStream_->time_base = audioEncoder->time_base;
        audioTimeBaseNumerator_ = audioEncoder->time_base.num;
        audioTimeBaseDenominator_ = audioEncoder->time_base.den;
    }

    if ((formatContext_->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&formatContext_->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            SetError("Could not open the output file", result);
            Close();
            return false;
        }
    }
    result = avformat_write_header(formatContext_, nullptr);
    if (result < 0) {
        SetError("Could not write the container header", result);
        Close();
        return false;
    }
    headerWritten_ = true;
    finalized_ = false;
    {
        std::scoped_lock lock(stateMutex_);
        lastError_.clear();
    }
    {
        std::scoped_lock lock(queueMutex_);
        writerStopping_ = false;
        writerFailed_ = false;
        writerError_.clear();
        packetQueue_.clear();
    }
    maxQueuedPacketCount_.store(0, std::memory_order_relaxed);
    try {
        writerThread_ = std::thread(&FFmpegMuxer::WriterLoop, this);
    } catch (const std::exception& error) {
        {
            std::scoped_lock lock(stateMutex_);
            lastError_ = std::string("Could not start the asynchronous container writer: ") + error.what();
        }
        Close();
        return false;
    }
    return true;
}

bool FFmpegMuxer::WriteVideoPacket(AVPacket* packet) {
    if (!formatContext_ || !videoStream_ || !packet || !headerWritten_ || finalized_) {
        std::scoped_lock lock(stateMutex_);
        lastError_ = "The output container is not ready for video packets.";
        return false;
    }
    return EnqueuePacket(packet, PacketKind::Video);
}

bool FFmpegMuxer::WriteAudioPacket(AVPacket* packet) {
    if (!formatContext_ || !audioStream_ || !packet || !headerWritten_ || finalized_) {
        std::scoped_lock lock(stateMutex_);
        lastError_ = "The output container is not ready for audio packets.";
        return false;
    }
    return EnqueuePacket(packet, PacketKind::Audio);
}

bool FFmpegMuxer::Finalize() {
    if (!formatContext_ || !headerWritten_ || finalized_) return finalized_;
    if (!DrainWriter()) return false;
    const int result = av_write_trailer(formatContext_);
    if (result < 0) {
        SetError("Could not finalize the output container", result);
        return false;
    }
    finalized_ = true;
    return true;
}

void FFmpegMuxer::Close() noexcept {
    try {
        DrainWriter();
    } catch (...) {
    }
    if (formatContext_) {
        if (headerWritten_ && !finalized_ && !writerFailed_) av_write_trailer(formatContext_);
        if ((formatContext_->oformat->flags & AVFMT_NOFILE) == 0 && formatContext_->pb) {
            avio_closep(&formatContext_->pb);
        }
        avformat_free_context(formatContext_);
    }
    formatContext_ = nullptr;
    videoStream_ = nullptr;
    audioStream_ = nullptr;
    videoTimeBaseNumerator_ = 0;
    videoTimeBaseDenominator_ = 0;
    audioTimeBaseNumerator_ = 0;
    audioTimeBaseDenominator_ = 0;
    headerWritten_ = false;
    finalized_ = false;
    ClearQueuedPackets();
    writerStopping_ = false;
    writerFailed_ = false;
    writerError_.clear();
}

std::string FFmpegMuxer::LastError() const {
    std::scoped_lock lock(stateMutex_);
    return lastError_;
}

bool FFmpegMuxer::EnqueuePacket(AVPacket* packet, PacketKind kind) {
    AVPacket* copy = av_packet_clone(packet);
    if (!copy) {
        std::scoped_lock lock(stateMutex_);
        lastError_ = "Could not retain an encoded packet for asynchronous writing.";
        return false;
    }
    std::size_t queued{};
    {
        std::scoped_lock lock(queueMutex_);
        if (writerFailed_ || writerStopping_ || !writerThread_.joinable()) {
            av_packet_free(&copy);
            std::scoped_lock stateLock(stateMutex_);
            lastError_ = writerError_.empty() ? "The asynchronous container writer is unavailable." : writerError_;
            return false;
        }
        if (packetQueue_.size() >= kMaximumQueuedPackets) {
            av_packet_free(&copy);
            std::scoped_lock stateLock(stateMutex_);
            lastError_ = "The container writer queue exceeded 2048 packets; the output device is not keeping up.";
            return false;
        }
        packetQueue_.push_back({copy, kind});
        queued = packetQueue_.size();
    }
    auto previous = maxQueuedPacketCount_.load(std::memory_order_relaxed);
    while (previous < queued && !maxQueuedPacketCount_.compare_exchange_weak(
               previous, queued, std::memory_order_relaxed)) {
    }
    queueChanged_.notify_one();
    return true;
}

bool FFmpegMuxer::WritePacketNow(AVPacket* packet, PacketKind kind) {
    AVStream* stream = kind == PacketKind::Video ? videoStream_ : audioStream_;
    const AVRational encoderTimeBase{
        kind == PacketKind::Video ? videoTimeBaseNumerator_ : audioTimeBaseNumerator_,
        kind == PacketKind::Video ? videoTimeBaseDenominator_ : audioTimeBaseDenominator_};
    av_packet_rescale_ts(packet, encoderTimeBase, stream->time_base);
    packet->stream_index = stream->index;
    packet->pos = -1;
    const int result = av_interleaved_write_frame(formatContext_, packet);
    if (result >= 0) return true;
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(result, text.data(), text.size());
    std::scoped_lock lock(queueMutex_);
    writerError_ = std::string(kind == PacketKind::Video
        ? "Could not write an encoded video packet: "
        : "Could not write an AAC packet: ") + text.data();
    writerFailed_ = true;
    writerStopping_ = true;
    return false;
}

void FFmpegMuxer::WriterLoop() noexcept {
    while (true) {
        QueuedPacket queued{};
        {
            std::unique_lock lock(queueMutex_);
            queueChanged_.wait(lock, [this] { return writerStopping_ || !packetQueue_.empty(); });
            if (packetQueue_.empty()) {
                if (writerStopping_) break;
                continue;
            }
            queued = packetQueue_.front();
            packetQueue_.pop_front();
        }
        const bool written = WritePacketNow(queued.packet, queued.kind);
        av_packet_free(&queued.packet);
        if (!written) break;
    }
    ClearQueuedPackets();
}

bool FFmpegMuxer::DrainWriter() {
    if (!writerThread_.joinable()) return !writerFailed_;
    {
        std::scoped_lock lock(queueMutex_);
        writerStopping_ = true;
    }
    queueChanged_.notify_one();
    writerThread_.join();
    std::string writerError;
    bool failed{};
    {
        std::scoped_lock lock(queueMutex_);
        failed = writerFailed_;
        writerError = writerError_;
    }
    if (failed) {
        std::scoped_lock lock(stateMutex_);
        lastError_ = writerError.empty() ? "The asynchronous container writer failed." : writerError;
        return false;
    }
    return true;
}

void FFmpegMuxer::ClearQueuedPackets() noexcept {
    std::scoped_lock lock(queueMutex_);
    while (!packetQueue_.empty()) {
        auto packet = packetQueue_.front().packet;
        av_packet_free(&packet);
        packetQueue_.pop_front();
    }
}

void FFmpegMuxer::SetError(std::string operation, int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    std::scoped_lock lock(stateMutex_);
    lastError_ = std::move(operation) + ": " + text.data();
}

} // namespace opencapture
