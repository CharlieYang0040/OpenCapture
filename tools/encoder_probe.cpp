#include "encoder/ffmpeg_encoder_registry.h"

#include <Windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <iomanip>
#include <iostream>

int main() {
    std::uint32_t vendor{};
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description))) vendor = description.VendorId;
    }

    opencapture::FFmpegEncoderRegistry registry;
    registry.Probe(vendor);
    std::cout << "D3D_VENDOR=0x" << std::hex << std::uppercase << vendor << std::dec << '\n';
    for (const auto& capability : registry.Capabilities()) {
        std::cout << capability.name
                  << " registered=" << capability.registered
                  << " adapter=" << capability.adapterCompatible
                  << " device=" << capability.deviceAvailable
                  << " usable=" << capability.usable
                  << " detail=" << capability.detail << '\n';
    }
    std::cout << "SELECTED=" << (registry.SelectedH264() ? registry.SelectedH264()->name : "none") << '\n';
    return registry.SelectedH264() ? 0 : 2;
}
