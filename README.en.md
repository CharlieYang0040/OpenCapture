# OpenCapture

**English** | [한국어](README.md)

OpenCapture is a native screen capture application for Windows 10 and 11. Its goal is a GPU-first recording path that keeps captured frames as D3D11 textures through processing and hardware encoding, avoiding a full-frame CPU memory round trip for every frame.

The project is an actively developed technical prototype. It can capture a window, monitor, or region with Windows Graphics Capture, crop and scale on D3D11, and produce H.264, HEVC, or AV1 video with AAC in MKV/MP4, palette-optimized GIFs, or PNG/clipboard still captures.

The latest technical preview is [OpenCapture v0.2.4](https://github.com/CharlieYang0040/OpenCapture/releases/tag/v0.2.4).
See the [v0.2.4 release notes](docs/releases/v0.2.4.md) for its exact scope and validation boundary.

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
- H.264, HEVC, and AV1 hardware selection with codec-aware safe fallback
- GPU-scaled recording at source size, a 1080p maximum, or a 720p maximum
- Compatibility, Balanced, Compact, and Custom profiles with an hourly size estimate
- NVENC Realtime, Balanced, and Efficient modes plus a custom target bitrate
- Recording controls with the active encoder, output size, and target bitrate
- PNG output, clipboard-only capture, and combined save-and-copy
- Diskless Windows clipboard transfer through `CF_DIBV5`
- Event-driven WASAPI loopback/microphone capture foundation and probe
- 48 kHz stereo mixing, AAC encoding, and synchronized MKV A/V muxing
- Safe `.part.mkv` recording, 512 MiB free-space gate, and final-name commit
- Incomplete-MKV discovery and output collision avoidance
- UI recovery that validates incomplete MKV streams before choosing a collision-free final name
- Recording pause/resume with paused time removed from the output timeline
- MP4 remux copies that preserve the safe MKV source
- GIF recording from the same targets and saved region presets
- Selectable GIF output at 360p, 480p, 720p, or 1080p; 6-30 fps; and 64-256 colors
- GPU-scaled low-FPS source recording and FFmpeg two-pass palettegen/paletteuse conversion
- Automatic GIF stop at 30 seconds or a 500-million-pixel processing budget, with source MKV recovery on failure
- Non-blocking background GIF conversion with staged progress and cancellation
- Media-job lifetime handling that removes `.part.gif` while retaining the safe source MKV on cancellation or shutdown
- One configurable output folder shared by screenshots and recordings
- Region confirmation that returns to the main app and restores its monitor after restart
- Region selection that keeps the selected area clear and dims only the outside by a configurable 0-70%
- Region coordinates constrained to the starting monitor so cross-monitor drags cannot silently produce clipped output
- Result-oriented screenshot, video, and GIF controls
- Click-through always-on-top borders for the selected region, window, or monitor
- Capture-excluded blue idle, yellow capture, and orange paused border states
- Persistent target-border visibility, 1-12 px thickness, and 20-100% opacity controls
- Configurable capture, video, and GIF global shortcuts with conflict and repeat protection
- Direct keyboard shortcut capture with clear, restore-default, and restart persistence
- Persistent profile, resolution, FPS, codec, compression, bitrate, audio, and GIF settings
- Result-oriented tooltips for primary actions and performance/size controls
- Adaptive UI that follows 100-200% monitor DPI with a persistent 75-200% user adjustment
- Configurable shortcut output: clipboard, PNG file, or both
- `Ctrl+Shift+F8` Quick Capture without changing the persistent region or presets
- Automatic main-window hiding during region selection and Capture/Video/GIF/Settings tabs
- Opt-in notification-area background operation with configurable close behavior
- Notification-area actions for opening the app, Quick Capture, stopping a recording, and fully exiting
- Company-PC-friendly default that fully exits when a new user closes the window
- Idle wait while minimized or hidden in the notification area to avoid extra CPU use
- Bounded queue with a drop-oldest overload policy
- Core unit tests and Windows GitHub Actions CI

Not implemented or still pending hardware validation:

- Audio device selection, per-source volume/mute, Opus, and long-run drift correction
- General MP4/MKV/WebM offline transcoding with VP9/Opus support
- Desktop Duplication
- QSV/AMF hardware validation and long-running performance tests
- Actual HEVC/AV1 support depends on the GPU generation and driver; OpenCapture verifies the
  D3D11 encoder when recording starts and Auto falls back to the next compatible codec

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for the roadmap and performance acceptance criteria.

## Company-PC security behavior

OpenCapture does not install a Windows service, scheduled task, startup entry, or process/DLL
injection, and it does not hook games or request administrator rights. It uses Windows Graphics
Capture, global shortcuts, and clipboard APIs inside the process explicitly launched by the user.

For new users, closing the main window fully exits the app. Users who need background shortcuts
can explicitly enable `Keep running when the window is closed` in Settings and can always fully
exit through `Exit OpenCapture` in the notification-area menu. A company security policy may still
require prior approval for screen or audio capture regardless of code-signing status.

## Requirements

- Windows 10 version 1903 or newer, or Windows 11
- Visual Studio 2022 Build Tools 17.14 or newer
  - **Desktop development with C++** workload
  - MSVC v143, Windows SDK, and CMake tools
- CMake 3.21 or newer
- Git for Windows
- An internet connection for the initial vcpkg and dependency download

OpenCapture currently requires no credentials. Do not commit credentials or machine-specific absolute paths.

## Install a release build

Download `OpenCapture-0.2.4-windows-x64.zip` and `SHA256SUMS.txt` from GitHub Releases,
verify the SHA-256, and extract the entire ZIP into one folder. Keep the DLLs and `licenses`
directory beside `OpenCapture.exe`. This technical preview is not yet Authenticode-signed and
may trigger SmartScreen or corporate security controls.
Corresponding FFmpeg source references, the exact vcpkg port and patches, and the linked build
configuration are included in the release ZIP and the separate
`OpenCapture-0.2.4-ffmpeg-build-materials.zip` asset.

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

After a successful Release build, reproduce the verified distribution ZIP with:

```powershell
.\scripts\package_release.ps1 -Version 0.2.4
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
├─ audio/      WASAPI system/microphone capture and audio pipeline
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

The next audio milestone will add device selection, per-source volume/mute, Opus, and long-run drift correction. UI code should remain limited to presentation and command dispatch, while capture, image, and encoding logic stays independently testable.

## Troubleshooting

- If Ninja or MSVC cannot be found, install **Desktop development with C++** and the CMake tools through Visual Studio Installer, then open a new PowerShell terminal.
- The first dependency installation can take a while because it prepares FFmpeg and related packages. `.tools`, `build`, and `vcpkg_installed` are local and ignored by Git.
- Do not reuse the same CMake build directory with a different generator. Use a separate directory when switching between Visual Studio and Ninja.

## License

OpenCapture is distributed under the [MIT License](LICENSE). Distributed builds must include notices matching their actual FFmpeg, Dear ImGui, and hardware SDK configuration. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
