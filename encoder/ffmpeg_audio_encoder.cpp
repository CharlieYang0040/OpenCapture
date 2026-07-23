#include "encoder/ffmpeg_audio_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <array>
#include <cstring>

namespace opencapture {

FFmpegAudioEncoder::~FFmpegAudioEncoder() { Close(); }

bool FFmpegAudioEncoder::Open(std::int64_t bitRate) {
    Close();
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        lastError_ = "The FFmpeg AAC encoder is not available.";
        return false;
    }
    codecContext_ = avcodec_alloc_context3(codec);
    if (!codecContext_) {
        lastError_ = "Could not allocate the FFmpeg AAC context.";
        return false;
    }
    codecContext_->sample_rate = 48'000;
    av_channel_layout_default(&codecContext_->ch_layout, 2);
    codecContext_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codecContext_->bit_rate = bitRate;
    codecContext_->time_base = AVRational{1, codecContext_->sample_rate};
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    const int result = avcodec_open2(codecContext_, codec, nullptr);
    if (result < 0) {
        SetError("Could not open the FFmpeg AAC encoder", result);
        Close();
        return false;
    }
    lastError_.clear();
    return true;
}

bool FFmpegAudioEncoder::Send(std::span<const float> interleavedStereo,
                              std::int64_t presentationTimestamp) {
    const int frameSize = FrameSize();
    if (!codecContext_ || frameSize <= 0 ||
        interleavedStereo.size() != static_cast<std::size_t>(frameSize) * 2) {
        lastError_ = "AAC input must contain one complete interleaved stereo frame.";
        return false;
    }
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        lastError_ = "Could not allocate an AAC frame.";
        return false;
    }
    frame->format = codecContext_->sample_fmt;
    frame->sample_rate = codecContext_->sample_rate;
    frame->nb_samples = frameSize;
    frame->pts = presentationTimestamp;
    av_channel_layout_copy(&frame->ch_layout, &codecContext_->ch_layout);
    int result = av_frame_get_buffer(frame, 0);
    if (result >= 0) result = av_frame_make_writable(frame);
    if (result < 0) {
        SetError("Could not allocate AAC sample planes", result);
        av_frame_free(&frame);
        return false;
    }
    auto* left = reinterpret_cast<float*>(frame->data[0]);
    auto* right = reinterpret_cast<float*>(frame->data[1]);
    for (int index = 0; index < frameSize; ++index) {
        left[index] = interleavedStereo[static_cast<std::size_t>(index) * 2];
        right[index] = interleavedStereo[static_cast<std::size_t>(index) * 2 + 1];
    }
    result = avcodec_send_frame(codecContext_, frame);
    av_frame_free(&frame);
    if (result < 0) {
        SetError("Could not submit samples to the AAC encoder", result);
        return false;
    }
    return ReceivePackets(false);
}

bool FFmpegAudioEncoder::Flush() {
    if (!codecContext_) return true;
    const int result = avcodec_send_frame(codecContext_, nullptr);
    if (result < 0 && result != AVERROR_EOF) {
        SetError("Could not flush the AAC encoder", result);
        return false;
    }
    return ReceivePackets(true);
}

int FFmpegAudioEncoder::FrameSize() const noexcept {
    return codecContext_ ? codecContext_->frame_size : 0;
}

bool FFmpegAudioEncoder::ReceivePackets(bool flushing) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        lastError_ = "Could not allocate an AAC packet.";
        return false;
    }
    while (true) {
        const int result = avcodec_receive_packet(codecContext_, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        if (result < 0) {
            SetError(flushing ? "Could not receive a flushed AAC packet" : "Could not receive an AAC packet", result);
            av_packet_free(&packet);
            return false;
        }
        if (packetCallback_ && !packetCallback_(packet)) {
            lastError_ = "The muxer rejected an AAC packet.";
            av_packet_free(&packet);
            return false;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return true;
}

void FFmpegAudioEncoder::Close() noexcept {
    if (codecContext_) avcodec_free_context(&codecContext_);
    packetCallback_ = {};
}

void FFmpegAudioEncoder::SetError(std::string operation, int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    lastError_ = std::move(operation) + ": " + text.data();
}

} // namespace opencapture
