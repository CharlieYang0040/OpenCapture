# 녹화 기술 심층 검토와 최적화 결정

검토일: 2026-08-31

## 결론

OpenCapture의 `Windows Graphics Capture → D3D11 crop/scale/NV12 → FFmpeg NVENC →
MKV 안전 기록 → 선택적 MP4 remux` 경로는 현재 Windows용 녹화기로서 현대적이고 적절한
기반이다. 다만 모든 상황에서 하나의 설정이 최선일 수는 없다. 게임 프레임타임, 용량,
화질, 재생 호환성은 서로 충돌하는 목표이므로 목적별 프리셋과 측정 가능한 진단이 필요하다.

이번 검토에서 즉시 개선할 수 있는 병목을 확인해 코드에 반영했고, 구조 변경이 필요한 항목은
후속 과제로 분리했다.

## 왜 기존 고효율 모드가 게임을 멈추게 할 수 있었나

NVENC의 높은 프리셋 번호, lookahead, AQ, multipass, B-frame reference는 같은 비트레이트의
화질이나 압축률을 높일 수 있지만 분석 프레임과 CUDA 작업, 참조 표면을 더 요구한다.
[NVIDIA Programming Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html)는
P1을 최고 성능, P7을 더 낮은 성능과 높은 화질 방향으로 설명하며, lookahead와 AQ 등을
추가 GPU 작업으로 분류한다. [NVIDIA FFmpeg Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)의
최대 화질 예시는 P6/P7, lookahead, B-reference, multipass를 사용하지만 UHQ는 오프라인 또는
실시간 제약이 느슨한 작업에 적합하다고 안내한다.

기존 문제 설정은 긴 lookahead와 작은 출력 텍스처 풀의 조합이었다. 게임과 인코더가 같은
GPU를 쓰는 상황에서 인코더가 참조할 프레임을 오래 보유하면 OpenCapture가 곧 재사용할
텍스처와 경합하고, 제출 지연이 게임 프레임까지 밀어낼 수 있다. FFmpeg의
[NVENC surface 계산 코드](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/nvenc.c)도
B-frame 수와 lookahead에 따라 필요한 surface 수를 늘린다.

## 이번에 적용한 개선

### 목적별 프리셋

| 프리셋 | 기본 설정 | 의도 |
|---|---|---|
| 게임 성능 | H.264, 최대 1080p, 60fps, P1/ULL, B-frame 없음 | 게임 반응성과 호환성 우선 |
| 균형 | HEVC, 최대 1080p, 60fps, P4/HQ, lookahead 없음 | 부하와 용량의 균형 |
| 용량 절약 | HEVC, 최대 1080p, 30fps, P4/HQ, lookahead 없음 | 가장 작은 기본 파일 |
| 품질 우선 | HEVC, 원본 해상도, 60fps, P6/HQ, lookahead 8, B-frame 3, middle B-ref, quarter-resolution multipass | GPU 여유가 있을 때 실시간 화질 우선 |
| 직접 설정 | FPS·코덱·해상도·인코더 강도·비트레이트 직접 선택 | 사용자가 트레이드오프 결정 |

품질 우선은 오프라인 UHQ/P7을 그대로 사용하지 않는다. 실시간 게임 녹화라는 경계를
지키기 위해 temporal AQ는 끄고 lookahead를 8프레임으로 제한했다. 1080p60 HEVC 기준
목표 비트레이트는 14.4Mbps로 높였고 해상도와 FPS에 따라 조절한다. 이 모드는 화질을 위해
GPU 여유를 소비하므로 게임 성능이 중요한 경우 게임 성능 또는 균형 프리셋을 사용해야 한다.

### 캡처와 표면 수명

- Windows 11 build 26100 이상에서는
  [`GraphicsCaptureSession.MinUpdateInterval`](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.minupdateinterval?view=winrt-26100)을
  녹화 FPS에 맞춘다. 144Hz/240Hz 화면에서 60fps 녹화에 필요하지 않은 캡처 알림을 줄인다.
- D3D11 NV12 출력 풀을 8개에서 24개로 늘렸다. 품질 모드의 lookahead와 3 B-frame 재정렬,
  애플리케이션 worker 큐를 함께 수용하고 아직 NVENC가 참조하는 텍스처의 조기 재사용을 피한다.
