#pragma once

#include "core/screenshot_options.h"

#include <Windows.h>
#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

namespace opencapture {

struct BgraImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
};

class ScreenshotService final {
public:
    bool Capture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                 SIZE contentSize, ScreenshotDestination destination, const std::wstring& outputPath,
                 HWND clipboardOwner, ScreenshotProfile profile = ScreenshotProfile::PngLossless);

    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    bool Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                  SIZE contentSize, BgraImage& image);
    bool SavePng(const BgraImage& image, const std::wstring& outputPath);
    bool SaveJpeg(const BgraImage& image, const std::wstring& outputPath, float quality);
    bool SaveWithFfmpeg(const BgraImage& image, const std::wstring& outputPath,
                        ScreenshotProfile profile);
    bool CopyToClipboard(const BgraImage& image, HWND owner);
    void SetError(std::string message);

    std::string lastError_;
};

} // namespace opencapture
