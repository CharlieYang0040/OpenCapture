#include "encoder/ffmpeg_animation_converter.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <memory>

namespace opencapture {
namespace {

std::string ErrorText(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}

struct PacketDelete { void operator()(AVPacket* value) const { av_packet_free(&value); } };
struct FrameDelete { void operator()(AVFrame* value) const { av_frame_free(&value); } };
struct CodecDelete { void operator()(AVCodecContext* value) const { avcodec_free_context(&value); } };
struct SwsDelete { void operator()(SwsContext* value) const { sws_freeContext(value); } };

} // namespace

bool FFmpegAnimationConverter::Convert(const std::string& inputPath, const std::string& outputPath,
                                       AnimationConversionOptions options, std::stop_token stopToken,
                                       std::function<void(double)> progress) {
    lastError_.clear();
    if (options.format != AnimationFormat::WebP && options.format != AnimationFormat::Avif) {
        lastError_ = "The modern animation converter requires WebP or AVIF.";
        return false;
    }
    AVFormatContext* input{};
    int result = avformat_open_input(&input, inputPath.c_str(), nullptr, nullptr);
    if (result < 0 || avformat_find_stream_info(input, nullptr) < 0) {
        lastError_ = "Could not open animation source: " + ErrorText(result);
        avformat_close_input(&input);
        return false;
    }
    const int streamIndex = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* decoder = streamIndex >= 0
        ? avcodec_find_decoder(input->streams[streamIndex]->codecpar->codec_id) : nullptr;
    std::unique_ptr<AVCodecContext, CodecDelete> decode(decoder ? avcodec_alloc_context3(decoder) : nullptr);
    if (!decode || avcodec_parameters_to_context(decode.get(), input->streams[streamIndex]->codecpar) < 0 ||
        avcodec_open2(decode.get(), decoder, nullptr) < 0) {
        lastError_ = "Could not open the animation source decoder.";
        avformat_close_input(&input);
        return false;
    }

    const char* muxerName = options.format == AnimationFormat::WebP ? "webp" : "avif";
    const char* encoderName = options.format == AnimationFormat::WebP ? "libwebp_anim" : "libsvtav1";
    AVFormatContext* output{};
    result = avformat_alloc_output_context2(&output, nullptr, muxerName, outputPath.c_str());
    const AVCodec* encoder = avcodec_find_encoder_by_name(encoderName);
    std::unique_ptr<AVCodecContext, CodecDelete> encode(encoder ? avcodec_alloc_context3(encoder) : nullptr);
    AVStream* outputStream = output ? avformat_new_stream(output, nullptr) : nullptr;
    if (result < 0 || !output || !encode || !outputStream) {
        lastError_ = std::string("Required animation encoder is unavailable: ") + encoderName;
        if (output) avformat_free_context(output);
        avformat_close_input(&input);
        return false;
    }
    encode->width = decode->width;
    encode->height = decode->height;
    encode->pix_fmt = AV_PIX_FMT_YUV420P;
    encode->time_base = input->streams[streamIndex]->time_base;
    if (encode->time_base.num <= 0 || encode->time_base.den <= 0) encode->time_base = AVRational{1, 1000};
    encode->framerate = av_guess_frame_rate(input, input->streams[streamIndex], nullptr);
    encode->gop_size = std::max(1, encode->framerate.num > 0 ? encode->framerate.num / encode->framerate.den * 2 : 24);
    encode->max_b_frames = 0;
    if (output->oformat->flags & AVFMT_GLOBALHEADER) encode->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (options.format == AnimationFormat::WebP) {
        av_opt_set_int(encode->priv_data, "lossless", 0, 0);
        av_opt_set_double(encode->priv_data, "quality", std::clamp(options.quality, 1, 100), 0);
    } else {
        av_opt_set_int(encode->priv_data, "crf", std::clamp(options.avifCrf, 0, 63), 0);
        av_opt_set_int(encode->priv_data, "preset", 8, 0);
    }
    result = avcodec_open2(encode.get(), encoder, nullptr);
    if (result >= 0) result = avcodec_parameters_from_context(outputStream->codecpar, encode.get());
    outputStream->time_base = encode->time_base;
    if (result >= 0 && !(output->oformat->flags & AVFMT_NOFILE)) {
        result = avio_open(&output->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
    }
    AVDictionary* muxOptions{};
    av_dict_set(&muxOptions, "loop", "0", 0);
    if (result >= 0) result = avformat_write_header(output, &muxOptions);
    av_dict_free(&muxOptions);
    if (result < 0) {
        lastError_ = "Could not open animation output: " + ErrorText(result);
        if (output->pb) avio_closep(&output->pb);
        avformat_free_context(output);
        avformat_close_input(&input);
        return false;
    }

    std::unique_ptr<SwsContext, SwsDelete> scaler(sws_getContext(
        decode->width, decode->height, decode->pix_fmt, encode->width, encode->height,
        encode->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr));
    std::unique_ptr<AVPacket, PacketDelete> packet(av_packet_alloc());
    std::unique_ptr<AVPacket, PacketDelete> encoded(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDelete> decoded(av_frame_alloc());
    std::unique_ptr<AVFrame, FrameDelete> converted(av_frame_alloc());
    if (!scaler || !packet || !encoded || !decoded || !converted) result = AVERROR(ENOMEM);
    converted->format = encode->pix_fmt;
    converted->width = encode->width;
    converted->height = encode->height;
    if (result >= 0) result = av_frame_get_buffer(converted.get(), 32);

    auto writePackets = [&]() {
        for (;;) {
            const int code = avcodec_receive_packet(encode.get(), encoded.get());
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) return 0;
            if (code < 0) return code;
            av_packet_rescale_ts(encoded.get(), encode->time_base, outputStream->time_base);
            encoded->stream_index = outputStream->index;
            const int write = av_interleaved_write_frame(output, encoded.get());
            av_packet_unref(encoded.get());
            if (write < 0) return write;
        }
    };
    auto sendFrame = [&](AVFrame* frame) {
        for (;;) {
            const int send = avcodec_send_frame(encode.get(), frame);
            if (send == AVERROR(EAGAIN)) {
                const int drained = writePackets();
                if (drained < 0) return drained;
                continue;
            }
            if (send < 0) return send;
            return writePackets();
        }
    };
    std::int64_t fallbackPts{};
    const std::int64_t duration = input->duration > 0 ? input->duration : 0;
    while (result >= 0 && av_read_frame(input, packet.get()) >= 0) {
        if (stopToken.stop_requested()) { result = AVERROR_EXIT; break; }
        if (packet->stream_index != streamIndex) { av_packet_unref(packet.get()); continue; }
        result = avcodec_send_packet(decode.get(), packet.get());
        av_packet_unref(packet.get());
        while (result >= 0 && (result = avcodec_receive_frame(decode.get(), decoded.get())) >= 0) {
            av_frame_make_writable(converted.get());
            sws_scale(scaler.get(), decoded->data, decoded->linesize, 0, decoded->height,
                      converted->data, converted->linesize);
            const auto sourcePts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                ? decoded->best_effort_timestamp : fallbackPts++;
            converted->pts = av_rescale_q(sourcePts, input->streams[streamIndex]->time_base, encode->time_base);
            result = sendFrame(converted.get());
            if (progress && duration > 0 && decoded->best_effort_timestamp != AV_NOPTS_VALUE) {
                const auto microseconds = av_rescale_q(decoded->best_effort_timestamp,
                    input->streams[streamIndex]->time_base, AV_TIME_BASE_Q);
                progress(std::clamp(static_cast<double>(microseconds) / duration, 0.0, 0.98));
            }
            av_frame_unref(decoded.get());
        }
        if (result == AVERROR(EAGAIN)) result = 0;
    }
    if (result >= 0) result = avcodec_send_packet(decode.get(), nullptr);
    while (result >= 0 && avcodec_receive_frame(decode.get(), decoded.get()) >= 0) {
        av_frame_make_writable(converted.get());
        sws_scale(scaler.get(), decoded->data, decoded->linesize, 0, decoded->height,
                  converted->data, converted->linesize);
        converted->pts = av_rescale_q(decoded->best_effort_timestamp,
            input->streams[streamIndex]->time_base, encode->time_base);
        result = sendFrame(converted.get());
    }
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) result = 0;
    if (result >= 0) result = sendFrame(nullptr);
    if (result >= 0) result = av_write_trailer(output);
    if (output->pb) avio_closep(&output->pb);
    avformat_free_context(output);
    avformat_close_input(&input);
    if (result < 0) {
        lastError_ = stopToken.stop_requested() ? "Animation conversion cancelled."
                                                 : "Animation conversion failed: " + ErrorText(result);
        return false;
    }
    if (progress) progress(1.0);
    return true;
}

} // namespace opencapture
