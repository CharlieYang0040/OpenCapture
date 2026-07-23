#include "encoder/ffmpeg_gif_converter.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>

namespace opencapture {
namespace {

std::string ErrorText(int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(error, buffer.data(), buffer.size());
    return buffer.data();
}

struct InputVideo {
    AVFormatContext* format{};
    AVCodecContext* decoder{};
    int streamIndex{-1};

    ~InputVideo() {
        avcodec_free_context(&decoder);
        avformat_close_input(&format);
    }

    bool Open(const std::string& path, std::string& error) {
        int result = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
        if (result < 0) { error = "Could not open GIF source: " + ErrorText(result); return false; }
        result = avformat_find_stream_info(format, nullptr);
        if (result < 0) { error = "Could not inspect GIF source: " + ErrorText(result); return false; }
        streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIndex < 0) { error = "GIF source has no video stream."; return false; }
        const AVCodec* codec = avcodec_find_decoder(format->streams[streamIndex]->codecpar->codec_id);
        decoder = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!decoder) { error = "Could not create the GIF source decoder."; return false; }
        avcodec_parameters_to_context(decoder, format->streams[streamIndex]->codecpar);
        result = avcodec_open2(decoder, codec, nullptr);
        if (result < 0) { error = "Could not open the GIF source decoder: " + ErrorText(result); return false; }
        return true;
    }

    bool Rewind(std::string& error) {
        const int result = av_seek_frame(format, streamIndex, 0, AVSEEK_FLAG_BACKWARD);
        if (result < 0) { error = "Could not rewind the GIF source: " + ErrorText(result); return false; }
        avcodec_flush_buffers(decoder);
        return true;
    }
};

struct PacketDeleter {
    void operator()(AVPacket* value) const noexcept { av_packet_free(&value); }
};
struct FrameDeleter {
    void operator()(AVFrame* value) const noexcept { av_frame_free(&value); }
};
struct GraphDeleter {
    void operator()(AVFilterGraph* value) const noexcept { avfilter_graph_free(&value); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* value) const noexcept { avcodec_free_context(&value); }
};

