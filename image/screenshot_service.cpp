#include "image/screenshot_service.h"

#include <wincodec.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <filesystem>
#include <limits>
#include <array>
#include <algorithm>

namespace opencapture {
namespace {

using Microsoft::WRL::ComPtr;

bool IncludesFile(ScreenshotDestination destination) {
    return destination == ScreenshotDestination::File ||
           destination == ScreenshotDestination::FileAndClipboard;
}

bool IncludesClipboard(ScreenshotDestination destination) {
    return destination == ScreenshotDestination::Clipboard ||
           destination == ScreenshotDestination::FileAndClipboard;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    if (!result.empty()) result.pop_back();
    return result;
}

} // namespace

bool ScreenshotService::Capture(ID3D11Device* device, ID3D11DeviceContext* context,
                                ID3D11Texture2D* texture, SIZE contentSize,
                                ScreenshotDestination destination, const std::wstring& outputPath,
                                HWND clipboardOwner, ScreenshotProfile profile) {
    lastError_.clear();
    if (!device || !context || !texture || contentSize.cx <= 0 || contentSize.cy <= 0) {
        SetError("A valid BGRA D3D11 texture is required for a screenshot.");
        return false;
    }
    if (IncludesFile(destination) && outputPath.empty()) {
        SetError("A screenshot output path is required.");
        return false;
    }

    BgraImage image;
    if (!Download(device, context, texture, contentSize, image)) return false;
    if (IncludesFile(destination)) {
        const bool saved = profile == ScreenshotProfile::PngLossless ? SavePng(image, outputPath) :
                           profile == ScreenshotProfile::JpegCompatible ? SaveJpeg(image, outputPath, 0.90F) :
                           SaveWithFfmpeg(image, outputPath, profile);
        if (!saved) return false;
    }
    if (IncludesClipboard(destination) && !CopyToClipboard(image, clipboardOwner)) return false;
    return true;
}

bool ScreenshotService::Download(ID3D11Device* device, ID3D11DeviceContext* context,
                                 ID3D11Texture2D* texture, SIZE contentSize, BgraImage& image) {
    D3D11_TEXTURE2D_DESC sourceDescription{};
    texture->GetDesc(&sourceDescription);
    if (sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        SetError("Screenshot readback requires a BGRA8 texture.");
        return false;
    }
    if (contentSize.cx > static_cast<LONG>(sourceDescription.Width) ||
        contentSize.cy > static_cast<LONG>(sourceDescription.Height)) {
        SetError("Screenshot dimensions exceed the source texture.");
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDescription = sourceDescription;
    stagingDescription.Width = static_cast<UINT>(contentSize.cx);
    stagingDescription.Height = static_cast<UINT>(contentSize.cy);
    stagingDescription.MipLevels = 1;
    stagingDescription.ArraySize = 1;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&stagingDescription, nullptr, &staging))) {
        SetError("Could not create the screenshot staging texture.");
        return false;
    }

    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        SetError("Could not map the screenshot staging texture.");
        return false;
    }

    const auto rowBytes = static_cast<std::size_t>(contentSize.cx) * 4;
    if (rowBytes > mapped.RowPitch ||
        static_cast<std::size_t>(contentSize.cy) > std::numeric_limits<std::size_t>::max() / rowBytes) {
        context->Unmap(staging.Get(), 0);
        SetError("The screenshot dimensions are too large.");
        return false;
    }
    image.width = contentSize.cx;
    image.height = contentSize.cy;
    image.pixels.resize(rowBytes * static_cast<std::size_t>(contentSize.cy));
    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    for (LONG row = 0; row < contentSize.cy; ++row) {
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(row) * rowBytes,
                    source + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
    }
    context->Unmap(staging.Get(), 0);
    return true;
}

