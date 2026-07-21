#include "platform/win32_d3d11_app.h"

#include "ui/main_panel.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern "C" {
#include <libavutil/avutil.h>
}

#include <array>
#include <cwchar>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace opencapture {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenCaptureWindow";

std::string ToUtf8(const wchar_t* text) {
    const int length = static_cast<int>(std::wcslen(text));
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), required, nullptr, nullptr);
    return result;
}

} // namespace

Win32D3D11App::~Win32D3D11App() { Shutdown(); }

bool Win32D3D11App::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return false;

    window_ = CreateWindowW(kWindowClass, L"OpenCapture", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 720, 520, nullptr, nullptr, instance_, this);
    if (!window_ || !CreateDeviceAndSwapChain()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(window_) || !ImGui_ImplDX11_Init(device_.Get(), context_.Get())) return false;

    ffmpegVersion_ = av_version_info();
    initialized_ = true;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

int Win32D3D11App::Run() {
    MSG message{};
    while (message.message != WM_QUIT) {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else {
            Render();
        }
    }
    return static_cast<int>(message.wParam);
}

bool Win32D3D11App::CreateDeviceAndSwapChain() {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL selectedLevel{};
    constexpr std::array requestedLevels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, requestedLevels.data(),
        static_cast<UINT>(requestedLevels.size()), D3D11_SDK_VERSION, &description,
        &swapChain_, &device_, &selectedLevel, &context_);
    if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, requestedLevels.data(),
            static_cast<UINT>(requestedLevels.size()), D3D11_SDK_VERSION, &description,
            &swapChain_, &device_, &selectedLevel, &context_);
    }
    if (FAILED(result)) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(device_.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC adapterDescription{};
        if (SUCCEEDED(adapter->GetDesc(&adapterDescription))) gpuName_ = ToUtf8(adapterDescription.Description);
    }
    CreateRenderTarget();
    return renderTarget_ != nullptr;
}

void Win32D3D11App::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
}

void Win32D3D11App::CleanupRenderTarget() { renderTarget_.Reset(); }

void Win32D3D11App::Render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    MainPanel::Draw(gpuName_, ffmpegVersion_);

    ImGui::Render();
    constexpr float clearColor[4]{0.055F, 0.065F, 0.08F, 1.0F};
    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain_->Present(1, 0);
}

void Win32D3D11App::Shutdown() {
    if (initialized_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }
    CleanupRenderTarget();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (instance_) {
        UnregisterClassW(kWindowClass, instance_);
        instance_ = nullptr;
    }
}

LRESULT CALLBACK Win32D3D11App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return 1;
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            auto* app = reinterpret_cast<Win32D3D11App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (app && app->swapChain_) {
                app->CleanupRenderTarget();
                app->swapChain_->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                app->CreateRenderTarget();
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0U) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        break;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace opencapture
