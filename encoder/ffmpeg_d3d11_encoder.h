#pragma once

#include "gpu/d3d11_frame_processor.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

struct AVBufferRef;
struct AVCodecContext;
struct AVPacket;

namespace opencapture {

class FFmpegD3D11Encoder final {
public:
    using PacketCallback = std::function<bool(AVPacket*)>;
    FFmpegD3D11Encoder() = default;
    ~FFmpegD3D11Encoder();

    FFmpegD3D11Encoder(const FFmpegD3D11Encoder&) = delete;
    FFmpegD3D11Encoder& operator=(const FFmpegD3D11Encoder&) = delete;

    bool Open(std::string encoderName, ID3D11Device* device, ID3D11Texture2D* prototypeTexture, SIZE frameSize,
              int framesPerSecond, std::int64_t bitRate);
    bool Send(const ProcessedFrame& frame, std::int64_t presentationTimestamp);
    bool Flush();
    void SetPacketCallback(PacketCallback callback) { packetCallback_ = std::move(callback); }
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return codecContext_ != nullptr; }
    [[nodiscard]] std::uint64_t PacketCount() const noexcept { return packetCount_; }
    [[nodiscard]] const AVCodecContext* CodecContext() const noexcept { return codecContext_; }
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    bool ReceivePackets(bool flushing);
    void SetError(std::string operation, int error);

    AVCodecContext* codecContext_{};
    AVBufferRef* deviceContext_{};
    AVBufferRef* framesContext_{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture_;
    bool softwareInput_{};
    std::uint64_t packetCount_{};
    PacketCallback packetCallback_;
    std::string lastError_;
};

} // namespace opencapture
