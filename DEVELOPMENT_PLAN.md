# OpenCapture 개발 계획서

## 1. 프로젝트 목표

OpenCapture는 Windows 10/11에서 동작하는 고성능 오픈소스 화면 녹화, 화면 캡처, GIF 녹화 및 미디어 포맷 변환 애플리케이션이다.

캡처 대상은 게임 여부에 따라 구분하지 않고 다음 세 종류로 통합한다.

1. Windows 창 선택
2. 사용자 지정 화면 영역
3. 모니터 전체

선택한 대상은 동영상 녹화, GIF 녹화 및 정지 화면 캡처에 공통으로 사용한다. 게임 프로세스의 메모리를 읽거나 DLL을 주입하고 렌더링 함수를 후킹하는 방식은 사용하지 않는다.

## 2. 핵심 요구사항

- Windows 전용 네이티브 애플리케이션으로 개발한다.
- 핵심 엔진과 UI는 C++20으로 개발한다.
- UI는 간결하게 유지하고 녹화 성능을 최우선으로 한다.
- 화면 프레임은 가능한 한 GPU 메모리 안에서 처리한다.
- NVIDIA GPU에서는 FFmpeg NVENC를 기본 인코더로 사용한다.
- NVIDIA가 아닌 환경에서는 Intel QSV, AMD AMF, Media Foundation, 소프트웨어 H.264 순서로 폴백한다.
- 창, 영역, 모니터에 대해 녹화와 스크린샷을 모두 지원한다.
- 자주 사용하는 화면 영역을 이름이 있는 프리셋으로 저장하고 스크린샷과 GIF에 즉시 재사용한다.
- 스크린샷은 파일 저장 없이 클립보드로만 복사하는 경로를 제공한다.
- 게임 플레이 및 성능 테스트 중에는 대상 게임의 FPS와 프레임 타임에 주는 영향을 최소화한다.
- 시스템 소리와 마이크를 선택적으로 녹음한다.
- 녹화 중 앱 또는 PC가 비정상 종료되어도 결과물을 최대한 복구할 수 있게 한다.

## 3. 제외 범위

- 게임 데이터 또는 리플레이 데이터 사용
- 게임 프로세스 메모리 읽기
- DLL 인젝션 및 게임 렌더링 함수 후킹
- 안티치트 우회
- Python 기반 실시간 프레임 처리
- GDI 기반 실시간 화면 녹화
- FFmpeg raw video 표준 입력을 이용한 기본 녹화 경로
- 실시간 녹화 중 매 프레임 전체 영상을 CPU 메모리로 복사하는 구조

## 4. 최종 기술 구성

| 영역 | 선택 기술 |
|---|---|
| 언어 | C++20 |
| 빌드 | CMake, CMake Presets, vcpkg |
| 컴파일러 | MSVC |
| UI | Dear ImGui, Win32, Direct3D 11 |
| 기본 캡처 | Windows Graphics Capture |
| 폴백 캡처 | DXGI Desktop Duplication |
| GPU 처리 | Direct3D 11 shader |
| 실시간 인코딩 | FFmpeg libavcodec |
| 컨테이너 기록 | FFmpeg libavformat |
| NVIDIA | H.264/HEVC/AV1 NVENC |
| Intel | H.264/HEVC/AV1 QSV |
| AMD | H.264/HEVC/AV1 AMF |
| 범용 폴백 | Media Foundation Hardware MFT, libx264 |
| 시스템 소리 | WASAPI loopback |
| 마이크 | WASAPI capture |
| 이미지 저장 | Windows Imaging Component |
| 오프라인 변환 | ffmpeg.exe |

## 5. 성능 설계 원칙

목표 영상 경로는 다음과 같다.

```text
Windows 화면
    -> D3D11 texture
    -> GPU crop/scale/color conversion
    -> FFmpeg hardware frame
    -> NVENC/QSV/AMF
    -> encoded packet
    -> FFmpeg muxer
    -> MKV/MP4
```

다음과 같은 경로는 사용하지 않는다.

```text
D3D11 texture
    -> CPU raw frame
    -> ffmpeg.exe stdin
    -> GPU 재업로드
    -> NVENC
```

후자의 경로는 NVENC를 사용하더라도 GPU 다운로드, 프로세스 간 복사 및 GPU 재업로드가 발생하기 때문이다.

추가 원칙은 다음과 같다.

