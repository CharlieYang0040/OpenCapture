# Third-party notices

OpenCapture's source tree does not vendor third-party source code. The build resolves dependencies through vcpkg.

## Dear ImGui

Dear ImGui is licensed under the MIT License. A distributed binary must include the license text supplied by the resolved package.

## FFmpeg

FFmpeg is licensed under LGPL 2.1 or later by default, but enabled components and build flags can make a particular build GPL-licensed. Release artifacts must publish the exact vcpkg triplet, FFmpeg feature set, configuration flags, corresponding source offer, and license notices. Codec patent obligations are separate from copyright licensing and require review before distribution.

## libaom, libwebp, SVT-AV1, and fastfeat

AVIF and WebP output uses libaom, libwebp, and SVT-AV1 through FFmpeg. SVT-AV1 also resolves fastfeat. Release packages include each resolved package's copyright and SPDX records. Codec patent obligations remain separate from copyright licensing.

## Microsoft Windows SDK and DirectX

Windows SDK headers and runtime components are used under Microsoft's applicable license terms. Windows runtime components are not redistributed from this repository.

## Hardware encoders

NVENC, Intel QSV, and AMD AMF support may require vendor SDK headers or drivers. Their applicable redistribution terms must be reviewed before enabling them in a release package.