bool FeedDecoded(InputVideo& input, AVFilterContext* source, AVFilterContext* sink,
                 const std::function<bool(AVFrame*)>& consume, std::string& error) {
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> decoded(av_frame_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> filtered(av_frame_alloc());
    if (!packet || !decoded || !filtered) { error = "Could not allocate GIF conversion frames."; return false; }

    auto drain = [&]() {
        for (;;) {
            const int result = av_buffersink_get_frame(sink, filtered.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
            if (result < 0) { error = "GIF filter failed: " + ErrorText(result); return false; }
            if (!consume(filtered.get())) return false;
            av_frame_unref(filtered.get());
        }
    };
    auto decode = [&](AVPacket* value) {
        int result = avcodec_send_packet(input.decoder, value);
        if (result < 0 && result != AVERROR_EOF) { error = "GIF decode failed: " + ErrorText(result); return false; }
        while ((result = avcodec_receive_frame(input.decoder, decoded.get())) >= 0) {
            decoded->pts = decoded->best_effort_timestamp;
            result = av_buffersrc_add_frame_flags(source, decoded.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
            av_frame_unref(decoded.get());
            if (result < 0) { error = "Could not feed the GIF filter: " + ErrorText(result); return false; }
            if (!drain()) return false;
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            error = "GIF decode failed: " + ErrorText(result); return false;
        }
        return true;
    };

    int result{};
    while ((result = av_read_frame(input.format, packet.get())) >= 0) {
        if (packet->stream_index == input.streamIndex && !decode(packet.get())) return false;
        av_packet_unref(packet.get());
    }
    if (result != AVERROR_EOF || !decode(nullptr)) {
        if (error.empty()) error = "Could not finish reading the GIF source.";
        return false;
    }
    result = av_buffersrc_add_frame_flags(source, nullptr, 0);
    if (result < 0) { error = "Could not finish the GIF filter: " + ErrorText(result); return false; }
    return drain();
}

AVFilterContext* AddBuffer(AVFilterGraph* graph, const char* name, int width, int height,
                           AVPixelFormat format, AVRational timeBase, AVRational aspect,
                           std::string& error) {
    std::ostringstream arguments;
    arguments << "video_size=" << width << 'x' << height
              << ":pix_fmt=" << static_cast<int>(format)
              << ":time_base=" << timeBase.num << '/' << timeBase.den
              << ":pixel_aspect=" << aspect.num << '/' << aspect.den;
    AVFilterContext* context{};
    const int result = avfilter_graph_create_filter(
        &context, avfilter_get_by_name("buffer"), name, arguments.str().c_str(), nullptr, graph);
    if (result < 0) error = "Could not create GIF buffer filter: " + ErrorText(result);
    return context;
}

} // namespace

bool FFmpegGifConverter::Convert(const std::string& inputPath, const std::string& outputPath,
                                 GifConversionOptions options) {
    lastError_.clear();
    InputVideo input;
    if (!input.Open(inputPath, lastError_)) return false;
    const auto* inputStream = input.format->streams[input.streamIndex];
    const AVRational timeBase = inputStream->time_base;
    const AVRational aspect = input.decoder->sample_aspect_ratio.num
        ? input.decoder->sample_aspect_ratio : AVRational{1, 1};

    std::unique_ptr<AVFilterGraph, GraphDeleter> paletteGraph(avfilter_graph_alloc());
    if (!paletteGraph) { SetError("Could not allocate the GIF palette graph."); return false; }
    AVFilterContext* paletteSource = AddBuffer(paletteGraph.get(), "palette-input",
        input.decoder->width, input.decoder->height, input.decoder->pix_fmt, timeBase, aspect, lastError_);
    AVFilterContext* paletteGenerator{};
    AVFilterContext* paletteSink{};
    std::ostringstream paletteOptions;
    paletteOptions << "max_colors=" << std::clamp(options.colors, 32, 256) << ":stats_mode=diff";
    int result = paletteSource ? avfilter_graph_create_filter(
        &paletteGenerator, avfilter_get_by_name("palettegen"), "palettegen",
        paletteOptions.str().c_str(), nullptr, paletteGraph.get()) : AVERROR(EINVAL);
    if (result >= 0) result = avfilter_graph_create_filter(
        &paletteSink, avfilter_get_by_name("buffersink"), "palette-sink", nullptr, nullptr, paletteGraph.get());
    if (result >= 0) result = avfilter_link(paletteSource, 0, paletteGenerator, 0);
    if (result >= 0) result = avfilter_link(paletteGenerator, 0, paletteSink, 0);
    if (result >= 0) result = avfilter_graph_config(paletteGraph.get(), nullptr);
    if (result < 0) { SetError(lastError_.empty() ? "Could not configure GIF palette: " + ErrorText(result) : lastError_); return false; }

    std::unique_ptr<AVFrame, FrameDeleter> palette(av_frame_alloc());
    if (!palette) { SetError("Could not allocate the GIF palette."); return false; }
    if (!FeedDecoded(input, paletteSource, paletteSink, [&](AVFrame* frame) {
            av_frame_unref(palette.get());
            return av_frame_ref(palette.get(), frame) >= 0;
        }, lastError_) || !palette->data[0]) {
        if (lastError_.empty()) lastError_ = "GIF palette generation produced no palette.";
        return false;
    }
    if (!input.Rewind(lastError_)) return false;

    std::unique_ptr<AVFilterGraph, GraphDeleter> useGraph(avfilter_graph_alloc());
    if (!useGraph) { SetError("Could not allocate the GIF color graph."); return false; }
    AVFilterContext* videoSource = AddBuffer(useGraph.get(), "video-input",
        input.decoder->width, input.decoder->height, input.decoder->pix_fmt, timeBase, aspect, lastError_);
    AVFilterContext* paletteInput = AddBuffer(useGraph.get(), "palette-input",
        palette->width, palette->height, static_cast<AVPixelFormat>(palette->format), AVRational{1, 1},
        AVRational{1, 1}, lastError_);
    AVFilterContext* paletteUse{};
    AVFilterContext* sink{};
    result = videoSource && paletteInput ? avfilter_graph_create_filter(
        &paletteUse, avfilter_get_by_name("paletteuse"), "paletteuse",
        "dither=sierra2_4a:diff_mode=rectangle", nullptr, useGraph.get()) : AVERROR(EINVAL);
    if (result >= 0) result = avfilter_graph_create_filter(
        &sink, avfilter_get_by_name("buffersink"), "gif-sink", nullptr, nullptr, useGraph.get());
    if (result >= 0) result = avfilter_link(videoSource, 0, paletteUse, 0);
    if (result >= 0) result = avfilter_link(paletteInput, 0, paletteUse, 1);
    if (result >= 0) result = avfilter_link(paletteUse, 0, sink, 0);
    if (result >= 0) result = avfilter_graph_config(useGraph.get(), nullptr);
    if (result < 0) { SetError(lastError_.empty() ? "Could not configure GIF colors: " + ErrorText(result) : lastError_); return false; }
    result = av_buffersrc_add_frame_flags(paletteInput, palette.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
    if (result >= 0) result = av_buffersrc_add_frame_flags(paletteInput, nullptr, 0);
    if (result < 0) { SetError("Could not feed the generated GIF palette: " + ErrorText(result)); return false; }

    AVFormatContext* rawOutput{};
    result = avformat_alloc_output_context2(&rawOutput, nullptr, "gif", outputPath.c_str());
    std::unique_ptr<AVFormatContext, void(*)(AVFormatContext*)> output(rawOutput, [](AVFormatContext* value) {
        if (!value) return;
        if (!(value->oformat->flags & AVFMT_NOFILE) && value->pb) avio_closep(&value->pb);
        avformat_free_context(value);
    });
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_GIF);
    std::unique_ptr<AVCodecContext, CodecDeleter> encoderContext(
        encoder ? avcodec_alloc_context3(encoder) : nullptr);
    AVStream* outputStream = output ? avformat_new_stream(output.get(), nullptr) : nullptr;
    if (!output || !encoderContext || !outputStream) { SetError("Could not create the GIF encoder."); return false; }
    encoderContext->width = input.decoder->width;
    encoderContext->height = input.decoder->height;
    encoderContext->pix_fmt = AV_PIX_FMT_PAL8;
    encoderContext->time_base = timeBase;
    if (output->oformat->flags & AVFMT_GLOBALHEADER) encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    result = avcodec_open2(encoderContext.get(), encoder, nullptr);
    if (result >= 0) result = avcodec_parameters_from_context(outputStream->codecpar, encoderContext.get());
    outputStream->time_base = encoderContext->time_base;
    if (result >= 0 && !(output->oformat->flags & AVFMT_NOFILE)) result = avio_open(&output->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
    if (result >= 0) result = avformat_write_header(output.get(), nullptr);
    if (result < 0) { SetError("Could not open the GIF output: " + ErrorText(result)); return false; }

    std::unique_ptr<AVPacket, PacketDeleter> encoded(av_packet_alloc());
    auto encode = [&](AVFrame* frame) {
        int code = avcodec_send_frame(encoderContext.get(), frame);
        while (code >= 0 && (code = avcodec_receive_packet(encoderContext.get(), encoded.get())) >= 0) {
            av_packet_rescale_ts(encoded.get(), encoderContext->time_base, outputStream->time_base);
            encoded->stream_index = outputStream->index;
            code = av_interleaved_write_frame(output.get(), encoded.get());
            av_packet_unref(encoded.get());
        }
        if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) return true;
        lastError_ = "GIF encoding failed: " + ErrorText(code);
        return false;
    };
    if (!encoded || !FeedDecoded(input, videoSource, sink, encode, lastError_) ||
        !encode(nullptr) || av_write_trailer(output.get()) < 0) {
        if (lastError_.empty()) lastError_ = "Could not finalize the GIF.";
        return false;
    }
    return true;
}

void FFmpegGifConverter::SetError(std::string message) { lastError_ = std::move(message); }

} // namespace opencapture
