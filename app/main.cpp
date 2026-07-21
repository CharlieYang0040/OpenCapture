#include "platform/win32_d3d11_app.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    opencapture::Win32D3D11App app;
    if (!app.Initialize(instance, showCommand)) {
        MessageBoxW(nullptr, L"OpenCapture could not initialize Direct3D 11.", L"OpenCapture", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}

