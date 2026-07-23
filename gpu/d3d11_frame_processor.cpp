#include "gpu/d3d11_frame_processor.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <utility>

namespace opencapture {
namespace {

constexpr char kShaderSource[] = R"(
cbuffer CropConstants : register(b0) {
    float4 cropUv;
};

Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID) {
    VertexOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 BgraMain(VertexOutput input) : SV_Target {
    const float2 uv = cropUv.xy + input.uv * cropUv.zw;
    return sourceTexture.SampleLevel(sourceSampler, uv, 0.0);
}

float LumaMain(VertexOutput input) : SV_Target {
    const float2 uv = cropUv.xy + input.uv * cropUv.zw;
    const float3 rgb = sourceTexture.SampleLevel(sourceSampler, uv, 0.0).rgb;
    return dot(rgb, float3(0.182586, 0.614231, 0.062007)) + 0.062745;
}

float2 ChromaMain(VertexOutput input) : SV_Target {
    const float2 uv = cropUv.xy + input.uv * cropUv.zw;
    const float3 rgb = sourceTexture.SampleLevel(sourceSampler, uv, 0.0).rgb;
    const float cb = dot(rgb, float3(-0.100644, -0.338572, 0.439216)) + 0.501961;
    const float cr = dot(rgb, float3(0.439216, -0.398942, -0.040274)) + 0.501961;
    return float2(cb, cr);
}
)";

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target, std::string& error) {
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS
#if defined(_DEBUG)
        | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION
#endif
        ;
    const HRESULT result = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "OpenCaptureCrop.hlsl",
                                      nullptr, nullptr, entryPoint, target, flags, 0, &bytecode, &messages);
    if (FAILED(result)) {
        if (messages) error.assign(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize());
        else error = "D3DCompile failed for the GPU crop shader.";
        return {};
    }
    return bytecode;
}

} // namespace

bool D3D11FrameProcessor::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    Reset();
    if (!device || !context) {
        SetError("A D3D11 device and immediate context are required.");
        return false;
    }
    device_ = device;
    context_ = context;

    auto vertexBytecode = CompileShader("VertexMain", "vs_5_0", lastError_);
    auto bgraBytecode = CompileShader("BgraMain", "ps_5_0", lastError_);
    auto lumaBytecode = CompileShader("LumaMain", "ps_5_0", lastError_);
    auto chromaBytecode = CompileShader("ChromaMain", "ps_5_0", lastError_);
    if (!vertexBytecode || !bgraBytecode || !lumaBytecode || !chromaBytecode ||
        FAILED(device_->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                                           nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(bgraBytecode->GetBufferPointer(), bgraBytecode->GetBufferSize(),
                                          nullptr, &bgraShader_)) ||
        FAILED(device_->CreatePixelShader(lumaBytecode->GetBufferPointer(), lumaBytecode->GetBufferSize(),
                                          nullptr, &lumaShader_)) ||
        FAILED(device_->CreatePixelShader(chromaBytecode->GetBufferPointer(), chromaBytecode->GetBufferSize(),
                                          nullptr, &chromaShader_))) {
        if (lastError_.empty()) SetError("Could not create the GPU processing shaders.");
        Reset();
        return false;
    }
    device_.As(&device3_);

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&samplerDescription, &sampler_))) {
        SetError("Could not create the GPU crop sampler.");
        Reset();
        return false;
    }

    D3D11_BUFFER_DESC bufferDescription{};
    bufferDescription.ByteWidth = sizeof(float) * 4;
    bufferDescription.Usage = D3D11_USAGE_DEFAULT;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device_->CreateBuffer(&bufferDescription, nullptr, &cropBuffer_))) {
        SetError("Could not create the GPU crop constant buffer.");
        Reset();
        return false;
    }
    lastError_.clear();
    return true;
}

void D3D11FrameProcessor::Reset() noexcept {
    for (auto& output : outputPool_) output = {};
    nextOutput_ = 0;
    cropBuffer_.Reset();
    sampler_.Reset();
    chromaShader_.Reset();
    lumaShader_.Reset();
    bgraShader_.Reset();
    vertexShader_.Reset();
    context_.Reset();
    device3_.Reset();
    device_.Reset();
}