- 게임과 캡처에 사용하는 GPU 어댑터를 가능한 한 일치시킨다.
- 캡처 콜백에서는 대기, 파일 쓰기 또는 동기 인코딩을 수행하지 않는다.
- 캡처, GPU 처리, 영상 인코딩, 오디오, muxing을 별도 실행 단계로 분리한다.
- 프레임 큐는 크기가 제한된 ring buffer로 구현한다.
- 처리 지연이 누적되면 게임을 방해하지 않도록 오래된 영상 프레임을 드롭한다.
- 오디오는 가능한 한 드롭하지 않고 영상 타임라인과 동기화한다.
- UI 미리보기는 기본적으로 끄고, 활성화 시 15fps 이하로 제한한다.
- 녹화 중 UI 렌더링 빈도를 낮춘다.
- 저부하 녹화 모드에서는 미리보기, 불필요한 UI 애니메이션 및 비필수 통계를 끄고 하드웨어 인코더를 우선한다.
- 게임 성능 테스트 시 녹화 전 기준 구간과 녹화 중 구간의 평균 FPS, 1% low 및 프레임 타임을 같은 조건에서 비교한다.

## 6. 통합 캡처 대상 모델

```cpp
enum class CaptureTargetType
{
    Window,
    Region,
    Monitor
};

struct CaptureTarget
{
    CaptureTargetType type{};
    HWND window{};
    HMONITOR monitor{};
    RECT region{};
};

enum class RegionAnchorType
{
    VirtualDesktop,
    WindowClient
};

struct CaptureRegionPreset
{
    std::string id;
    std::string name;
    RegionAnchorType anchorType{};
    RECT region{};
    std::wstring processName;
    std::wstring windowTitleHint;
    SIZE referenceClientSize{};
};
```

녹화, GIF 및 스크린샷은 동일한 `CaptureTarget`을 사용한다.

```cpp
recorder.Start(target, recordingOptions);
gifRecorder.Start(target, gifOptions);
screenshot.Capture(target, imageOptions);
```

### 6.1 창 선택

- 현재 표시된 일반 최상위 창을 열거한다.
- 창 제목, 프로세스 이름 및 아이콘을 표시한다.
- 게임, 브라우저, 영상 플레이어 등 애플리케이션 종류에 따른 분기를 두지 않는다.
- 선택한 창이 종료되면 녹화를 안전하게 중지한다.
- 창 크기 변경 시 캡처 프레임 풀과 GPU 리소스를 재생성한다.
- 최소화된 창을 캡처할 수 없는 경우 사용자에게 상태를 안내한다.

### 6.2 영역 선택

- 전체 가상 데스크톱에 반투명 선택 오버레이를 표시한다.
- 마우스 드래그로 영역을 선택한다.
- 좌표와 크기를 실시간 표시한다.
- 방향키로 1픽셀, Shift+방향키로 10픽셀 조정한다.
- 다중 모니터와 서로 다른 DPI 배율을 처리한다.
- 마지막 선택 영역을 저장하고 다시 사용할 수 있게 한다.
- 자주 사용하는 영역을 사용자 지정 이름으로 여러 개 저장, 수정, 복제 및 삭제할 수 있게 한다.
- 프리셋은 가상 데스크톱 절대 좌표 또는 특정 창의 클라이언트 영역 기준 상대 좌표로 저장한다.
- 브라우저 창이 이동하거나 크기가 바뀌어도 창 기준 프리셋은 현재 클라이언트 좌표에 맞춰 복원한다.
- 창 기준 프리셋의 대상 창을 찾지 못하면 자동 실행하지 않고 창 재선택을 안내한다.
- 프리셋 적용 전 실제 캡처 영역을 오버레이로 확인하고 필요하면 미세 조정할 수 있게 한다.
- 선택 영역은 CPU가 아닌 D3D11 shader로 자른다.

### 6.3 모니터 전체

- 연결된 모니터 목록과 주 모니터를 표시한다.
- 해상도, 주사율 및 HDR 여부를 표시한다.
- 모니터가 분리되면 녹화를 안전하게 종료한다.
- Windows Graphics Capture 실패 시 Desktop Duplication으로 폴백한다.

## 7. 인코더 정책

실행 시 사용 가능한 GPU와 FFmpeg encoder를 검사해 다음 순서로 자동 선택한다.

1. NVIDIA NVENC
2. Intel Quick Sync Video
3. AMD Advanced Media Framework
4. Media Foundation Hardware MFT
5. FFmpeg libx264

NVIDIA 기본 설정은 다음과 같다.

```text
기본 코덱:       H.264 NVENC
고급 코덱:       HEVC NVENC, AV1 NVENC
기본 픽셀 형식:  NV12
HDR 픽셀 형식:   P010
기본 프레임률:   60fps
키프레임 간격:   2초
품질 제어:       CQP 또는 VBR
기본 컨테이너:   MKV
```