bool ScreenshotService::SavePng(const BgraImage& image, const std::wstring& outputPath) {
    if (image.pixels.size() > std::numeric_limits<UINT>::max()) {
        SetError("The screenshot is too large for the PNG encoder.");
        return false;
    }
    std::error_code filesystemError;
    const auto parent = std::filesystem::path(outputPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) {
        SetError("Could not create the screenshot output folder.");
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        SetError("Could not initialize Windows Imaging Component.");
        return false;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &properties)) ||
        FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)))) {
        SetError("Could not initialize the PNG encoder.");
        return false;
    }
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&pixelFormat)) || pixelFormat != GUID_WICPixelFormat32bppBGRA) {
        SetError("The PNG encoder does not accept BGRA pixels.");
        return false;
    }
    const auto stride = static_cast<UINT>(image.width * 4);
    if (FAILED(frame->WritePixels(static_cast<UINT>(image.height), stride,
                                  static_cast<UINT>(image.pixels.size()),
                                  const_cast<BYTE*>(image.pixels.data()))) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        SetError("Could not write the PNG screenshot.");
        return false;
    }
    return true;
}

bool ScreenshotService::SaveJpeg(const BgraImage& image, const std::wstring& outputPath, float quality) {
    std::error_code filesystemError;
    const auto parent = std::filesystem::path(outputPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) { SetError("Could not create the screenshot output folder."); return false; }
    const auto stride = static_cast<UINT>(image.width * 3);
    if (static_cast<std::size_t>(stride) * image.height > std::numeric_limits<UINT>::max()) {
        SetError("The screenshot is too large for the JPEG encoder."); return false;
    }
    std::vector<BYTE> bgr(static_cast<std::size_t>(stride) * image.height);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto source = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const auto target = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 3;
            bgr[target] = image.pixels[source]; bgr[target + 1] = image.pixels[source + 1];
            bgr[target + 2] = image.pixels[source + 2];
        }
    }
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &properties))) {
        SetError("Could not initialize the JPEG encoder."); return false;
    }
    PROPBAG2 option{}; option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value{}; VariantInit(&value); value.vt = VT_R4; value.fltVal = std::clamp(quality, 0.0F, 1.0F);
    properties->Write(1, &option, &value); VariantClear(&value);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height))) ||
        FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat24bppBGR ||
        FAILED(frame->WritePixels(static_cast<UINT>(image.height), stride, static_cast<UINT>(bgr.size()), bgr.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        SetError("Could not write the JPEG screenshot."); return false;
    }
    return true;
}

