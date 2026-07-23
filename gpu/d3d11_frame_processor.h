#pragma once

#include "platform/windows_graphics_capture.h"

#include <d3d11.h>
#include <d3d11_3.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace opencapture {

enum class FramePixelFormat {
    Bgra,
    Nv12,
};

struct FrameProcessOptions {
    SIZE outputSize{};
    LONG maximumOutputHeight{};
    FramePixelFormat pixelFormat{FramePixelFormat::Bgra};
};

struct ProcessedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    SIZE contentSize{};
    std::int64_t qpcTimestamp{};
    DXGI_FORMAT textureFormat{DXGI_FORMAT_UNKNOWN};
};

class D3D11FrameProcessor final {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Reset() noexcept;

    [[nodiscard]] std::optional<ProcessedFrame> Process(
        const CapturedFrame& frame, const CaptureTarget& target,
        FrameProcessOptions options = {});
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    [[nodiscard]] RECT ResolveCrop(const CapturedFrame& frame, const CaptureTarget& target) const noexcept;
    struct OutputSlot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> plane0;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> plane1;
        UINT width{};
        UINT height{};
        DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    };

    OutputSlot* AcquireOutput(UINT width, UINT height, DXGI_FORMAT format);
    bool CreateOutput(OutputSlot& slot, UINT width, UINT height, DXGI_FORMAT format);
    void DrawPlane(ID3D11ShaderResourceView* sourceView, ID3D11RenderTargetView* targetView,
                   ID3D11PixelShader* shader, UINT width, UINT height);
    void SetError(std::string message);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Device3> device3_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> bgraShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> lumaShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> chromaShader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cropBuffer_;
    std::array<OutputSlot, 3> outputPool_{};
    std::size_t nextOutput_{};
    std::string lastError_;
};

} // namespace opencapture