실시간 녹화에서는 FFmpeg 라이브러리를 프로세스 안에서 사용한다. `ffmpeg.exe`는 파일 변환, remux 및 GIF 후처리에 사용한다.

## 8. 오디오 설계

```text
시스템 소리 -> WASAPI loopback --+
                                  +-> mixer -> AAC/Opus -> muxer
마이크 ------> WASAPI capture ----+
```

- 내부 오디오 포맷은 48kHz float PCM으로 통일한다.
- 시스템 소리와 마이크에 각각 볼륨 및 음소거를 제공한다.
- event-driven WASAPI shared mode를 사용한다.
- `IAudioClock`과 `QueryPerformanceCounter`를 이용해 타임스탬프를 연결한다.
- 장시간 녹화 시 오디오 resampling으로 드리프트를 보정한다.
- 녹화 중 장치가 분리되더라도 앱 전체가 종료되지 않도록 한다.

## 9. 파일 형식 및 안전성

기본 녹화는 MKV로 기록하고, 완료 후 사용자가 원하면 재인코딩 없이 MP4로 remux한다.

제공 모드는 다음과 같다.

- 안전 녹화: MKV 기록 후 MP4 자동 remux
- 직접 MP4: MP4로 바로 기록
- 원본 MKV: MKV 그대로 보관

추가 안전 기능은 다음과 같다.

- 녹화 중 임시 파일명 사용
- 정상 중지 후 최종 파일명 확정
- 녹화 시작 전 저장 공간 확인
- 디스크 쓰기 오류 감지 및 안전 종료
- 시작 시 복구 가능한 임시 파일 검색
- 동일 파일명 충돌 방지

## 10. UI 계획

UI는 하나의 간단한 메인 패널로 구성한다.

```text
+---------------------------------------+
| OpenCapture                           |
+---------------------------------------+
| 대상  (o) 창  ( ) 영역  ( ) 모니터  |
|       [ 캡처 대상 선택 ]             |
| 영역  [YouTube 영상 v] [저장/관리]  |
|                                       |
| 형식  [MP4/H.264 v]   FPS [60 v]     |
| 품질  [균형 v]        크기 [원본 v]  |
|                                       |
| [x] 시스템 소리 [ ] 마이크 [x] 커서 |
|                                       |
| [클립보드 캡처] [스크린샷] [GIF]    |
|                         [녹화 시작]  |
+---------------------------------------+
| 준비됨 / NVENC / 저장 위치           |
+---------------------------------------+
```

UI 코드는 상태 표시와 명령 전달만 담당한다.

```cpp
void MainPanel::Draw()
{
    DrawTargetSelector(state.target);
    DrawVideoOptions(state.video);
    DrawAudioOptions(state.audio);
    DrawActionButtons(state);
    DrawStatus(state.status);
}
```

Win32 메시지 처리와 Dear ImGui/D3D11 초기화는 `platform` 모듈에 격리한다. 캡처와 인코딩 모듈은 Dear ImGui에 의존하지 않는다.

## 11. 권장 프로젝트 구조