bool ScreenshotService::SaveWithFfmpeg(const BgraImage& image, const std::wstring& outputPath,
                                       ScreenshotProfile profile) {
    const bool webp = profile == ScreenshotProfile::WebpDocument ||
                      profile == ScreenshotProfile::WebpBalanced;
    const char* muxerName = webp ? "webp" : "avif";
    const char* encoderName = webp ? "libwebp" : "libaom-av1";
    const auto finalPath = std::filesystem::path(outputPath);
    const auto temporary = finalPath.parent_path() /
        (finalPath.stem().wstring() + L".part" + finalPath.extension().wstring());
    std::error_code fileError;
    if (!finalPath.parent_path().empty()) std::filesystem::create_directories(finalPath.parent_path(), fileError);
    std::filesystem::remove(temporary, fileError);
    const auto temporaryUtf8 = Utf8(temporary.wstring());
    AVFormatContext* output{};
    int result = avformat_alloc_output_context2(&output, nullptr, muxerName, temporaryUtf8.c_str());
    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName);
    AVCodecContext* encoder = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVStream* stream = output ? avformat_new_stream(output, nullptr) : nullptr;
    SwsContext* scaler{}; AVFrame* frame{}; AVPacket* packet{};
    auto cleanup = [&] {
        av_packet_free(&packet); av_frame_free(&frame); sws_freeContext(scaler);
        avcodec_free_context(&encoder);
        if (output) { if (output->pb) avio_closep(&output->pb); avformat_free_context(output); }
    };
    if (result < 0 || !output || !encoder || !stream) {
        cleanup(); SetError(std::string("Required screenshot encoder is unavailable: ") + encoderName); return false;
    }
    encoder->width = image.width; encoder->height = image.height; encoder->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder->time_base = {1, 1}; encoder->framerate = {1, 1};
    if (output->oformat->flags & AVFMT_GLOBALHEADER) encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (webp) {
        av_opt_set_int(encoder->priv_data, "lossless",
                       profile == ScreenshotProfile::WebpDocument ? 1 : 0, 0);
        av_opt_set_double(encoder->priv_data, "quality",
                          profile == ScreenshotProfile::WebpDocument ? 90.0 : 82.0, 0);
    } else {
        av_opt_set_int(encoder->priv_data, "crf", 32, 0);
        av_opt_set_int(encoder->priv_data, "cpu-used", 6, 0);
        av_opt_set_int(encoder->priv_data, "still-picture", 1, 0);
        av_opt_set_int(encoder->priv_data, "lag-in-frames", 0, 0);
    }
    result = avcodec_open2(encoder, codec, nullptr);
    if (result >= 0) result = avcodec_parameters_from_context(stream->codecpar, encoder);
    stream->time_base = encoder->time_base;
    if (result >= 0 && !(output->oformat->flags & AVFMT_NOFILE))
        result = avio_open(&output->pb, temporaryUtf8.c_str(), AVIO_FLAG_WRITE);
    if (result >= 0) result = avformat_write_header(output, nullptr);
    frame = av_frame_alloc(); packet = av_packet_alloc();
    if (result >= 0 && (!frame || !packet)) result = AVERROR(ENOMEM);
    if (result >= 0) {
        frame->format = encoder->pix_fmt; frame->width = image.width; frame->height = image.height;
        result = av_frame_get_buffer(frame, 32);
    }
    scaler = sws_getContext(image.width, image.height, AV_PIX_FMT_BGRA, image.width, image.height,
                            encoder->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (result >= 0 && !scaler) result = AVERROR(ENOMEM);
    if (result >= 0) {
        const std::uint8_t* source[]{image.pixels.data()};
        const int strides[]{image.width * 4};
        sws_scale(scaler, source, strides, 0, image.height, frame->data, frame->linesize);
        frame->pts = 0; result = avcodec_send_frame(encoder, frame);
    }
    if (result >= 0) result = avcodec_send_frame(encoder, nullptr);
    if (result >= 0) result = avcodec_receive_packet(encoder, packet);
    if (result >= 0) { packet->stream_index = stream->index; result = av_interleaved_write_frame(output, packet); }
    if (result >= 0) result = av_write_trailer(output);
    cleanup();
    if (result < 0) {
        std::filesystem::remove(temporary, fileError);
        SetError("Could not encode the modern screenshot format."); return false;
    }
    if (!MoveFileExW(temporary.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, fileError);
        SetError("Could not finalize the screenshot file name."); return false;
    }
    return true;
}

bool ScreenshotService::CopyToClipboard(const BgraImage& image, HWND owner) {
    const auto imageBytes = image.pixels.size();
    if (imageBytes > std::numeric_limits<DWORD>::max() ||
        imageBytes > std::numeric_limits<std::size_t>::max() - sizeof(BITMAPV5HEADER)) {
        SetError("The screenshot is too large for the clipboard.");
        return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + imageBytes);
    if (!memory) {
        SetError("Could not allocate clipboard memory.");
        return false;
    }
    auto* bytes = static_cast<std::uint8_t*>(GlobalLock(memory));
    if (!bytes) {
        GlobalFree(memory);
        SetError("Could not lock clipboard memory.");
        return false;
    }
    auto* header = reinterpret_cast<BITMAPV5HEADER*>(bytes);
    *header = {};
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(imageBytes);
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    std::memcpy(bytes + sizeof(BITMAPV5HEADER), image.pixels.data(), imageBytes);
    GlobalUnlock(memory);

    bool clipboardOpen{};
    for (int attempt = 0; attempt < 5 && !clipboardOpen; ++attempt) {
        clipboardOpen = OpenClipboard(owner) != FALSE;
        if (!clipboardOpen) Sleep(10);
    }
    if (!clipboardOpen) {
        GlobalFree(memory);
        SetError("Could not open the Windows clipboard.");
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIBV5, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        SetError("Could not place the screenshot on the clipboard.");
        return false;
    }
    CloseClipboard();
    return true;
}

void ScreenshotService::SetError(std::string message) { lastError_ = std::move(message); }

} // namespace opencapture
