#include "encoder/ffmpeg_d3d11_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
}

#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace opencapture {
namespace {

void ReleaseTexture(void*, std::uint8_t* data) {
    if (data) reinterpret_cast<ID3D11Texture2D*>(data)->Release();
}

bool UsesSoftwareFrames(std::string_view encoderName) {
    return encoderName == "libopenh264" || encoderName == "libx264";
}

} // namespace

FFmpegD3D11Encoder::~FFmpegD3D11Encoder() { Close(); }

bool FFmpegD3D11Encoder::Open(std::string encoderName, ID3D11Device* device,
                              ID3D11Texture2D* prototypeTexture, SIZE frameSize,
                              int framesPerSecond, std::int64_t bitRate) {
    return Open(std::move(encoderName), device, prototypeTexture, frameSize,
                EncoderOpenOptions{framesPerSecond, bitRate, EncoderEfficiencyMode::Realtime});
}

bool FFmpegD3D11Encoder::Open(std::string encoderName, ID3D11Device* device,
                              ID3D11Texture2D* prototypeTexture, SIZE frameSize,
                              const EncoderOpenOptions& options) {
    Close();
    const int framesPerSecond = options.framesPerSecond;
    const std::int64_t bitRate = options.bitRate;
    if (!device || !prototypeTexture || frameSize.cx <= 0 || frameSize.cy <= 0 || framesPerSecond <= 0) {
        lastError_ = "Invalid D3D11 encoder configuration.";
        return false;
    }
    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName.c_str());
    if (!codec) {
        lastError_ = "FFmpeg encoder is not registered: " + encoderName;
        return false;
    }

    int result{};
    softwareInput_ = UsesSoftwareFrames(encoderName);
    if (softwareInput_) {
        D3D11_TEXTURE2D_DESC stagingDescription{};
        prototypeTexture->GetDesc(&stagingDescription);
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&stagingDescription, nullptr, &stagingTexture_))) {
            lastError_ = "Could not create the NV12 staging texture for the software encoder fallback.";
            Close();
            return false;
        }
        device->GetImmediateContext(&d3dContext_);
    } else {
        deviceContext_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!deviceContext_) {
            lastError_ = "Could not allocate the FFmpeg D3D11 hardware device context.";
            return false;
        }
        auto* hardwareDevice = reinterpret_cast<AVHWDeviceContext*>(deviceContext_->data);
        auto* d3dDevice = reinterpret_cast<AVD3D11VADeviceContext*>(hardwareDevice->hwctx);
        device->AddRef();
        d3dDevice->device = device;
        result = av_hwdevice_ctx_init(deviceContext_);
        if (result < 0) {
            SetError("Could not initialize the FFmpeg D3D11 hardware device", result);
            Close();
            return false;
        }

        framesContext_ = av_hwframe_ctx_alloc(deviceContext_);
        if (!framesContext_) {
            lastError_ = "Could not allocate the FFmpeg D3D11 hardware frames context.";
            Close();
            return false;
        }
        auto* hardwareFrames = reinterpret_cast<AVHWFramesContext*>(framesContext_->data);
        hardwareFrames->format = AV_PIX_FMT_D3D11;
        hardwareFrames->sw_format = AV_PIX_FMT_NV12;
        hardwareFrames->width = frameSize.cx;
        hardwareFrames->height = frameSize.cy;
        hardwareFrames->initial_pool_size = 1;
        auto* d3dFrames = reinterpret_cast<AVD3D11VAFramesContext*>(hardwareFrames->hwctx);
        prototypeTexture->AddRef();
        d3dFrames->texture = prototypeTexture;
        result = av_hwframe_ctx_init(framesContext_);
        if (result < 0) {
            SetError("Could not initialize the FFmpeg D3D11 hardware frames context", result);
            Close();
            return false;
        }
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (!codecContext_) {
        lastError_ = "Could not allocate the FFmpeg encoder context.";
        Close();
        return false;
    }
    codecContext_->width = frameSize.cx;
    codecContext_->height = frameSize.cy;
    codecContext_->time_base = AVRational{1, framesPerSecond};
    codecContext_->framerate = AVRational{framesPerSecond, 1};
    codecContext_->pix_fmt = softwareInput_ ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_D3D11;
    codecContext_->bit_rate = bitRate;
    codecContext_->gop_size = framesPerSecond * 2;
    codecContext_->max_b_frames = options.efficiency == EncoderEfficiencyMode::Realtime ? 0 :
                                 options.efficiency == EncoderEfficiencyMode::Balanced ? 2 : 3;
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (options.efficiency == EncoderEfficiencyMode::Realtime) {
        codecContext_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    } else {
        codecContext_->rc_max_rate = bitRate + bitRate / 2;
        codecContext_->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(
            codecContext_->rc_max_rate * 2, std::numeric_limits<int>::max()));
    }
    codecContext_->color_range = AVCOL_RANGE_MPEG;
    codecContext_->colorspace = AVCOL_SPC_BT709;
    codecContext_->color_primaries = AVCOL_PRI_BT709;
    codecContext_->color_trc = AVCOL_TRC_BT709;
    if (!softwareInput_) codecContext_->hw_frames_ctx = av_buffer_ref(framesContext_);
    if (encoderName.find("nvenc") != std::string::npos) {
        const bool realtime = options.efficiency == EncoderEfficiencyMode::Realtime;
        const bool efficient = options.efficiency == EncoderEfficiencyMode::Efficient;
        av_opt_set(codecContext_->priv_data, "preset", realtime ? "p1" : efficient ? "p5" : "p4", 0);
        av_opt_set(codecContext_->priv_data, "tune", realtime ? "ull" : "hq", 0);
        if (!realtime) {
            av_opt_set(codecContext_->priv_data, "rc", "vbr", 0);
            av_opt_set(codecContext_->priv_data, "spatial-aq", "1", 0);
            if (efficient) {
                av_opt_set(codecContext_->priv_data, "temporal-aq", "1", 0);
                av_opt_set(codecContext_->priv_data, "rc-lookahead", "20", 0);
            }
        }
    }
    result = avcodec_open2(codecContext_, codec, nullptr);
    if (result < 0) {
        SetError("Could not open the FFmpeg D3D11 encoder", result);
        Close();
        return false;
    }
    packetCount_ = 0;
    lastError_.clear();
    return true;
}