```text
OpenCapture/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg.json
|-- README.md
|-- LICENSE
|-- app/
|-- core/
|-- capture/
|-- graphics/
|-- audio/
|-- encoding/
|-- image/
|-- ui/
|-- platform/
`-- tests/
```

모듈 책임은 다음과 같다.

- `app`: 프로그램 진입점과 전체 구성
- `core`: 녹화 세션, 상태 머신, 설정 및 타임스탬프
- `capture`: WGC와 Desktop Duplication
- `graphics`: crop, scale, 색 공간 변환 및 HDR 처리
- `audio`: WASAPI 캡처, 믹싱 및 오디오 클록
- `encoding`: FFmpeg encoder, muxer 및 하드웨어 탐색
- `image`: 스크린샷 저장과 클립보드 처리
- `ui`: Dear ImGui 패널
- `platform`: Win32, D3D11, 트레이, 단축키 및 DPI 처리
- `tests`: 기능, 장시간 및 성능 테스트

## 12. 단계별 구현 계획

### 12.0 현재 기준선과 실행 우선순위

2026-07-23 중간 점검 기준으로 전체 계획은 약 40% 진행되었다. 무음 H.264 MKV 녹화 코어는 실제 동작하지만, 오디오, 스크린샷, GIF, 안전 저장 및 장시간 성능 검증은 아직 제품 수준으로 연결되지 않았다.

현재 구현된 기준선:

- CMake, Ninja, MSVC 및 vcpkg 기반 Debug/Release 빌드
- Win32/D3D11/Dear ImGui 애플리케이션
- 창, 모니터 및 화면 영역 선택과 이름이 있는 영역 프리셋
- Windows Graphics Capture와 QPC 프레임 타임스탬프
- D3D11 shader crop, scale 및 BGRA-to-NV12 변환
- H.264 NVENC, MKV muxing, 시작/중지 및 실제 활성 인코더 표시
- 인코더 자동/수동 선택과 OpenH264 최종 폴백
- 선택 대상 PNG 저장과 디스크 없는 `CF_DIBV5` 클립보드 캡처
- WASAPI 시스템·마이크 캡처, 48kHz stereo 믹싱 및 AAC MKV 오디오 트랙

다음 항목은 UI에 표시되더라도 아직 실제 기능과 연결되지 않았다.

- 커서 옵션
- MP4와 HEVC 형식 선택
- GIF 버튼
- 일시정지, 실제 복구 실행, MP4 remux 및 저부하 모드

향후 작업은 기존 단계 번호와 무관하게 다음 순서로 진행한다.

1. 현재 구현을 빌드·테스트하고 문서와 함께 체크포인트 커밋한다. (완료: `7e32e86`)
2. 가장 자주 사용하는 스크린샷 저장과 디스크 없는 `CF_DIBV5` 클립보드 캡처를 완성한다. (구현 및 자동 스모크 완료)
3. WASAPI 시스템 소리와 마이크, 오디오 인코딩 및 A/V 동기화를 구현한다. (기본 AAC MKV 경로 및 스모크 완료)
4. 임시 MKV, 저장 공간 검사, 일시정지, 복구 및 MP4 remux를 구현한다. (안전 임시 저장과 복구 파일 검색 완료)
5. 영역 프리셋 기반 GIF와 오프라인 포맷 변환을 구현한다.
6. 저부하 모드, 성능 측정, QSV/AMF, Desktop Duplication, 단축키와 트레이를 검증·완성한다.

각 마일스톤은 Debug/Release 빌드, 관련 자동 테스트 및 실제 결과물 검증을 완료해야 종료한다.

### 12.1 1단계: C++ 프로젝트 기반

작업:

- CMake 기반 C++20 프로젝트를 생성한다.
- MSVC, Windows SDK 및 x64 Release 구성을 설정한다.
- vcpkg manifest를 추가한다.
- Dear ImGui Win32/DX11 backend를 연결한다.
- Win32 창과 D3D11 device를 생성한다.
- FFmpeg 개발 라이브러리 탐색 구성을 추가한다.
- 프로젝트 README와 오픈소스 라이선스 고지를 작성한다.

완료 기준:

- x64 Debug/Release 빌드가 성공한다.
- 빈 메인 창이 실행되고 정상 종료된다.
- 사용 중인 D3D11 GPU와 FFmpeg 버전을 표시한다.
- UI와 엔진 모듈의 의존성이 분리되어 있다.

### 12.2 2단계: 통합 캡처 대상 선택

작업:

- `CaptureTarget`과 관련 옵션 모델을 구현한다.
- 모니터와 최상위 창을 열거한다.
- 창 제목, 프로세스 이름 및 아이콘을 표시한다.
- DPI aware 영역 선택 오버레이를 구현한다.
- 마지막 선택 대상을 설정 파일에 저장한다.
- 이름이 있는 영역 프리셋의 생성, 수정, 복제, 삭제 및 정렬을 구현한다.
- 화면 절대 좌표 프리셋과 창 클라이언트 기준 상대 좌표 프리셋을 구분한다.
- 브라우저 등 대상 창의 이동, 크기 및 DPI 변경 시 창 기준 프리셋을 현재 좌표로 변환한다.

완료 기준:

- 임의의 일반 Windows 창을 선택할 수 있다.
- 임의의 화면 영역과 모니터를 선택할 수 있다.
- 게임 전용 코드 분기가 없다.
- 동일 대상을 녹화와 스크린샷에 전달할 수 있다.
- 저장한 영역 프리셋을 앱 재시작 후 다시 선택할 수 있다.
- 창 기준 프리셋은 창 위치가 바뀐 뒤에도 동일한 콘텐츠 영역을 가리킨다.
- 프리셋의 대상 창을 찾지 못한 경우 잘못된 좌표를 캡처하지 않고 사용자 확인을 요청한다.

### 12.3 3단계: Windows Graphics Capture

작업:

- C++/WinRT를 초기화한다.
- D3D11 device로 `Direct3D11CaptureFramePool::CreateFreeThreaded`를 구성한다.
- 프레임을 `ID3D11Texture2D`로 수신한다.
- 창과 모니터 크기 변경을 처리한다.
- device lost와 캡처 대상 종료를 처리한다.
- 모든 프레임에 QPC 기반 타임스탬프를 부여한다.

완료 기준:

- 창, 영역 및 모니터를 동일한 캡처 엔진으로 처리한다.
- 1080p 60fps 프레임을 안정적으로 수집한다.
- 실시간 경로에서 전체 프레임 CPU 복사가 없다.
- 종료 후 모든 캡처 및 GPU 리소스가 해제된다.

### 12.4 4단계: GPU 영상 처리

작업:

- D3D11 shader 기반 crop과 scale을 구현한다.
- BGRA에서 NV12로 변환한다.
- H.264에 맞게 출력 크기를 짝수로 보정한다.
- 커서 포함 여부를 처리한다.
- HDR 감지, P010 및 HDR-to-SDR 톤매핑 기반을 마련한다.

완료 기준:

- 영역 녹화와 영역 스크린샷 좌표가 일치한다.
- 4K 입력을 GPU에서 1080p로 축소할 수 있다.
- CPU 기반 픽셀 변환을 사용하지 않는다.

### 12.5 5단계: FFmpeg NVENC

작업:

- libavcodec, libavformat 및 libavutil을 연결한다.
- FFmpeg hardware device와 NVENC encoder를 탐색한다.
- H.264 NVENC를 기본으로 구현한다.
- HEVC NVENC와 AV1 NVENC를 선택 옵션으로 추가한다.
- D3D11/CUDA 상호운용을 구성한다.
- GPU texture를 FFmpeg hardware frame으로 전달한다.
- MKV/MP4 muxer와 PTS/DTS 생성을 구현한다.
- encoder flush와 안전 종료를 처리한다.

완료 기준:

- NVIDIA 환경에서 NVENC가 자동 선택된다.
- 운영체제 GPU 모니터에서 Video Encode 사용을 확인한다.
- raw-frame stdin 파이프를 사용하지 않는다.
- 생성된 파일을 일반 플레이어와 ffprobe로 검증한다.

### 12.6 6단계: 다른 인코더 폴백

작업:

- Intel QSV와 AMD AMF를 탐색하고 연결한다.
- Media Foundation hardware MFT 폴백을 구현한다.
- libx264 최종 폴백을 구현한다.
- 실제 사용 중인 인코더를 UI와 로그에 표시한다.

완료 기준:

- NVIDIA가 없는 PC에서도 녹화할 수 있다.
- 사용할 수 없는 인코더는 자동으로 안전하게 대체된다.
- 자동 선택과 사용자 수동 선택을 모두 제공한다.

현재 구현 상태 (2026-07-23):

- H.264 후보를 정책 순서대로 실제 초기화해 보고 실패 시 다음 후보를 시도하는 자동 폴백을 연결했다.
- UI에서 자동 선택 또는 개별 H.264 인코더 수동 선택을 제공하고, 녹화 중 실제 활성 인코더를 표시한다.
- NVENC/AMF/Media Foundation의 D3D11 입력 경로와 OpenH264/libx264의 최종 CPU 호환 경로를 분리했다.
- OpenH264 폴백은 GPU NV12 staging readback 후 YUV420P로 변환하며, 하드웨어 인코더가 모두 실패한 경우에만 사용한다.
- 현재 NVIDIA 시험 PC에서 NVENC 자동 선택과 OpenH264 강제 폴백 MKV를 검증했다. Media Foundation은 등록은 확인됐지만 이 PC에서 초기화가 거부되어 OpenH264로 안전하게 넘어간다.
- Intel QSV 및 AMD AMF의 실제 하드웨어 경로는 해당 GPU가 있는 시험 환경에서 추가 검증이 필요하다.

### 12.7 7단계: WASAPI 오디오

작업:

- 시스템 소리 loopback과 마이크 capture를 구현한다.
- 장치 선택, 볼륨 및 음소거를 구현한다.
- 오디오 ring buffer와 mixer를 구현한다.
- AAC와 Opus 인코딩을 연결한다.
- 영상 타임라인과 동기화하고 장시간 드리프트를 보정한다.

완료 기준:

- 시스템 소리와 선택적 마이크가 영상에 포함된다.
- 한 시간 녹화 후 체감 가능한 싱크 어긋남이 없다.
- 오디오 장치 변경이나 분리로 앱 전체가 종료되지 않는다.

현재 구현 상태 (2026-07-23):

- 시스템 기본 출력의 WASAPI loopback과 기본 마이크 capture를 event-driven shared mode로 초기화하는 독립 모듈을 구현했다.
- 캡처 스레드, 종료 이벤트, 128패킷 제한 큐, drop-oldest 계수, 장치 mix format 및 WASAPI QPC 위치를 제공한다.
- 진단 도구에서 시스템 loopback 48kHz stereo float와 마이크 48kHz mono float 초기화를 확인했다.
- 0.5초 마이크 시험에서 51패킷, 드롭 0을 확인했다. 시스템 loopback은 시험 시 출력이 무음이어서 포맷 초기화만 확인했다.
- WASAPI float32/PCM16/PCM32 패킷을 48kHz stereo float로 정규화하고 QPC 위치를 영상 시작 시각 기준 sample PTS로 변환한다.
- 시스템 소리와 마이크를 50ms 지연 타임라인에서 믹싱하고 누락 구간은 무음으로 유지한다.
- UI의 시스템 소리와 마이크 옵션을 녹화 세션에 연결하고 FFmpeg AAC-LC 192kbps와 MKV 오디오 스트림을 구현했다.
- 합성 A/V 스모크에서 H.264 60fps와 AAC 48kHz stereo가 포함된 1.023초 MKV를 검증했다.
- 실제 WGC/WASAPI 스모크에서 1920x1080 H.264 63프레임과 AAC 49프레임을 포함한 1.066초 MKV를 검증했다.
- 장치 선택, 개별 볼륨·음소거, Opus, 장치 분리 복구 및 1시간/2시간 드리프트 검증은 남아 있다.

### 12.8 8단계: 녹화 제어와 안전한 저장

작업:

- 시작, 중지 및 일시정지 상태 머신을 구현한다.
- MKV 안전 녹화와 MP4 자동 remux를 구현한다.
- 저장 공간, 파일 충돌 및 디스크 오류를 처리한다.
- 비정상 종료 후 임시 파일 검색과 복구 안내를 구현한다.
- 게임 플레이 테스트용 저부하 녹화 모드를 구현한다.
- 저부하 모드에서는 미리보기와 비필수 UI 갱신을 끄고 캡처 및 인코딩 큐 지연을 우선 감시한다.

완료 기준:

- 반복 시작/중지에서 자원 누수가 없다.
- 일시정지 구간이 최종 타임라인에서 제거된다.
- 중지 후 파일이 정상적으로 재생된다.
- 실패 원인이 UI와 로그에 명확히 나타난다.
- 저부하 모드 활성 여부와 실제 하드웨어 인코더 사용 상태가 UI와 로그에 나타난다.
- 게임 성능이 임계치 이상 저하되면 프레임 드롭 또는 품질 조정 정책을 적용하고 경고한다.

현재 구현 상태 (2026-07-23):

- 최종 파일과 별도의 `이름.part.mkv`에 녹화하고 정상 trailer 및 파일 close 후 같은 볼륨에서 최종 `.mkv`로 rename한다.
- 녹화 시작 전에 출력 폴더의 사용 가능 공간을 확인하고 최소 512MiB 미만이면 시작하지 않는다.
- 최종 파일명 충돌 시 숫자 suffix를 사용하며 기존 최종 파일이나 미완료 파일을 덮어쓰지 않는다.
- 앱 시작 시 `Videos/OpenCapture`의 `.part.mkv` 파일을 검색하고 복구 가능 개수를 UI에 표시한다.
- 정상 종료 스모크에서 최종 H.264/AAC MKV만 남고 `.part.mkv`가 제거되는 것을 확인했다.
- 강제 실패 스모크에서 최종 파일은 생성되지 않고 ffprobe로 읽을 수 있는 H.264/AAC `.part.mkv`가 보존되는 것을 확인했다.
- 실제 복구/이름 확정 버튼, 일시정지 타임라인 제거, MP4 remux, 디스크 공간 지속 감시 및 쓰기 오류 주입 시험은 남아 있다.

### 12.9 9단계: 스크린샷

작업:

- 현재 캡처 대상에서 단일 프레임을 얻는다.
- 창, 영역 및 모니터에 동일한 대상 모델을 사용한다.
- 필요한 한 프레임만 staging texture로 복사한다.
- WIC로 PNG/JPEG를 저장한다.
- 파일 저장, 클립보드만 복사, 파일 저장 후 클립보드 복사의 세 가지 동작을 제공한다.
- 클립보드 전용 경로는 임시 이미지 파일을 만들지 않고 `CF_DIBV5` 등 Windows 클립보드 메모리 형식으로 직접 전달한다.
- 자동 파일명을 구현한다.

완료 기준:

- 선택한 영역과 저장된 이미지 좌표가 일치한다.
- PNG 해상도와 색상을 검증한다.
- 클립보드 전용 캡처 후 브라우저, 메신저 및 이미지 편집기에 바로 붙여넣을 수 있다.
- 클립보드 전용 캡처는 디스크에 파일이나 임시 파일을 남기지 않는다.
- 스크린샷 단축키를 지원한다.
- 정지 화면 저장이 실시간 녹화에 지속적인 부하를 주지 않는다.

현재 구현 상태 (2026-07-23):

- `Copy capture`, `Save PNG`, `Save + copy`의 세 동작을 메인 UI와 실제 캡처 프레임에 연결했다.
- 선택한 창, 모니터 또는 영역을 기존 D3D11 crop 경로로 처리한 뒤 한 프레임만 staging texture로 readback한다.
- WIC PNG 저장과 top-down 32-bit `CF_DIBV5` 클립보드 직접 복사를 구현했다.
- 클립보드 전용 경로는 출력 경로를 만들거나 WIC 파일 인코더를 호출하지 않는다.
- 합성 BGRA D3D11 texture를 이용한 자동 스모크에서 64x32 PNG WIC 재열기, `CF_DIBV5` 헤더 및 클립보드 전용 무파일 동작을 검증했다.
- 실제 브라우저, 메신저 및 이미지 편집기 붙여넣기와 1080p 300ms 목표 측정, 전역 단축키는 추가 검증이 필요하다.

### 12.10 10단계: GIF와 포맷 변환

작업:

- 동일 캡처 대상으로 짧은 GIF 녹화를 구현한다.
- 저장된 영역 프리셋에서 즉시 GIF 녹화를 시작할 수 있게 한다.
- FFmpeg palettegen/paletteuse를 적용한다.
- GIF FPS와 출력 해상도 제한을 제공한다.
- MP4, MKV 및 WebM 변환을 지원한다.
- remux와 재인코딩을 구분한다.
- NVENC 기반 고속 변환 옵션과 진행률 및 취소를 제공한다.

완료 기준:

- 창, 영역 및 모니터 GIF를 만들 수 있다.
- YouTube 등 창 기준 영역 프리셋으로 반복 GIF를 만들 때 캡처 위치가 일관된다.
- GIF 색상과 용량이 적절하다.
- 변환 작업이 실시간 녹화보다 낮은 우선순위로 실행된다.

### 12.11 11단계: Desktop Duplication 폴백

작업:

- Windows Graphics Capture 지원 여부와 실패를 감지한다.
- DXGI Output과 Desktop Duplication을 연결한다.
- 모니터 회전과 하드웨어 포인터를 처리한다.
- 기존 GPU 처리 및 인코더 파이프라인을 재사용한다.

완료 기준:

- WGC가 실패하는 환경에서 모니터 전체를 녹화할 수 있다.
- 자동, WGC, DXGI 캡처 방식을 설정에서 선택할 수 있다.
- 캡처 구현이 바뀌어도 인코더 코드는 변경되지 않는다.

### 12.12 12단계: 단축키와 트레이

작업:

- 녹화 시작/중지, 일시정지, 스크린샷, GIF 및 마이크 음소거 단축키를 제공한다.
- 마지막 선택 영역 재사용 단축키를 제공한다.
- 클립보드 전용 스크린샷 단축키를 별도로 제공한다.
- 즐겨찾는 영역 프리셋을 빠르게 선택하거나 바로 캡처하는 단축키를 제공한다.
- 시스템 트레이와 녹화 상태 표시를 구현한다.
- 모든 단축키를 사용자가 변경할 수 있게 한다.

완료 기준:

- 앱 창이 숨겨져 있어도 모든 주요 동작을 실행할 수 있다.
- 일반적인 게임 단축키와 충돌하면 쉽게 변경할 수 있다.
- 녹화 중 UI가 불필요한 GPU 부하를 만들지 않는다.

## 13. 성능 및 안정성 검증

### 13.1 시험 환경

- NVIDIA 단독 GPU
- Intel 내장 GPU
- AMD GPU
- 내장 GPU와 외장 GPU를 함께 사용하는 노트북
- 1080p 60Hz
- 1440p 고주사율
- 4K 60Hz
- SDR 및 HDR
- 단일 및 다중 모니터
- 일반 창, 경계 없는 전체 화면 및 모니터 전체
- 브라우저의 YouTube 영상 영역 프리셋 반복 캡처
- 개발 중인 게임의 동일 장면 또는 재현 가능한 벤치마크 구간

### 13.2 측정 항목

- 녹화 전후 대상 애플리케이션 평균 FPS
- 1% low FPS
- 캡처 및 인코딩 프레임 드롭률
- 프레임 큐 길이와 지연
- GPU Video Encode 사용률
- GPU 3D 및 Copy 사용률
- CPU 사용률
- 메모리와 GPU 메모리 증가
- 오디오/영상 동기화 오차
- 장시간 녹화 후 파일 정상성
- 기준 구간 대비 녹화 중 평균 FPS, 1% low 및 프레임 타임 변화율
- 저부하 모드에서 OpenCapture의 CPU, GPU 3D, Copy 및 Video Encode 사용량
- 클립보드 전용 캡처 지연 시간과 디스크 쓰기 발생 여부

### 13.3 초기 합격 기준

```text
1080p 60fps
- 프레임 드롭률 0.1% 미만
- NVIDIA 환경에서 NVENC 사용
- 게임 FPS 영향 목표 5% 이내
- 저부하 모드에서 1% low 저하 목표 8% 이내
- 녹화로 인한 추가 CPU 사용률 목표 5%p 이내

