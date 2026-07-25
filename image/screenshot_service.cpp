#include "image/screenshot_service.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <filesystem>
#include <limits>

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

} // namespace

bool ScreenshotService::Capture(ID3D11Device* device, ID3D11DeviceContext* context,
                                ID3D11Texture2D* texture, SIZE contentSize,
                                ScreenshotDestination destination, const std::wstring& outputPath,
                                HWND clipboardOwner) {
    lastError_.clear();
    if (!device || !context || !texture || contentSize.cx <= 0 || contentSize.cy <= 0) {
        SetError("A valid BGRA D3D11 texture is required for a screenshot.");
        return false;
    }
    if (IncludesFile(destination) && outputPath.empty()) {
        SetError("A PNG output path is required.");
        return false;
    }

    BgraImage image;
    if (!Download(device, context, texture, contentSize, image)) return false;
    if (IncludesFile(destination) && !SavePng(image, outputPath)) return false;
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
