# FFmpeg corresponding source and build information

OpenCapture 0.2.0 for Windows dynamically links FFmpeg 8.1.2 libraries built
through the vcpkg dependency manifest and baseline recorded in the OpenCapture
source tree.

The materials needed to inspect or reproduce this FFmpeg build are available
from the following locations:

- FFmpeg 8.1.2 source:
  <https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz>
- OpenCapture 0.2.0 source and dependency manifest:
  <https://github.com/CharlieYang0040/OpenCapture/tree/v0.2.0>
- Exact vcpkg FFmpeg port files and patches used for the binary:
  `licenses/ffmpeg-vcpkg-port/` in the release package and the separate
  `OpenCapture-0.2.0-ffmpeg-build-materials.zip` release asset.
- Exact linked FFmpeg configuration:
  `licenses/FFMPEG_BUILD_CONFIGURATION.txt`.
- Package copyright and SPDX data:
  `licenses/ffmpeg-copyright.txt` and
  `licenses/ffmpeg-vcpkg.spdx.json`.

The FFmpeg DLLs are distributed separately from `OpenCapture.exe`. They may be
replaced with a compatible rebuild that preserves the required ABI and enabled
features. Keep a backup of the release package before replacing DLLs.

For at least three years after publication of this release, the corresponding
source and build materials will remain available from the GitHub release and
repository above. If those links become unavailable, request a copy through the
repository issue tracker. No charge is imposed beyond any unavoidable cost of
transferring the files.