bool FFmpegD3D11Encoder::Send(const ProcessedFrame& frame, std::int64_t presentationTimestamp) {
    if (!codecContext_ || !frame.texture || frame.textureFormat != DXGI_FORMAT_NV12) {
        lastError_ = "The encoder requires an open session and an NV12 D3D11 texture.";
        return false;
    }
    AVFrame* avFrame = av_frame_alloc();
    if (!avFrame) {
        lastError_ = "Could not allocate an FFmpeg video frame.";
        return false;
    }
    avFrame->width = frame.contentSize.cx;
    avFrame->height = frame.contentSize.cy;
    if (softwareInput_) {
        avFrame->format = AV_PIX_FMT_YUV420P;
        if (av_frame_get_buffer(avFrame, 32) < 0 || av_frame_make_writable(avFrame) < 0) {
            av_frame_free(&avFrame);
            lastError_ = "Could not allocate a writable NV12 software frame.";
            return false;
        }
        d3dContext_->CopyResource(stagingTexture_.Get(), frame.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(d3dContext_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            av_frame_free(&avFrame);
            lastError_ = "Could not map the NV12 staging texture for the software encoder fallback.";
            return false;
        }
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (int row = 0; row < frame.contentSize.cy; ++row) {
            std::memcpy(avFrame->data[0] + static_cast<std::ptrdiff_t>(row) * avFrame->linesize[0],
                        source + static_cast<std::ptrdiff_t>(row) * mapped.RowPitch,
                        static_cast<std::size_t>(frame.contentSize.cx));
        }
        const auto* sourceUv = source + static_cast<std::ptrdiff_t>(mapped.RowPitch) * frame.contentSize.cy;
        for (int row = 0; row < frame.contentSize.cy / 2; ++row) {
            const auto* sourceRow = sourceUv + static_cast<std::ptrdiff_t>(row) * mapped.RowPitch;
            auto* destinationU = avFrame->data[1] + static_cast<std::ptrdiff_t>(row) * avFrame->linesize[1];
            auto* destinationV = avFrame->data[2] + static_cast<std::ptrdiff_t>(row) * avFrame->linesize[2];
            for (int column = 0; column < frame.contentSize.cx / 2; ++column) {
                destinationU[column] = sourceRow[column * 2];
                destinationV[column] = sourceRow[column * 2 + 1];
            }
        }
        d3dContext_->Unmap(stagingTexture_.Get(), 0);
    } else {
        frame.texture->AddRef();
        avFrame->buf[0] = av_buffer_create(reinterpret_cast<std::uint8_t*>(frame.texture.Get()), sizeof(void*),
                                           ReleaseTexture, nullptr, AV_BUFFER_FLAG_READONLY);
        if (!avFrame->buf[0]) {
            frame.texture->Release();
            av_frame_free(&avFrame);
            lastError_ = "Could not retain the D3D11 texture for FFmpeg.";
            return false;
        }
        avFrame->format = AV_PIX_FMT_D3D11;
        avFrame->data[0] = reinterpret_cast<std::uint8_t*>(frame.texture.Get());
        avFrame->data[1] = nullptr;
        avFrame->hw_frames_ctx = av_buffer_ref(framesContext_);
    }
    avFrame->pts = presentationTimestamp;
    const int result = avcodec_send_frame(codecContext_, avFrame);
    av_frame_free(&avFrame);
    if (result < 0) {
        SetError("Could not submit the D3D11 texture to FFmpeg", result);
        return false;
    }
    return ReceivePackets(false);
}

bool FFmpegD3D11Encoder::Flush() {
    if (!codecContext_) return true;
    const int result = avcodec_send_frame(codecContext_, nullptr);
    if (result < 0 && result != AVERROR_EOF) {
        SetError("Could not flush the FFmpeg encoder", result);
        return false;
    }
    return ReceivePackets(true);
}

bool FFmpegD3D11Encoder::ReceivePackets(bool flushing) {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        lastError_ = "Could not allocate an FFmpeg packet.";
        return false;
    }
    while (true) {
        const int result = avcodec_receive_packet(codecContext_, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        if (result < 0) {
            SetError(flushing ? "Could not receive a flushed packet" : "Could not receive an encoded packet", result);
            av_packet_free(&packet);
            return false;
        }
        ++packetCount_;
        if (packetCallback_ && !packetCallback_(packet)) {
            lastError_ = "The encoded packet sink rejected a video packet.";
            av_packet_free(&packet);
            return false;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return true;
}

void FFmpegD3D11Encoder::Close() noexcept {
    if (codecContext_) avcodec_free_context(&codecContext_);
    av_buffer_unref(&framesContext_);
    av_buffer_unref(&deviceContext_);
    stagingTexture_.Reset();
    d3dContext_.Reset();
    softwareInput_ = false;
    packetCount_ = 0;
    packetCallback_ = {};
}

void FFmpegD3D11Encoder::SetError(std::string operation, int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    lastError_ = std::move(operation) + ": " + text.data();
}

} // namespace opencapture
