[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$toolsDirectory = Join-Path $repositoryRoot ".tools"
$vcpkgDirectory = Join-Path $toolsDirectory "vcpkg"
$vcpkgRevision = "3ddaad9be959816602453ecb05533f8732464ef4"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required. Install Git for Windows, then run this script again."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake 3.21 or newer is required. Install Visual Studio 2022 C++ tools with CMake support."
}

New-Item -ItemType Directory -Force -Path $toolsDirectory | Out-Null

if (-not (Test-Path (Join-Path $vcpkgDirectory ".git"))) {
    git clone https://github.com/microsoft/vcpkg.git $vcpkgDirectory
}

git -C $vcpkgDirectory fetch --depth 1 origin $vcpkgRevision
git -C $vcpkgDirectory checkout --detach $vcpkgRevision

$bootstrap = Join-Path $vcpkgDirectory "bootstrap-vcpkg.bat"
& $bootstrap -disableMetrics
if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed with exit code $LASTEXITCODE." }

if ($SkipBuild) {
    Write-Host "Tooling is ready. Run: cmake --preset repo-vs2022-x64"
    exit 0
}

cmake --preset repo-vs2022-x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

$buildPreset = "repo-vs2022-x64-$($Configuration.ToLowerInvariant())"
cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

if ($Configuration -eq "Debug") {
    ctest --preset repo-vs2022-x64-debug
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
}

Write-Host "OpenCapture is ready: build/repo-vs2022-x64/$Configuration/OpenCapture.exe"