- 옵션 이름이나 값이 현재 FFmpeg NVENC 구현에서 거부되면 조용히 무시하지 않고 녹화 시작
  오류로 보고한다. 품질 프리셋이 실제로 적용됐는지 검증 가능하다.
- 일반 고효율(P5)은 spatial AQ만 사용하며 temporal AQ와 lookahead를 계속 끈다. 고부하 분석은
  명시적으로 선택한 품질 모드에만 들어간다.
- `avcodec_send_frame`과 packet 회수, flush를 4프레임 bounded worker로 옮겼다. worker가
  밀리면 가장 오래된 대기 프레임을 버리므로 캡처/UI 스레드가 인코더를 기다리거나 지연이
  무한히 누적되지 않는다. 중지는 worker drain → video flush → audio flush → mux trailer 순서다.
- 앱 시작 때 320×180 NV12 텍스처로 실제 D3D11 encoder-open을 검증한다. Auto는 활성 GPU의
  하드웨어 AV1 → HEVC → H.264를 먼저 사용하고, encoder-open뿐 아니라 MKV mux-open 실패도
  다음 후보로 이어진다.
- UI에는 encoder effort·codec·FPS·예상 출력 높이를 이용한 `낮음/보통/높음/매우 높음`
  GPU 압력 예측을 표시한다. 이 값은 실제 게임 benchmark가 아니라 사전 선택 안내다.

## 아직 ‘최선’이 되기 위해 필요한 작업

1. **실제 게임 프레임타임 검증**: 합성 인코더 처리량은 게임과 GPU가 경쟁할 때의 결과가
   아니다. 동일 장면에서 녹화 전/중 평균 FPS, 1% low, 프레임타임, GPU 3D/Copy/Video Encode,
   드롭률을 PresentMon/ETW로 비교해야 한다. 이번 작업에서는 사용자 요청에 따라 실행하지
   않고 예측 안내만 제공한다.
2. **객관적 화질 회귀 시험**: 움직임·텍스트·어두운 장면·입자 효과가 있는 고정 corpus를
   무손실 기준으로 캡처하고 VMAF/SSIM 및 파일 크기를 프리셋별로 비교해야 한다. 품질 프리셋의
   비트레이트와 lookahead는 이 결과로 GPU 세대별 보정해야 한다.
3. **HDR/10비트**: 현재 경로는 BGRA8 → NV12, BT.709 SDR이다. Microsoft의
   [WGC HDR 지침](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)은
   HDR에서 `R16G16B16A16_FLOAT` 캡처 또는 명시적 tone mapping을 요구한다. HEVC Main10/P010,
   색 공간과 HDR metadata까지 연결하는 별도 파이프라인이 필요하다.
4. **가변 주사율/정지 화면 정책**: build 26100의 DirtyRegion API는 조사했지만 현재 인코더는
   완전한 프레임을 요구하므로 적용하지 않았다. 정지 화면 중 중복 프레임 정책과 A/V 타임라인을
   함께 설계한 뒤 도입해야 한다.

## 검증 결과

- Debug·Release 빌드 성공 및 각각 CTest 2/2 통과.
- RTX 3070 Ti에서 HEVC NVENC 품질 모드가 모든 고급 옵션을 수락하고 320×180 60fps
  600 video packet MKV를 생성.
- bounded encoder worker 적용 후 Release Auto 선택은 RTX 3070 Ti에서 실제-open에 실패한
  AV1을 제외하고 HEVC를 선택해 10초/600 packet MKV를 생성했다.
- Release WGC + 품질 모드 HEVC worker 경로는 1920×1080 60fps, 1.130초,
  67 packet MKV를 생성했다. MP4 회귀는 65 packet MP4 생성과 MKV 제거를 확인했다.
- 강제 실패 경로는 교착 없이 종료되고 최종 MKV를 만들지 않았으며, 판독 가능한
  63 packet `.part.mkv`를 보존했다.
- 실제 게임 프레임타임, 2시간 지속 녹화 및 HDR은 별도 실기 검증 항목이다.

## 근거 자료

- [NVIDIA Video Codec SDK 13.1](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/index.html)
- [NVIDIA NVENC Programming Guide 13.1](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html)
- [NVIDIA FFmpeg Guide 13.1](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html)
- [NVIDIA NVENC Application Note 13.1](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-application-note/index.html)
- [FFmpeg NVENC implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/nvenc.c)
- [Microsoft Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [Microsoft CreateFreeThreaded](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded?view=winrt-28000)
