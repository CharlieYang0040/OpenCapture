#pragma once

#include <string>

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
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
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
    std::string lastError_;
};

} // namespace opencapture