1440p 60fps
- 프레임 드롭률 0.5% 미만
- 캡처 및 인코딩 큐가 지속해서 증가하지 않음

4K 60fps
- 지원되는 NVIDIA GPU에서 NVENC 사용
- 전체 프레임 GPU-to-CPU 실시간 복사 없음

장시간 녹화
- 2시간 동안 지속적인 메모리 증가 없음
- 오디오/영상 오차 50ms 이내
- 녹화 중지 후 결과 파일 정상 재생

영역 프리셋 및 클립보드
- 창 기준 프리셋을 창 이동 후 적용해도 목표 영역 오차 2픽셀 이내
- 동일 프리셋의 스크린샷과 GIF가 동일한 영역을 사용
- 클립보드 전용 캡처에서 이미지 파일 및 임시 파일 생성 없음
- 1080p 영역의 클립보드 캡처 완료 목표 300ms 이내
```

FPS 영향 수치는 게임, GPU 사용률, 해상도 및 인코더 세대에 따라 달라지므로 실제 시험 결과를 함께 기록한다.

## 14. 로깅과 진단

- 세션별 로그 파일을 생성한다.
- GPU, 드라이버, 캡처 방식 및 인코더 정보를 기록한다.
- 캡처 FPS, 인코딩 FPS, 프레임 드롭 및 큐 지연을 기록한다.
- FFmpeg 오류 코드를 사람이 이해할 수 있는 메시지로 변환한다.
- 민감한 창 제목과 파일 경로는 진단 정보 내보내기 시 선택적으로 제거한다.

## 15. 라이선스 검토

- OpenCapture는 오픈소스 라이선스로 공개한다.
- Dear ImGui, FFmpeg 및 기타 의존성의 라이선스와 고지 파일을 배포물에 포함한다.
- FFmpeg 배포 빌드의 LGPL/GPL 구성 옵션을 명확하게 기록한다.
- NVIDIA Video Codec SDK 헤더 및 런타임 사용 조건을 검토한다.
- 특허가 적용될 수 있는 H.264, HEVC 및 AAC 배포 조건은 릴리스 전에 별도로 검토한다.

## 16. 최종 결정 요약

OpenCapture는 게임 전용 내부 캡처기가 아니라 모든 Windows 창, 사용자 지정 영역 및 모니터 전체를 대상으로 하는 고성능 화면 캡처 애플리케이션으로 개발한다.

핵심 구현은 C++20, Windows Graphics Capture, Direct3D 11 및 FFmpeg 하드웨어 인코딩을 사용한다. NVIDIA 환경에서는 NVENC를 기본으로 선택하며, 화면 프레임을 CPU raw video로 변환해 FFmpeg 프로세스에 전달하지 않는다. 녹화와 스크린샷은 동일한 캡처 대상과 GPU 처리 경로를 공유한다.

반복 작업을 위해 화면 절대 좌표 및 창 기준 상대 좌표 영역 프리셋을 제공하고, 저장된 영역에서 스크린샷과 GIF를 빠르게 실행한다. 스크린샷은 파일 저장 없이 클립보드로만 전달할 수 있다. 게임 플레이 및 성능 테스트에는 저부하 녹화 모드를 제공하며 평균 FPS, 1% low 및 프레임 타임 변화로 녹화 오버헤드를 검증한다.
