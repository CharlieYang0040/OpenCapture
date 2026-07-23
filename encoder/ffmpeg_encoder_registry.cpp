#include "encoder/ffmpeg_encoder_registry.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}

#include <array>
#include <sstream>
#include <utility>

namespace opencapture {
namespace {

constexpr std::uint32_t kNvidiaVendor = 0x10DE;
constexpr std::uint32_t kIntelVendor = 0x8086;
constexpr std::uint32_t kAmdVendor = 0x1002;
constexpr std::uint32_t kAmdLegacyVendor = 0x1022;

struct Candidate {
    const char* name;
    const char* displayName;
    VideoCodecFamily codec;
    EncoderBackend backend;
};

constexpr std::array kCandidates{
    Candidate{"h264_nvenc", "H.264 NVENC", VideoCodecFamily::H264, EncoderBackend::Nvenc},
    Candidate{"h264_qsv", "H.264 Intel QSV", VideoCodecFamily::H264, EncoderBackend::Qsv},
    Candidate{"h264_amf", "H.264 AMD AMF", VideoCodecFamily::H264, EncoderBackend::Amf},
    Candidate{"h264_mf", "H.264 Media Foundation", VideoCodecFamily::H264, EncoderBackend::MediaFoundation},
    Candidate{"libopenh264", "H.264 OpenH264", VideoCodecFamily::H264, EncoderBackend::Software},
    Candidate{"libx264", "H.264 libx264", VideoCodecFamily::H264, EncoderBackend::Software},
    Candidate{"hevc_nvenc", "HEVC NVENC", VideoCodecFamily::Hevc, EncoderBackend::Nvenc},
    Candidate{"hevc_qsv", "HEVC Intel QSV", VideoCodecFamily::Hevc, EncoderBackend::Qsv},
    Candidate{"hevc_amf", "HEVC AMD AMF", VideoCodecFamily::Hevc, EncoderBackend::Amf},
    Candidate{"hevc_mf", "HEVC Media Foundation", VideoCodecFamily::Hevc, EncoderBackend::MediaFoundation},
    Candidate{"av1_nvenc", "AV1 NVENC", VideoCodecFamily::Av1, EncoderBackend::Nvenc},
    Candidate{"av1_qsv", "AV1 Intel QSV", VideoCodecFamily::Av1, EncoderBackend::Qsv},
    Candidate{"av1_amf", "AV1 AMD AMF", VideoCodecFamily::Av1, EncoderBackend::Amf},
    Candidate{"av1_mf", "AV1 Media Foundation", VideoCodecFamily::Av1, EncoderBackend::MediaFoundation},
};

bool AdapterCompatible(EncoderBackend backend, std::uint32_t vendor) {
    switch (backend) {
    case EncoderBackend::Nvenc: return vendor == kNvidiaVendor;
    case EncoderBackend::Qsv: return vendor == kIntelVendor;
    case EncoderBackend::Amf: return vendor == kAmdVendor || vendor == kAmdLegacyVendor;
    case EncoderBackend::MediaFoundation:
    case EncoderBackend::Software: return true;
    }
    return false;
}

AVHWDeviceType DeviceType(EncoderBackend backend) {
    switch (backend) {
    case EncoderBackend::Nvenc: return AV_HWDEVICE_TYPE_CUDA;
    case EncoderBackend::Qsv: return AV_HWDEVICE_TYPE_QSV;
    case EncoderBackend::Amf: return AV_HWDEVICE_TYPE_D3D11VA;
    case EncoderBackend::MediaFoundation:
    case EncoderBackend::Software: return AV_HWDEVICE_TYPE_NONE;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

std::string AvError(int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(error, text.data(), text.size());
    return text.data();
}

} // namespace

void FFmpegEncoderRegistry::Probe(std::uint32_t d3dAdapterVendorId) {
    capabilities_.clear();
    capabilities_.reserve(kCandidates.size());
    selectedH264Index_ = static_cast<std::size_t>(-1);

    for (const auto& candidate : kCandidates) {
        EncoderCapability capability{};
        capability.name = candidate.name;
        capability.displayName = candidate.displayName;
        capability.codec = candidate.codec;
        capability.backend = candidate.backend;
        capability.registered = avcodec_find_encoder_by_name(candidate.name) != nullptr;
        capability.adapterCompatible = AdapterCompatible(candidate.backend, d3dAdapterVendorId);
        capability.deviceAvailable = candidate.backend == EncoderBackend::MediaFoundation ||
                                     candidate.backend == EncoderBackend::Software;

        if (!capability.registered) {
            capability.detail = "not compiled into this FFmpeg build";
        } else if (!capability.adapterCompatible) {
            capability.detail = "not compatible with the active D3D11 adapter";
        } else if (const AVHWDeviceType deviceType = DeviceType(candidate.backend);
                   deviceType != AV_HWDEVICE_TYPE_NONE) {
            AVBufferRef* device{};
            const int result = av_hwdevice_ctx_create(&device, deviceType, nullptr, nullptr, 0);
            capability.deviceAvailable = result >= 0;
            capability.detail = result >= 0 ? "hardware device initialized" : AvError(result);
            av_buffer_unref(&device);
        } else {
            capability.detail = "encoder registered";
        }
        capability.usable = capability.registered && capability.adapterCompatible && capability.deviceAvailable;
        capabilities_.push_back(std::move(capability));
        if (candidate.codec == VideoCodecFamily::H264 && selectedH264Index_ == static_cast<std::size_t>(-1) &&
            capabilities_.back().usable) {
            selectedH264Index_ = capabilities_.size() - 1;
        }
    }
}

const EncoderCapability* FFmpegEncoderRegistry::SelectedH264() const noexcept {
    return selectedH264Index_ < capabilities_.size() ? &capabilities_[selectedH264Index_] : nullptr;
}

std::vector<const EncoderCapability*> FFmpegEncoderRegistry::H264Candidates(
    std::string_view requestedName) const {
    std::vector<const EncoderCapability*> result;
    for (const auto& capability : capabilities_) {
        if (capability.codec != VideoCodecFamily::H264 || !capability.usable) continue;
        if (!requestedName.empty() && capability.name != requestedName) continue;
        result.push_back(&capability);
    }
    return result;
}

std::string FFmpegEncoderRegistry::Summary() const {
    std::ostringstream result;
    if (const auto* selected = SelectedH264()) result << selected->displayName;
    else result << "No usable H.264 encoder";
    result << " | ";
    bool first = true;
    for (const auto& capability : capabilities_) {
        if (!capability.registered) continue;
        if (!first) result << ", ";
        result << capability.name << (capability.usable ? " ready" : " unavailable");
        first = false;
    }
    if (first) result << "no candidate encoders compiled";
    return result.str();
}

} // namespace opencapture
