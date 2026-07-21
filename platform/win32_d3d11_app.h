#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace opencapture {

class Win32D3D11App final {
public:
    Win32D3D11App() = default;
    ~Win32D3D11App();

    Win32D3D11App(const Win32D3D11App&) = delete;
    Win32D3D11App& operator=(const Win32D3D11App&) = delete;

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateDeviceAndSwapChain();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void Render();
    void Shutdown();

    HINSTANCE instance_{};
    HWND window_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    std::string gpuName_;
    std::string ffmpegVersion_;
    bool initialized_{};
};

} // namespace opencapture

