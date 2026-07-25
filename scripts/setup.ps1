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
$ninjaDirectory = Join-Path $toolsDirectory "ninja"
$ninjaExecutable = Join-Path $ninjaDirectory "ninja.exe"
$vcpkgRevision = "3ddaad9be959816602453ecb05533f8732464ef4"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required. Install Git for Windows, then run this script again."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake 3.21 or newer is required. Install Visual Studio 2022 C++ tools with CMake support."
}

New-Item -ItemType Directory -Force -Path $toolsDirectory | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer was not found. Install Visual Studio 2022 Build Tools with Desktop development with C++."
}

$visualStudioDirectory = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioDirectory) {
    throw "Visual Studio 2022 C++ Build Tools were not found. Install the Desktop development with C++ workload."
}

$vsDevCmd = Join-Path $visualStudioDirectory "Common7\Tools\VsDevCmd.bat"
$bundledNinja = Join-Path $visualStudioDirectory "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path $bundledNinja)) {
    throw "Visual Studio's Ninja executable was not found. Add the CMake tools component in Visual Studio Installer."
}

New-Item -ItemType Directory -Force -Path $ninjaDirectory | Out-Null
Copy-Item -LiteralPath $bundledNinja -Destination $ninjaExecutable -Force

# Import the x64 MSVC compiler and linker environment into this PowerShell process.
$developerEnvironment = & $env:COMSPEC /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw "Failed to initialize the Visual Studio x64 developer environment." }
foreach ($entry in $developerEnvironment) {
    $separator = $entry.IndexOf("=")
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable($entry.Substring(0, $separator), $entry.Substring($separator + 1), "Process")
    }
}

if (-not (Test-Path (Join-Path $vcpkgDirectory ".git"))) {
    git clone https://github.com/microsoft/vcpkg.git $vcpkgDirectory
}

git -C $vcpkgDirectory fetch --depth 1 origin $vcpkgRevision
git -C $vcpkgDirectory checkout --detach $vcpkgRevision

$bootstrap = Join-Path $vcpkgDirectory "bootstrap-vcpkg.bat"
& $bootstrap -disableMetrics
if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed with exit code $LASTEXITCODE." }

if ($SkipBuild) {
    Write-Host "Tooling is ready. Run this from an x64 Native Tools terminal: cmake --preset repo-ninja-x64-debug"
    exit 0
}

$configurePreset = "repo-ninja-x64-$($Configuration.ToLowerInvariant())"
cmake --preset $configurePreset
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

$buildPreset = $configurePreset
cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

if ($Configuration -eq "Debug") {
    ctest --preset repo-ninja-x64-debug
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
}

Write-Host "OpenCapture is ready: build/$configurePreset/OpenCapture.exe"
