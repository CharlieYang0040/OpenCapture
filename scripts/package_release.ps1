[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^\d+\.\d+\.\d+$")]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$releaseBuild = Join-Path $repositoryRoot "build\repo-ninja-x64-release"
$outputRoot = Join-Path $repositoryRoot "out\release-v$Version"
$packageName = "OpenCapture-$Version-windows-x64"
$sourceMaterialsName = "OpenCapture-$Version-ffmpeg-build-materials"
$stageDirectory = Join-Path $outputRoot $packageName
$sourceMaterialsDirectory = Join-Path $outputRoot $sourceMaterialsName
$verifyDirectory = Join-Path $outputRoot "verify"
$archivePath = Join-Path $outputRoot "$packageName.zip"
$sourceMaterialsArchivePath = Join-Path $outputRoot "$sourceMaterialsName.zip"
$checksumPath = Join-Path $outputRoot "SHA256SUMS.txt"

$expectedOutputRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "out\release-v$Version"))
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($outputRoot)
if ($resolvedOutputRoot -ne $expectedOutputRoot -or
    -not $resolvedOutputRoot.StartsWith(
        [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "out")),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package into an unexpected path: $resolvedOutputRoot"
}

$cmakeText = Get-Content -Raw (Join-Path $repositoryRoot "CMakeLists.txt")
$manifest = Get-Content -Raw (Join-Path $repositoryRoot "vcpkg.json") | ConvertFrom-Json
if ($cmakeText -notmatch "project\(OpenCapture VERSION $([regex]::Escape($Version)) ") {
    throw "CMake project version does not match $Version."
}
if ($manifest.'version-semver' -ne $Version) {
    throw "vcpkg manifest version does not match $Version."
}

$requiredBuildFiles = @(
    "OpenCapture.exe",
    "opencapture_ffmpeg_build_info.exe",
    "avcodec-62.dll",
    "avfilter-11.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "libvpl.dll",
    "openh264-7.dll",
    "swresample-6.dll",
    "swscale-9.dll"
)
foreach ($name in $requiredBuildFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseBuild $name))) {
        throw "Release build file is missing: $name"
    }
}

if (Test-Path -LiteralPath $resolvedOutputRoot) {
    Remove-Item -LiteralPath $resolvedOutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null
$licenseDirectory = New-Item -ItemType Directory -Force -Path (
    Join-Path $stageDirectory "licenses")

foreach ($name in $requiredBuildFiles | Where-Object { $_ -ne "opencapture_ffmpeg_build_info.exe" }) {
    Copy-Item -LiteralPath (Join-Path $releaseBuild $name) -Destination $stageDirectory
}
foreach ($name in @("LICENSE", "README.md", "README.en.md",
                     "THIRD_PARTY_NOTICES.md", "FFMPEG_SOURCE_OFFER.md",
                     "vcpkg.json")) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $name) -Destination $stageDirectory
}

$vcpkgRoot = Join-Path $releaseBuild "vcpkg_installed"
$vcpkgShare = Join-Path $vcpkgRoot "x64-windows\share"
foreach ($package in @("amd-amf", "ffmpeg", "ffnvcodec", "imgui", "libvpl", "openh264")) {
    $share = Join-Path $vcpkgShare $package
    Copy-Item -LiteralPath (Join-Path $share "copyright") -Destination (
        Join-Path $licenseDirectory "$package-copyright.txt")
    Copy-Item -LiteralPath (Join-Path $share "vcpkg.spdx.json") -Destination (
        Join-Path $licenseDirectory "$package-vcpkg.spdx.json")
}
Copy-Item -LiteralPath (Join-Path $vcpkgRoot "vcpkg\status") -Destination (
    Join-Path $licenseDirectory "vcpkg-status.txt")
$ffmpegPortDirectory = Join-Path $repositoryRoot ".tools\vcpkg\ports\ffmpeg"
if (-not (Test-Path -LiteralPath $ffmpegPortDirectory -PathType Container)) {
    throw "The pinned vcpkg FFmpeg port directory is missing."
}
Copy-Item -LiteralPath $ffmpegPortDirectory -Destination (
    Join-Path $licenseDirectory "ffmpeg-vcpkg-port") -Recurse

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$visualStudioDirectory = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioDirectory) {
    throw "Visual Studio 2022 C++ Build Tools were not found."
}
$redistRoot = Get-ChildItem (Join-Path $visualStudioDirectory "VC\Redist\MSVC") -Directory |
    Where-Object { $_.Name -match "^\d+\.\d+\.\d+$" } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $redistRoot) {
    throw "MSVC redistributable directory was not found."
}
$crtDirectory = Join-Path $redistRoot.FullName "x64\Microsoft.VC143.CRT"
foreach ($name in @("msvcp140.dll", "msvcp140_atomic_wait.dll",
                     "vcruntime140.dll", "vcruntime140_1.dll")) {
    Copy-Item -LiteralPath (Join-Path $crtDirectory $name) -Destination $stageDirectory
}
$redistNotice = Join-Path $visualStudioDirectory "Licenses\1033\Redist.txt"
if (-not (Test-Path -LiteralPath $redistNotice)) {
    $redistNotice = Join-Path $visualStudioDirectory "Licenses\1042\Redist.txt"
}
Copy-Item -LiteralPath $redistNotice -Destination (
    Join-Path $licenseDirectory "Microsoft-Visual-Studio-Redist.txt")