std::optional<ProcessedFrame> D3D11FrameProcessor::Process(
    const CapturedFrame& frame, const CaptureTarget& target, FrameProcessOptions options) {
    if (!device_ || !context_ || !frame.texture) {
        SetError("The GPU frame processor is not initialized or received an empty frame.");
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC inputDescription{};
    frame.texture->GetDesc(&inputDescription);
    const RECT crop = ResolveCrop(frame, target);
    const LONG cropWidth = std::max<LONG>(0, crop.right - crop.left);
    const LONG cropHeight = std::max<LONG>(0, crop.bottom - crop.top);
    if (cropWidth == 0 || cropHeight == 0) {
        SetError("The selected crop does not intersect the captured surface.");
        return std::nullopt;
    }
    const bool nv12 = options.pixelFormat == FramePixelFormat::Nv12;
    const SIZE sourceSize{cropWidth, cropHeight};
    const SIZE requestedSize = options.maximumOutputHeight > 0
        ? FitOutputHeight(sourceSize, options.maximumOutputHeight, nv12)
        : options.outputSize;
    const SIZE outputSize = NormalizeOutputSize(requestedSize, sourceSize, nv12);
    if (outputSize.cx <= 0 || outputSize.cy <= 0) {
        SetError("The requested GPU output size is empty.");
        return std::nullopt;
    }
    const auto outputFormat = nv12 ? DXGI_FORMAT_NV12 : inputDescription.Format;
    OutputSlot* output = AcquireOutput(static_cast<UINT>(outputSize.cx), static_cast<UINT>(outputSize.cy), outputFormat);
    if (!output) return std::nullopt;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inputView;
    if (FAILED(device_->CreateShaderResourceView(frame.texture.Get(), nullptr, &inputView))) {
        SetError("The WGC frame could not be bound as a shader resource.");
        return std::nullopt;
    }

    const std::array<float, 4> cropUv{
        static_cast<float>(crop.left) / static_cast<float>(inputDescription.Width),
        static_cast<float>(crop.top) / static_cast<float>(inputDescription.Height),
        static_cast<float>(cropWidth) / static_cast<float>(inputDescription.Width),
        static_cast<float>(cropHeight) / static_cast<float>(inputDescription.Height),
    };
    context_->UpdateSubresource(cropBuffer_.Get(), 0, nullptr, cropUv.data(), 0, 0);

    if (nv12) {
        DrawPlane(inputView.Get(), output->plane0.Get(), lumaShader_.Get(),
                  static_cast<UINT>(outputSize.cx), static_cast<UINT>(outputSize.cy));
        DrawPlane(inputView.Get(), output->plane1.Get(), chromaShader_.Get(),
                  static_cast<UINT>(outputSize.cx / 2), static_cast<UINT>(outputSize.cy / 2));
    } else {
        DrawPlane(inputView.Get(), output->plane0.Get(), bgraShader_.Get(),
                  static_cast<UINT>(outputSize.cx), static_cast<UINT>(outputSize.cy));
    }

    lastError_.clear();
    return ProcessedFrame{output->texture, outputSize, frame.qpcTimestamp, outputFormat};
}

RECT D3D11FrameProcessor::ResolveCrop(const CapturedFrame& frame, const CaptureTarget& target) const noexcept {
    const RECT fullFrame{0, 0, frame.contentSize.cx, frame.contentSize.cy};
    if (target.type != CaptureTargetType::Region) return fullFrame;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!target.monitor || !GetMonitorInfoW(target.monitor, &info)) return {};
    RECT local = ToLocalClampedRegion(target.region, info.rcMonitor);
    local.right = std::min(local.right, frame.contentSize.cx);
    local.bottom = std::min(local.bottom, frame.contentSize.cy);
    local.left = std::min(local.left, local.right);
    local.top = std::min(local.top, local.bottom);
    return local;
}

D3D11FrameProcessor::OutputSlot* D3D11FrameProcessor::AcquireOutput(UINT width, UINT height, DXGI_FORMAT format) {
    auto& output = outputPool_[nextOutput_];
    nextOutput_ = (nextOutput_ + 1) % outputPool_.size();
    if ((!output.texture || output.width != width || output.height != height || output.format != format) &&
        !CreateOutput(output, width, height, format)) {
        return nullptr;
    }
    return &output;
}

bool D3D11FrameProcessor::CreateOutput(OutputSlot& output, UINT width, UINT height, DXGI_FORMAT format) {
    output = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &output.texture))) {
        SetError("Could not allocate the GPU output texture.");
        return false;
    }
    if (format == DXGI_FORMAT_NV12) {
        if (!device3_) {
            SetError("The D3D11 device does not support NV12 plane render-target views.");
            output = {};
            return false;
        }
        D3D11_RENDER_TARGET_VIEW_DESC1 yDescription{};
        yDescription.Format = DXGI_FORMAT_R8_UNORM;
        yDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        yDescription.Texture2D.MipSlice = 0;
        yDescription.Texture2D.PlaneSlice = 0;
        D3D11_RENDER_TARGET_VIEW_DESC1 uvDescription = yDescription;
        uvDescription.Format = DXGI_FORMAT_R8G8_UNORM;
        uvDescription.Texture2D.PlaneSlice = 1;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView1> yView;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView1> uvView;
        if (FAILED(device3_->CreateRenderTargetView1(output.texture.Get(), &yDescription, &yView)) ||
            FAILED(device3_->CreateRenderTargetView1(output.texture.Get(), &uvDescription, &uvView)) ||
            FAILED(yView.As(&output.plane0)) || FAILED(uvView.As(&output.plane1))) {
            SetError("Could not create the NV12 Y and UV plane views.");
            output = {};
            return false;
        }
    } else if (FAILED(device_->CreateRenderTargetView(output.texture.Get(), nullptr, &output.plane0))) {
        SetError("Could not create the BGRA output render-target view.");
        output = {};
        return false;
    }
    output.width = width;
    output.height = height;
    output.format = format;
    return true;
}

void D3D11FrameProcessor::DrawPlane(ID3D11ShaderResourceView* sourceView,
                                    ID3D11RenderTargetView* targetView,
                                    ID3D11PixelShader* shader, UINT width, UINT height) {
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
    ID3D11SamplerState* sampler = sampler_.Get();
    ID3D11Buffer* constants = cropBuffer_.Get();
    context_->OMSetRenderTargets(1, &targetView, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(shader, nullptr, 0);
    context_->PSSetShaderResources(0, 1, &sourceView);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->PSSetConstantBuffers(0, 1, &constants);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* emptyView{};
    context_->PSSetShaderResources(0, 1, &emptyView);
}

void D3D11FrameProcessor::SetError(std::string message) { lastError_ = std::move(message); }

} // namespace opencapture
