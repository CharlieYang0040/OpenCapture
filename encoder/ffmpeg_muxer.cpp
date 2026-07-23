#include "encoder/ffmpeg_muxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <array>
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
    lastError_.clear();
    return true;
}

bool FFmpegMuxer::WriteVideoPacket(AVPacket* packet) {
    if (!formatContext_ || !videoStream_ || !packet || !headerWritten_ || finalized_) {
        lastError_ = "The output container is not ready for video packets.";
        return false;
    }
    const AVRational encoderTimeBase{videoTimeBaseNumerator_, videoTimeBaseDenominator_};
    av_packet_rescale_ts(packet, encoderTimeBase, videoStream_->time_base);
    packet->stream_index = videoStream_->index;
    packet->pos = -1;
    const int result = av_interleaved_write_frame(formatContext_, packet);
    if (result < 0) {
        SetError("Could not write an encoded video packet", result);
        return false;
    }
    return true;
}

bool FFmpegMuxer::WriteAudioPacket(AVPacket* packet) {
    if (!formatContext_ || !audioStream_ || !packet || !headerWritten_ || finalized_) {
        lastError_ = "The output container is not ready for audio packets.";
        return false;
    }
    const AVRational encoderTimeBase{audioTimeBaseNumerator_, audioTimeBaseDenominator_};
    av_packet_rescale_ts(packet, encoderTimeBase, audioStream_->time_base);
    packet->stream_index = audioStream_->index;
    packet->pos = -1;
    const int result = av_interleaved_write_frame(formatContext_, packet);
    if (result < 0) {
        SetError("Could not write an AAC packet", result);
        return false;
    }
    return true;
}

bool FFmpegMuxer::Finalize() {
    if (!formatContext_ || !headerWritten_ || finalized_) return finalized_;
    const int result = av_write_trailer(formatContext_);
    if (result < 0) {
        SetError("Could not finalize the output container", result);
        return false;
    }
    finalized_ = true;
    return true;
}

void FFmpegMuxer::Close() noexcept {
    if (formatContext_) {
        if (headerWritten_ && !finalized_) av_write_trailer(formatContext_);
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
}

void FFmpegMuxer::SetError(std::string operation, int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    lastError_ = std::move(operation) + ": " + text.data();
}

} // namespace opencapture