$ffmpegInfo = & (Join-Path $releaseBuild "opencapture_ffmpeg_build_info.exe")
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the linked FFmpeg build information."
}
[System.IO.File]::WriteAllLines(
    (Join-Path $licenseDirectory "FFMPEG_BUILD_CONFIGURATION.txt"),
    [string[]]$ffmpegInfo,
    [System.Text.UTF8Encoding]::new($false))

New-Item -ItemType Directory -Force -Path $sourceMaterialsDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryRoot "FFMPEG_SOURCE_OFFER.md") `
    -Destination $sourceMaterialsDirectory
Copy-Item -LiteralPath $ffmpegPortDirectory -Destination (
    Join-Path $sourceMaterialsDirectory "ffmpeg-vcpkg-port") -Recurse
foreach ($name in @("FFMPEG_BUILD_CONFIGURATION.txt",
                     "ffmpeg-copyright.txt",
                     "ffmpeg-vcpkg.spdx.json",
                     "vcpkg-status.txt")) {
    Copy-Item -LiteralPath (Join-Path $licenseDirectory $name) `
        -Destination $sourceMaterialsDirectory
}
Copy-Item -LiteralPath (Join-Path $repositoryRoot "vcpkg.json") `
    -Destination $sourceMaterialsDirectory

$sourceCommit = (git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the source commit."
}
$signature = Get-AuthenticodeSignature (Join-Path $stageDirectory "OpenCapture.exe")
$buildInfo = @(
    "OpenCapture $Version",
    "Source commit: $sourceCommit",
    "Platform: Windows x64",
    "Configuration: Release",
    "Toolchain: CMake + Ninja + MSVC v143 + vcpkg",
    "Authenticode status: $($signature.Status)",
    "Network behavior: OpenCapture does not upload captures or require network access at runtime."
)
[System.IO.File]::WriteAllLines(
    (Join-Path $stageDirectory "BUILD_INFO.txt"),
    $buildInfo,
    [System.Text.UTF8Encoding]::new($false))

$manifestLines = Get-ChildItem $stageDirectory -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($stageDirectory.Length + 1).Replace("\", "/")
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        "$hash *$relative"
    }
[System.IO.File]::WriteAllLines(
    (Join-Path $stageDirectory "PACKAGE_MANIFEST.sha256"),
    [string[]]$manifestLines,
    [System.Text.UTF8Encoding]::new($false))

Compress-Archive -LiteralPath $stageDirectory -DestinationPath $archivePath -CompressionLevel Optimal
Compress-Archive -LiteralPath $sourceMaterialsDirectory `
    -DestinationPath $sourceMaterialsArchivePath -CompressionLevel Optimal
$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
$sourceMaterialsArchiveHash = (
    Get-FileHash -Algorithm SHA256 -LiteralPath $sourceMaterialsArchivePath
).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    $checksumPath,
    "$archiveHash *$packageName.zip`n" +
    "$sourceMaterialsArchiveHash *$sourceMaterialsName.zip`n",
    [System.Text.UTF8Encoding]::new($false))

Expand-Archive -LiteralPath $archivePath -DestinationPath $verifyDirectory
$verifiedStage = Join-Path $verifyDirectory $packageName
$expectedManifest = Get-Content -LiteralPath (
    Join-Path $stageDirectory "PACKAGE_MANIFEST.sha256")
foreach ($entry in $expectedManifest) {
    if ($entry -notmatch "^([0-9a-f]{64}) \*(.+)$") {
        throw "Invalid package manifest entry: $entry"
    }
    $verifiedFile = Join-Path $verifiedStage $Matches[2]
    if (-not (Test-Path -LiteralPath $verifiedFile)) {
        throw "Archive verification is missing: $($Matches[2])"
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $verifiedFile).Hash.ToLowerInvariant()
    if ($actualHash -ne $Matches[1]) {
        throw "Archive verification hash mismatch: $($Matches[2])"
    }
}

Write-Host "Release package: $archivePath"
Write-Host "SHA-256: $archiveHash"
Write-Host "FFmpeg build materials: $sourceMaterialsArchivePath"
Write-Host "SHA-256: $sourceMaterialsArchiveHash"
Write-Host "Verified files: $($expectedManifest.Count)"
