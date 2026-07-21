# OpenCapture

OpenCapture is a Windows 10/11 native screen capture application under active development. It is designed around a GPU-first path: Windows Graphics Capture or Desktop Duplication produces D3D11 textures, GPU shaders prepare frames, and FFmpeg hardware encoders write the result without a per-frame full-image CPU round trip.

The current milestone provides the C++20 project foundation, module boundaries, Win32/D3D11/ImGui shell, FFmpeg runtime diagnostics, capture target model, recording state machine, and a bounded drop-oldest video queue.

## Prerequisites

- Windows 10 version 1903 or newer, or Windows 11
- Visual Studio 2022 Build Tools 17.14 or newer with **Desktop development with C++** and CMake tools
- CMake 3.21 or newer (included with the Visual Studio component above)
- Git for Windows

Do not put credentials or machine-specific paths in the repository. OpenCapture currently requires no credentials.

## Quick start on a new PC

Clone the repository and run the setup script from a regular PowerShell terminal:

```powershell
git clone https://github.com/CharlieYang0040/OpenCapture.git
cd OpenCapture
powershell -ExecutionPolicy Bypass -File .\scripts\setup.ps1
```

The script downloads the pinned vcpkg revision into the ignored `.tools` directory, installs dependencies, builds the Debug configuration, and runs the tests.

To prepare dependencies without building, or to build Release:

```powershell
.\scripts\setup.ps1 -SkipBuild
.\scripts\setup.ps1 -Configuration Release
```

If vcpkg is already installed globally, set `VCPKG_ROOT` and use the shared preset:

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
```

For a dependency-free core-only build and test:

```powershell
cmake --preset vs2022-core-tests
cmake --build --preset vs2022-core-tests
ctest --preset vs2022-core-tests
```

Run `build/repo-vs2022-x64/Debug/OpenCapture.exe`. The initial application window reports the active D3D11 adapter and linked FFmpeg version. Capture buttons are deliberately inactive until the capture milestone is implemented.

Machine-specific CMake overrides belong in `CMakeUserPresets.json`, which is intentionally ignored. Build outputs, downloaded dependencies, editor settings, environment files, and signing keys are also excluded from Git.

## Architecture

- `app`: composition root and process entry point
- `core`: target model, session lifecycle, settings, clocks, and queues
- `capture`: Windows Graphics Capture and Desktop Duplication implementations
- `graphics`: D3D11 crop, scale, color conversion, and HDR processing
- `audio`: WASAPI capture, mixing, and clock synchronization
- `encoding`: FFmpeg encoders, hardware selection, and muxing
- `image`: WIC image saving and clipboard integration
- `ui`: presentation and command dispatch only
- `platform`: Win32, D3D11, DPI, tray, and hotkey integration
- `tests`: deterministic unit, integration, soak, and performance tests

The detailed roadmap and performance acceptance criteria are in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md).

## License

OpenCapture source code is licensed under the MIT License. Distributed builds must also include notices for their exact FFmpeg, Dear ImGui, and hardware SDK configuration. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
