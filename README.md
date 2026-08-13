# OpenCapture

[English](README.en.md) | **한국어**

OpenCapture는 Windows 10/11을 위한 네이티브 화면 캡처 애플리케이션입니다. 화면 캡처부터 색상 변환, 크기 조절, 하드웨어 인코딩까지 가능한 한 GPU 안에서 처리하여 매 프레임 전체 이미지를 CPU 메모리로 복사하지 않는 고성능 녹화 경로를 목표로 합니다.

현재는 활발히 개발 중인 기술 시제품 단계입니다. 창·모니터·영역을 Windows Graphics Capture로 받아 D3D11에서 자르기와 크기 조절을 수행하고, H.264/AAC MKV·MP4, 팔레트 최적화 GIF 또는 PNG·클립보드 캡처로 출력할 수 있습니다.

최신 기술 프리뷰는 [OpenCapture v0.2.1](https://github.com/CharlieYang0040/OpenCapture/releases/tag/v0.2.1)이며,
상세 변경 및 검증 범위는 [v0.2.1 릴리스 노트](docs/releases/v0.2.1.md)에 기록합니다.

## 주요 목표

- Windows Graphics Capture와 Desktop Duplication을 상황에 맞게 사용
- D3D11 텍스처를 유지하는 GPU 중심 영상 처리
- NVIDIA NVENC, Intel Quick Sync, AMD AMF 등 FFmpeg 하드웨어 인코더 활용
- 모니터, 창, 영역 캡처와 스크린샷 지원
- WASAPI 기반 시스템·마이크 오디오 캡처 및 동기화
- 장시간 녹화에서도 메모리와 지연이 계속 증가하지 않는 제한된 파이프라인
- 캡처·그래픽·인코딩·UI를 분리하여 테스트와 유지보수가 쉬운 구조

## 현재 구현 상태

구현됨:

- CMake 및 vcpkg 기반 C++20 프로젝트
- Win32 창과 D3D11 디바이스 초기화
- Dear ImGui 사용자 인터페이스 셸
- FFmpeg 연결 상태 및 런타임 버전 표시
- 창, 모니터 및 DPI aware 화면 영역 선택
- 이름이 있는 화면 영역 프리셋 저장·적용·수정·복제·삭제
- Windows Graphics Capture 프레임 수집과 QPC 타임스탬프
- D3D11 shader 기반 crop, scale 및 BGRA-to-NV12 변환
- H.264 NVENC 실시간 인코딩과 MKV 저장
- H.264 인코더 자동/수동 선택과 OpenH264 최종 폴백
- 녹화 시작·중지와 실제 활성 인코더 상태 표시
- 선택 대상 PNG 저장, 클립보드 전용 복사 및 저장 후 복사
- 임시 파일 없이 Windows `CF_DIBV5`로 전달하는 클립보드 경로
- event-driven WASAPI 시스템 loopback·마이크 캡처 기반과 진단 도구
- 48kHz stereo 시스템·마이크 믹싱, AAC 인코딩과 MKV A/V muxing
- `.part.mkv` 안전 기록, 512MiB 저장 공간 검사 및 정상 종료 후 최종 파일 확정
- 미완료 MKV 검색과 기존 파일 충돌 방지
- FFmpeg 판독 후 미완료 MKV를 충돌 없는 최종 이름으로 복구하는 UI
- 결과 타임라인에서 정지 구간을 제거하는 녹화 일시정지·재개
- 안전 MKV를 보존하면서 만드는 H.264/AAC MP4 무재인코딩 복사본
- 영역 프리셋과 동일한 대상을 사용하는 GIF 녹화
- GIF 360p·480p·720p·1080p, 6~30fps 및 64~256색 선택
- GPU 축소와 저FPS 소스 기록, FFmpeg 2-pass palettegen/paletteuse 변환
- 30초 및 5억 픽셀 처리 예산 기반 GIF 자동 종료와 실패 시 소스 MKV 보존
- UI를 멈추지 않는 GIF 백그라운드 변환, 단계별 진행률과 취소
- 취소·앱 종료 시 `.part.gif`를 제거하고 안전 소스 MKV를 유지하는 작업 수명 관리
- 스크린샷·녹화 공용 출력 폴더와 UI 폴더 선택
- Enter 확정 후 메인 앱을 유지하고 재실행 시 모니터 연결까지 복원하는 영역 선택
- 선택 내부는 밝게 유지하고 바깥만 0~70%로 어둡게 조절하는 영역 선택 화면
- 잘린 결과를 만들지 않도록 선택 시작 모니터 안으로 제한되는 영역 좌표
- 결과를 명확히 구분한 클립보드·PNG·비디오·GIF 버튼
- 선택한 영역·창·모니터를 항상 위에 표시하는 클릭 통과 상태 테두리
- 캡처 결과에서 제외되는 파란색 대기, 노란색 캡처, 주황색 일시정지 테두리
- 대상 테두리 표시 여부, 1~12픽셀 굵기 및 20~100% 불투명도 설정
- 충돌 감지와 자동 반복 방지를 적용한 사용자 지정 캡처·비디오·GIF 전역 단축키
- 주요 버튼과 성능·용량 설정의 결과 중심 툴팁
- 100~200% 모니터 DPI를 자동 반영하고 75~200% 추가 보정을 저장하는 적응형 UI
- 단축키 캡처 결과를 클립보드, PNG 또는 둘 다로 선택하는 스크린샷 설정
- 영구 Region과 프리셋을 바꾸지 않는 `Ctrl+Shift+F8` Quick Capture
- Region 선택 중 메인 창 자동 숨김과 Capture·Video·GIF·Settings 탭 UI
- 사용자가 직접 켠 경우에만 창을 닫아도 전역 단축키가 유지되는 알림 영역 상주
- 알림 영역의 앱 열기, Quick Capture, 현재 녹화 중지 및 완전 종료 메뉴
- 신규 사용자는 창을 닫으면 완전히 종료되는 회사 PC 친화적 기본 정책
- drop-oldest 정책을 사용하는 제한 큐
- 핵심 모델 단위 테스트와 Windows GitHub Actions CI

아직 구현되지 않음:

- 오디오 장치 선택, 개별 볼륨·음소거, Opus와 장시간 드리프트 보정
- VP9/Opus를 포함한 MP4/MKV/WebM 범용 오프라인 재인코딩
- Desktop Duplication
- QSV/AMF 실기 검증과 장시간 성능 시험

전체 개발 순서와 성능 기준은 [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md)에서 확인할 수 있습니다.

## 회사 PC 보안 관련 동작

OpenCapture는 Windows 서비스, 예약 작업, 시작 프로그램을 설치하지 않으며 관리자 권한,
프로세스 주입, DLL 주입 또는 게임 후킹을 사용하지 않습니다. 사용자가 직접 실행한 프로세스
안에서 Windows Graphics Capture, 전역 단축키 및 클립보드 API를 사용합니다.

신규 사용자는 메인 창을 닫으면 앱이 완전히 종료됩니다. 백그라운드 전역 단축키가 필요한
사용자만 Settings의 `Keep running when the window is closed`를 직접 켤 수 있으며,
트레이 메뉴의 `Exit OpenCapture`로 완전히 종료할 수 있습니다. 회사 정책이 화면·오디오
캡처를 제한한다면 서명 여부와 관계없이 보안팀의 사전 승인을 받아야 합니다.

## 개발 환경 요구 사항

- Windows 10 버전 1903 이상 또는 Windows 11
- Visual Studio 2022 Build Tools 17.14 이상
  - **Desktop development with C++** 워크로드
  - MSVC v143, Windows SDK, CMake 도구
- CMake 3.21 이상
- Git for Windows
- 인터넷 연결: 최초 설정 시 vcpkg와 C++ 의존성을 내려받는 데 필요

OpenCapture 자체에는 API 키나 계정 정보가 필요하지 않습니다. 인증정보나 PC별 절대 경로는 저장소에 커밋하지 마세요.

## 배포본 설치

GitHub Releases에서 `OpenCapture-0.2.1-windows-x64.zip`과 `SHA256SUMS.txt`를 받은 뒤
SHA-256을 확인하고 ZIP 전체를 한 폴더에 압축 해제합니다. DLL과 `licenses` 폴더를
`OpenCapture.exe`와 같은 배포 구조로 유지해야 합니다. 이 기술 프리뷰는 아직
Authenticode 서명되지 않았으므로 SmartScreen이나 회사 보안 정책이 경고할 수 있습니다.
FFmpeg 대응 소스 링크, 실제 vcpkg 포트/패치와 빌드 설정은 배포 ZIP과 별도
`OpenCapture-0.2.1-ffmpeg-build-materials.zip`에 함께 제공합니다.

## 새 PC에서 빠르게 시작하기

일반 PowerShell에서 다음 명령을 실행합니다.

```powershell
git clone https://github.com/CharlieYang0040/OpenCapture.git
cd OpenCapture
powershell -ExecutionPolicy Bypass -File .\scripts\setup.ps1
```

`setup.ps1`은 다음 작업을 순서대로 수행합니다.

1. Git과 CMake가 설치되어 있는지 확인합니다.
2. 저장소에 고정된 버전의 vcpkg를 `.tools/vcpkg`에 내려받습니다.
3. vcpkg를 부트스트랩하고 `vcpkg.json`의 Dear ImGui와 FFmpeg를 설치합니다.
4. Visual Studio에 포함된 Ninja를 `.tools/ninja`에 준비합니다.
5. x64 MSVC 컴파일러와 링커 환경을 불러옵니다.
6. CMake의 Ninja 생성기로 프로젝트를 구성합니다.
7. Ninja가 MSVC 컴파일러와 링커를 호출하여 Debug 구성을 빌드하고 핵심 테스트를 실행합니다.

완료 후 실행 파일은 다음 위치에 생성됩니다.

```text
build/repo-ninja-x64-debug/OpenCapture.exe
```

Release 빌드 또는 도구만 준비하려면 다음 옵션을 사용합니다.

```powershell
.\scripts\setup.ps1 -Configuration Release
.\scripts\setup.ps1 -SkipBuild
```

검증된 배포 ZIP은 Release 빌드 후 다음 명령으로 재현합니다.

```powershell
.\scripts\package_release.ps1 -Version 0.2.1
```

### 빌드 흐름

```text
C++ 소스 코드
  ↓
CMake가 빌드 방법 구성
  ↓
vcpkg가 Dear ImGui·FFmpeg 준비
  ↓
Ninja가 컴파일 작업 실행
  ↓
MSVC가 C++ 파일 컴파일
  ↓
MSVC Linker가 EXE·DLL 연결
  ↓
OpenCapture.exe 생성
```

`setup.ps1`은 Visual Studio에 포함된 Ninja를 저장소 로컬 도구 폴더에 복사해 사용하므로 Ninja를 별도로 설치할 필요는 없습니다.

## 직접 빌드하기

### 저장소 로컬 vcpkg 사용

먼저 일반 PowerShell에서 도구를 준비합니다. 이후의 CMake 명령은 **x64 Native Tools Command Prompt for VS 2022** 또는 동일한 MSVC 환경이 설정된 터미널에서 실행합니다.

```powershell
.\scripts\setup.ps1 -SkipBuild
cmake --preset repo-ninja-x64-debug
cmake --build --preset repo-ninja-x64-debug
ctest --preset repo-ninja-x64-debug
```

Release 구성은 다음과 같습니다.

```powershell
cmake --preset repo-ninja-x64-release
cmake --build --preset repo-ninja-x64-release
```

### 전역 vcpkg 사용

이미 설치한 vcpkg가 있다면 `VCPKG_ROOT`를 설정하고 다음 프리셋을 사용합니다.

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset vs2022-x64-debug
cmake --build --preset vs2022-x64-debug
```

### 외부 의존성 없이 핵심 테스트만 실행

애플리케이션과 FFmpeg/ImGui를 제외한 핵심 라이브러리만 빠르게 검증할 수 있습니다.

```powershell
cmake --preset vs2022-core-tests
cmake --build --preset vs2022-core-tests
ctest --preset vs2022-core-tests
```

## 빌드 시스템과 의존성

- 기본 생성기: Ninja
- 기본 아키텍처: x64
- 언어 표준: C++20
- 패키지 관리자: vcpkg manifest mode
- 주요 라이브러리: Dear ImGui, FFmpeg
- Windows API: Win32, D3D11, DXGI, DWM
- 컴파일러·링커: MSVC v143

vcpkg 기준 커밋은 `vcpkg.json`과 `scripts/setup.ps1`에 고정되어 있어 다른 PC에서도 같은 의존성 기준을 사용합니다. PC별 CMake 설정은 Git에서 제외된 `CMakeUserPresets.json`에 작성할 수 있습니다.

## 프로젝트 구조

```text
OpenCapture/
├─ audio/      WASAPI 시스템·마이크 캡처와 오디오 파이프라인
├─ app/        프로그램 진입점과 객체 조립
├─ core/       캡처 대상, 세션 상태와 제한 큐
├─ encoder/    FFmpeg 인코더 탐색, D3D11 인코딩과 muxing
├─ gpu/        D3D11 crop, scale 및 색상 변환
├─ image/      WIC 스크린샷 저장과 Windows 클립보드
├─ platform/   Win32, WGC, 대상 선택과 DPI 등 플랫폼 코드
├─ ui/         Dear ImGui 화면과 명령 전달
├─ tests/      핵심 로직 자동 테스트
├─ tools/      인코더 및 통합 스모크 진단 도구
├─ scripts/    새 개발 PC 설정 스크립트
└─ .github/    GitHub Actions CI 설정
```

다음 오디오 마일스톤에서는 장치 선택, 볼륨·음소거, Opus 및 장시간 드리프트 보정을 추가합니다. UI 계층은 표시와 명령 전달만 담당하고, 캡처·이미지·인코딩 로직은 독립 모듈로 유지하는 것이 기본 원칙입니다.

## 문제 해결

### Ninja 또는 MSVC를 찾지 못하는 경우

Visual Studio Installer에서 **Desktop development with C++** 워크로드와 CMake 도구가 설치되어 있는지 확인하세요. `setup.ps1`은 해당 CMake 구성요소에 포함된 Ninja와 MSVC 개발 환경을 사용합니다. 설치 후 새 PowerShell 창을 열고 다시 실행하세요.

### 최초 설정 시간이 오래 걸리는 경우

FFmpeg를 포함한 의존성을 소스에서 준비하므로 첫 빌드는 오래 걸릴 수 있습니다. `.tools`, `build`, `vcpkg_installed`는 로컬 캐시·산출물이며 Git에 커밋되지 않습니다.

### 기존 CMake 캐시와 생성기가 충돌하는 경우

다른 생성기로 만든 동일한 빌드 디렉터리를 재사용하면 충돌할 수 있습니다. 공용 프리셋은 각각 별도 빌드 경로를 사용합니다. 직접 생성기를 바꿀 때도 새 빌드 디렉터리를 사용하세요.

## 기여 및 개발 원칙

- 변경 전후에 관련 테스트를 실행합니다.
- GPU 경로에서 불필요한 전체 프레임 CPU 복사를 추가하지 않습니다.
- 큐는 무제한으로 커지지 않도록 용량과 과부하 정책을 명시합니다.
- 빌드 산출물, 내려받은 패키지, 에디터 설정, 환경 파일과 서명 키는 커밋하지 않습니다.
- 배포 파일에는 실제 사용한 FFmpeg, Dear ImGui 및 하드웨어 SDK 고지를 포함합니다.

## 라이선스

OpenCapture 소스 코드는 [MIT License](LICENSE)로 배포됩니다. 제3자 구성요소 관련 내용은 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를 참고하세요.
