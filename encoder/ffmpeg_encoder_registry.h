#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace opencapture {

enum class VideoCodecFamily {
    H264,
    Hevc,
    Av1,
};

enum class EncoderBackend {
    Nvenc,
    Qsv,
    Amf,
    MediaFoundation,
    Software,
};

struct EncoderCapability {
    std::string name;
    std::string displayName;
    VideoCodecFamily codec{};
    EncoderBackend backend{};
    bool registered{};
    bool adapterCompatible{};
    bool deviceAvailable{};
    bool usable{};
    std::string detail;
};

class FFmpegEncoderRegistry final {
public:
    void Probe(std::uint32_t d3dAdapterVendorId);

    [[nodiscard]] const std::vector<EncoderCapability>& Capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] const EncoderCapability* SelectedH264() const noexcept;
    [[nodiscard]] std::vector<const EncoderCapability*> H264Candidates(
        std::string_view requestedName = {}) const;
    [[nodiscard]] std::string Summary() const;

private:
    std::vector<EncoderCapability> capabilities_;
    std::size_t selectedH264Index_{static_cast<std::size_t>(-1)};
};

} // namespace opencapture
