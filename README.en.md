# OpenCapture

**English** | [한국어](README.md)

OpenCapture is a native screen capture application for Windows 10 and 11. Its goal is a GPU-first recording path that keeps captured frames as D3D11 textures through processing and hardware encoding, avoiding a full-frame CPU memory round trip for every frame.

The project is an actively developed technical prototype. It can capture a window, monitor, or region with Windows Graphics Capture, crop and scale on D3D11, and produce H.264 MKV recordings or PNG/clipboard still captures. Audio, GIF, and safe-output workflows are still under development.

## Goals

- Use Windows Graphics Capture and Desktop Duplication where appropriate
- Keep video processing on the GPU with D3D11 textures
- Use FFmpeg hardware encoders such as NVENC, Quick Sync, and AMF
- Support monitor, window, region, and still-image capture
- Capture and synchronize system and microphone audio through WASAPI
- Keep memory use and latency bounded during long recordings
- Separate capture, graphics, encoding, and UI code for testing and maintenance

## Current status

Implemented:

- C++20 project using CMake and vcpkg
- Win32 window and D3D11 device initialization
- Dear ImGui application shell
- FFmpeg linkage and runtime-version diagnostics
- Window, monitor, and DPI-aware region selection
- Persistent named region presets with management actions
- Windows Graphics Capture frames with QPC timestamps
- D3D11 shader crop, scale, and BGRA-to-NV12 conversion
- Real-time H.264 NVENC encoding and MKV output
- Automatic/manual H.264 selection with an OpenH264 final fallback
- Recording start/stop controls and active-encoder status
- PNG output, clipboard-only capture, and combined save-and-copy
- Diskless Windows clipboard transfer through `CF_DIBV5`
- Bounded queue with a drop-oldest overload policy
- Core unit tests and Windows GitHub Actions CI

Not implemented yet:

- Audio capture, mixing, and synchronization
- GIF recording and MP4 remux
- Temporary output, recovery, pause, and free-space checks
- Desktop Duplication, global hotkeys, and tray integration
- QSV/AMF hardware validation and long-running performance tests

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for the roadmap and performance acceptance criteria.

## Requirements

- Windows 10 version 1903 or newer, or Windows 11
- Visual Studio 2022 Build Tools 17.14 or newer
  - **Desktop development with C++** workload
  - MSVC v143, Windows SDK, and CMake tools
- CMake 3.21 or newer
- Git for Windows
- An internet connection for the initial vcpkg and dependency download

OpenCapture currently requires no credentials. Do not commit credentials or machine-specific absolute paths.

## Quick start on a new PC

Run the following commands in a regular PowerShell terminal:

```powershell
git clone https://github.com/CharlieYang0040/OpenCapture.git
cd OpenCapture
powershell -ExecutionPolicy Bypass -File .\scripts\setup.ps1
```

The script verifies Git and CMake, downloads the pinned vcpkg revision to `.tools/vcpkg`, copies Visual Studio's Ninja into `.tools/ninja`, initializes the x64 MSVC environment, installs the dependencies in `vcpkg.json`, configures with CMake's Ninja generator, builds Debug, and runs the core tests.

The resulting executable is located at:

```text
build/repo-ninja-x64-debug/OpenCapture.exe
```

Use these options to build Release or only prepare the tooling:

```powershell
.\scripts\setup.ps1 -Configuration Release
.\scripts\setup.ps1 -SkipBuild
```

### Build flow

```text
C++ source code
  ↓
CMake configures the build
  ↓
vcpkg prepares Dear ImGui and FFmpeg
  ↓
Ninja schedules compilation work
  ↓
MSVC compiles the C++ files
  ↓
The MSVC linker links the EXE and DLL dependencies
  ↓
OpenCapture.exe is produced
```

`setup.ps1` uses the Ninja executable included with Visual Studio, so a separate global Ninja installation is not required.

## Manual build

Prepare the repository-local tools in PowerShell. Run the subsequent CMake commands from an **x64 Native Tools Command Prompt for VS 2022** or another terminal with the x64 MSVC environment initialized:

```powershell
.\scripts\setup.ps1 -SkipBuild
cmake --preset repo-ninja-x64-debug
cmake --build --preset repo-ninja-x64-debug
ctest --preset repo-ninja-x64-debug
```

For a dependency-free core-only build:

```powershell
cmake --preset vs2022-core-tests
cmake --build --preset vs2022-core-tests
ctest --preset vs2022-core-tests
```

If vcpkg is installed globally, set `VCPKG_ROOT` and use `vs2022-x64-debug` instead of `repo-ninja-x64-debug`.

## Build system

- Generator: Ninja
- Architecture: x64
- Language: C++20
- Package manager: vcpkg manifest mode
- Main libraries: Dear ImGui and FFmpeg
- Windows APIs: Win32, D3D11, DXGI, and DWM
- Compiler and linker: MSVC v143

The vcpkg baseline is pinned in both `vcpkg.json` and `scripts/setup.ps1` for reproducible dependency resolution. Put machine-specific CMake overrides in the ignored `CMakeUserPresets.json` file.

## Project layout

```text
OpenCapture/
├─ app/        Process entry point and composition
├─ core/       Capture targets, session state, and bounded queues
├─ encoder/    FFmpeg discovery, D3D11 encoding, and muxing
├─ gpu/        D3D11 crop, scale, and color conversion
├─ image/      WIC screenshot output and Windows clipboard transfer
├─ platform/   Win32, WGC, target selection, and DPI integration
├─ ui/         Dear ImGui presentation and command dispatch
├─ tests/      Automated core tests
├─ tools/      Encoder and integration smoke diagnostics
├─ scripts/    New-machine setup automation
└─ .github/    GitHub Actions CI
```

Future milestones will add the `audio` module. UI code should remain limited to presentation and command dispatch, while capture, image, and encoding logic stays independently testable.

## Troubleshooting

- If Ninja or MSVC cannot be found, install **Desktop development with C++** and the CMake tools through Visual Studio Installer, then open a new PowerShell terminal.
- The first dependency installation can take a while because it prepares FFmpeg and related packages. `.tools`, `build`, and `vcpkg_installed` are local and ignored by Git.
- Do not reuse the same CMake build directory with a different generator. Use a separate directory when switching between Visual Studio and Ninja.

## License

OpenCapture is distributed under the [MIT License](LICENSE). Distributed builds must include notices matching their actual FFmpeg, Dear ImGui, and hardware SDK configuration. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
