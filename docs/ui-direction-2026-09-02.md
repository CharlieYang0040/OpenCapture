# OpenCapture UI 방향 검토 (상용 외관, 게임 성능 우선)

검토일: 2026-09-02

## 결론

OpenCapture의 메인 UI는 **Dear ImGui를 유지**하고, 상용 프로그램처럼 보이게 만드는
작업은 프레임워크 교체가 아니라 **시각 체계 + 표시 정책 + Present 절약**으로 해야 한다.

WinUI 3, Qt, Slint, WebView2/Electron로 메인 캡처 패널을 다시 짜는 것은 이 앱의 목적에
맞지 않는다. 게임 플레이 중 성능 소모를 키우거나, 이미 검증된 D3D11 캡처·인코딩 경로와
경합하거나, MIT 배포와 회사 PC 제약을 복잡하게 만든다.

권장 순서는 다음과 같다.

1. **녹화 중 UI GPU를 거의 쓰지 않게 만든다.** 창이 숨겨지거나 가려지면 Present를 건너뛰고,
   보이는 상태에서도 10~15Hz로 제한한다.
2. **설정 창이 열려 있을 때도 vsync 바쁜 루프를 없앤다.** 입력이 없으면 메시지를 기다린다.
3. 그 다음에 Dear ImGui 1.92 기준으로 **Fluent에 가까운 어두운 제품 UI**를 입힌다.
4. 게임 중 상태 표시는 기존 클릭 통과 테두리와 알림 영역만 쓰고, 인게임 ImGui 오버레이는
   만들지 않는다.
5. WinUI 3는 접근성·회사 배포가 필수 요구가 될 때만 설정 창 분리용으로 남긴다.

이 판단은 [DEVELOPMENT_PLAN.md](../DEVELOPMENT_PLAN.md) 10절의 Dear ImGui 유지 결정을
뒤집지 않고, 상용 외관과 게임 성능이라는 두 목표를 같은 스택 안에서 구체화한다.

## 이 앱의 UI가 만족해야 하는 조건

OpenCapture는 일반 생산성 앱이 아니라 **게임이 쓰는 같은 GPU 위에서 동작하는 녹화기**다.

| 조건 | 의미 |
|---|---|
| GPU 경로 보호 | 캡처 프레임은 D3D11 텍스처로 남고, UI가 전체 프레임 CPU 복사나 추가 합성 장면을 만들면 안 된다. |
| 게임 프레임타임 | 녹화 중 평균 FPS, 1% low, 프레임 타임을 가능한 한 건드리지 않는다. |
| 같은 어댑터 | 게임·캡처·인코딩과 UI Present가 같은 GPU를 쓰면, UI swapchain이 3D/Copy 엔진을 뺏을 수 있다. |
| 백그라운드 기본 | 실제 사용은 전역 단축키와 트레이다. 메인 창은 설정·시작·복구용이다. |
| 보안 경계 | 서비스 설치, 관리자 권한, 게임 후킹, DLL 주입 없음. 인게임 후킹 오버레이는 제외 범위다. |
| C++20 네이티브 | 핵심 엔진과 UI를 C++로 유지한다. 별도 UI 런타임·패키징을 기본 경로에 넣지 않는다. |

따라서 “최신 Windows 앱처럼 보이게 WinUI로 재작성”은 최신처럼 보여도, 이 제품의 성능
목표와는 다른 최적화다.

## 상용 녹화기가 실제로 쓰는 패턴

2026년 LTT Labs 비교는 녹화기 자체가 프레임을 가져간다는 점을 다시 확인했다.
[Your Screen Recorder Wants to Steal Your Frames!](https://www.lttlabs.com/articles/2026/08-24/your-screen-recorder-wants-to-steal-your-frames)
에서 Xbox Game Bar는 평균 약 1fps, NVIDIA 녹화는 약 3fps, OBS는 약 5~16fps,
Streamlabs는 그보다 큰 하락을 보였다. OBS의 추가 부하는 설정 창 위젯보다
**미리보기 장면 합성**에서 크게 온다.

상용 제품의 UI 패턴은 대략 세 갈래다.

| 제품 | UI 방식 | 게임 중 실제 화면 | OpenCapture에 주는 힌트 |
|---|---|---|---|
| NVIDIA ShadowPlay / NVIDIA App | 드라이버·오버레이, 설정은 별도 앱 | 녹화 중 메인 UI를 거의 그리지 않음 | 플레이 중에는 창을 숨기고 단축키만 남긴다. |
| Xbox Game Bar | WinUI 오버레이, DWM 합성 | Win+G로만 열고, 평소에는 그리지 않음 | 상시 60fps UI Present가 아니다. |
| OBS Studio | Qt Widgets | 미리보기와 장면 합성이 GPU를 씀 | 미리보기를 기본으로 켜면 상용처럼 보여도 게임이 먼저 손해 본다. |
| Streamlabs | Electron | Chromium GPU 프로세스 | 가장 피해야 할 방향이다. |
| ShareX | WinForms | 캡처 순간에만 UI | 도구형 UX는 가볍지만 2026년 제품 외관은 약하다. |

OpenCapture가 따라가야 할 상용 기준은 OBS의 복잡한 스튜디오 화면이 아니라,
**ShadowPlay/Game Bar처럼 플레이 중에는 UI가 사라지는 제어 패널**이다.
설정 화면만 NVIDIA App이나 Windows 설정에 가깝게 다듬으면 된다.

## 프레임워크 비교

### 1. Dear ImGui + D3D11 (권장, 유지)

이미 `platform/win32_d3d11_app.cpp`의 같은 D3D11 장치에 붙어 있고, 캡처·인코딩 모듈은
ImGui에 의존하지 않는다. 추가 런타임, 별도 UI 스레드, 패키징이 없다.

2025~2026년 Dear ImGui 1.92는 동적 글꼴, DPI 배율, FreeType 래스터라이저를 제공한다.
기본 `StyleColorsDark`는 디버그 도구처럼 보이지만, 색·간격·글꼴·아이콘·카드 레이아웃을
제품 토큰으로 바꾸면 RenderDoc, Nsight, CapFrameX 급의 도구 UI가 된다. 게임 오버레이가
아니라 **닫을 수 있는 제어 패널**이라면 이 외관은 상용 도구로 충분하다.

약점인 스크린 리더와 UI Automation은 현재 기술 프리뷰 범위 밖이다. 회사 배포에서
필수 요구가 되면 설정 창만 분리하면 된다.

### 2. WinUI 3 / Windows App SDK (설정 창 후보, 메인 패널 비권장)

Build 2026 이후 Microsoft는 WinUI를 Windows 네이티브 UI의 본선으로 재확인했다.
Windows 11 설정 앱과 가장 비슷한 외관을 얻을 수 있다.

다만 OpenCapture 메인 프로세스에 넣으면 XAML 컴포지터, DComp, 가능한 C# 또는 무거운
C++/WinRT 계층이 추가된다. 캡처용 D3D11 장치와 WinUI 스왑체인을 한 프로세스에서 섞는
일은 메시지 펌프와 GPU 스케줄이 겹친다. Game Bar가 상대적으로 가벼운 이유는 WinUI가
캡처 GPU 경로를 매 프레임 그리지 않기 때문이다. 상시 열린 설정 창을 WinUI로 바꿔도
녹화 경로가 자동으로 가벼워지지는 않는다.

접근성이나 Microsoft Store 형태가 필요해지기 전에는 메인 패널 재작성 비용 대비 이득이
작다.

### 3. Qt 6 (비권장)

OBS가 증명하듯 캡처 앱에서 쓸 수는 있다. 그러나 Qt Widgets는 배포 크기가 크고,
MIT 프로젝트에 LGPL 동적 링크 의무를 더한다. Qt Quick은 더 현대적으로 보이지만
Scene Graph가 GPU를 쓴다. 이 저장소의 목표인 “GPU는 캡처와 인코딩에 남긴다”와 반대로
간다.

### 4. Slint (흥미롭지만 시기상조)

선언형 C++ UI이고, 소프트웨어 렌더러는 UI를 CPU로만 그려 GPU 드라이버 스택을 피할 수
있다. 유휴 설정 창에는 매력적이다. 그러나 트레이, 전역 단축키, Per-Monitor V2 DPI,
WGC 동의, 영역 선택 오버레이까지 이미 Win32로 구현된 앱을 Slint로 옮기는 것은
전면 재작성이다. 데스크톱 완성도와 한글·접근성도 아직 이 제품의 기본 스택으로 쓰기엔
이르다.

### 5. WebView2 / Electron (제외)

HTML/CSS로 상용 외관을 가장 빨리 만들 수 있지만, 게임 녹화기와는 충돌한다.
Chromium GPU 프로세스와 상시 컴포지터는 Streamlabs가 보여 준 경로다. 메모리와
백그라운드 전력 사용도 회사 PC 기본 정책과 맞지 않는다.

## 현재 코드에서 보이는 UI 성능 병목

시각을 바꾸기 전에, 지금 루프가 이미 게임을 방해할 수 있는 지점이 있다.

### 보이는 유휴 창이 vsync마다 Present한다

`Win32D3D11App::Run`은 트레이로 숨기거나 최소화한 뒤에만 `WaitMessage` /
`MsgWaitForMultipleObjects`로 쉰다. 창이 보이고 녹화가 아니면 매 루프 `Render()`를
호출하고 `Present(1)`로 vsync를 기다린다.

설정 패널이 모니터 한쪽에 열려 있기만 해도 UI가 디스플레이 주사율로 D3D11을 돌린다.
ImGui가 가벼운 이유는 드로우가 싸기 때문이 아니라, **그릴 일이 없을 때 GPU를 깨우지
않을 수 있기 때문**이다. 지금은 그 이득을 쓰지 않고 있다.

### 녹화 중 보이는 창은 약 60Hz UI를 유지한다

녹화 중에는 `Present(0)`으로 vsync 대기는 끈다. 그러나 창이 보이면 16ms마다
`Render()`를 호출한다. 개발 계획의 “녹화 중 UI 15fps 이하, 미리보기 기본 꺼짐”보다
높다. 같은 장치의 immediate context에서 ImGui 드로우와 `Present`가 캡처 처리와
번갈아 실행된다.

### 숨긴 녹화도 10Hz로 스왑체인을 Present한다

창이 숨겨지거나 최소화되어도 녹화 중에는 100ms마다 `Render()` → `Present(0)`이
돈다. 단축키와 안전 제한을 폴링하기 위해서다. 명령 처리는 CPU에서 하고, GPU Present는
건너뛰는 편이 맞다.

### 레거시 스왑체인

`DXGI_SWAP_EFFECT_DISCARD`는 추가 복사 가능성이 있는 예전 모델이다. 창 UI는
`FLIP_DISCARD`가 기본이어야 한다. 다만 플립 모델로 바꾼 뒤에도 유휴 Present를 줄이지
않으면 GPU 사용은 남는다.

### 기본 시각이 디버그 도구다

`ApplyUiScale`은 `ImGui::StyleColorsDark`에 배율만 곱한다. 탭 구조는 이미 있으나
카드, 제품 글꼴, 아이콘, 상태 필, 커스텀 타이틀 바가 없다. 기능은 제품에 가깝고
표면만 기술 시제품이다.

## 권장 아키텍처: 세 층으로 나누기

상용처럼 보이면서 게임 중 부하를 낮추려면, 하나의 ImGui 창이 모든 역할을 하게 두지
말고 표시 계층을 나눈다.

```text
플레이/녹화 중
  전역 단축키
  알림 영역 메뉴
  클릭 통과 대상 테두리 (기존 DWM 레이어 창)
  메인 창: 숨김. GPU Present 없음

설정/준비 중
  Dear ImGui 제어 패널 (이벤트 기반 Present)
  미리보기: 기본 꺼짐. 켜면 15fps 이하, 녹화 시작 시 자동 해제

예외 (접근성 요구가 생길 때만)
  설정 창만 WinUI 3
  캡처·인코딩·실시간 패널은 기존 C++/D3D11 유지
```

### 층 A. 세션 HUD — 이미 있는 테두리와 트레이

게임 위에는 ImGui를 올리지 않는다. 후킹이 필요하고 안티치트·회사 정책과 충돌한다.
현재 `CaptureTargetOverlay`의 파란/노란/주황 테두리와 알림 영역이 ShadowPlay식 HUD다.
여기에는 Acrylic, Mica, 애니메이션, D3D11 스왑체인을 넣지 않는다.

### 층 B. 제어 패널 — 상용 외관의 Dear ImGui

메인 창만 제품 UI로 다룬다. 레이아웃은 개발 계획 10.4의 헤더 + Capture/Video/GIF/
Settings 탭 + 상태 바를 유지하되, 기본 스타일 대신 OpenCapture 토큰을 쓴다.

권장 시각:

- 어두운 배경, 파란 대기 / 노란 캡처 / 주황 일시정지 액센트는 테두리와 맞춘다.
- 8~12px 라운딩, 카드형 그룹, 넓은 클릭 영역, 결과 중심 버튼 문구 유지.
- 글꼴은 시스템 Segoe UI Variable 또는 번들 Inter. ImGui 1.92 FreeType.
- 아이콘은 Lucide 또는 Fluent System Icons의 TTF. SVG-in-OT 컬러 폰트는 의존성 대비
  이득이 작다.
- Windows 11 다크 모드 타이틀 바(`DWMWA_USE_IMMERSIVE_DARK_MODE`)만 적용한다.
- Mica/Acrylic는 유휴 장식용으로도 기본 꺼 둔다. DWM 블러는 게임 GPU와 경쟁한다.
- ImGui 멀티 뷰포트·도킹 추가 스왑체인은 쓰지 않는다. 창이 늘어날수록 Present가 늘어난다.

미리보기를 넣더라도 녹화 중이 아닐 때만, 15fps 이하, 작은 썸네일로 제한한다.
OBS식 실시간 장면 합성은 제품 목표가 아니다.

### 층 C. 렌더 정책 — 외관보다 먼저 고칠 부분

| 상태 | 지금 | 권장 |
|---|---|---|
| 창 보임, 유휴 | 매 프레임 `Present(1)` | 입력·상태 변경이 있을 때만 Present. 그 외 `WaitMessage` / 대기 가능 스왑체인 |
| 창 보임, 녹화 | 16ms UI | 10~15Hz 상태 갱신. 미리보기·애니메이션 없음 |
| 창 숨김/최소화, 녹화 | 10Hz `Render`+`Present` | 파이프라인 펌프만. ImGui/Present 생략. 단축키는 메시지 경로 |
| 트레이 유휴 | `WaitMessage` | 유지 |
| 스왑체인 | `DISCARD` | `FLIP_DISCARD`. 녹화 중 vsync 대기 없음은 유지 |

숨긴 창에서 Present를 생략하는 것이 이번 검토에서 가장 비용 대비 큰 성능 작업이다.
테마를 아무리 바꿔도, 보이지 않는 스왑체인을 10~60Hz로 제출하면 게임이 먼저 손해 본다.

## 구현 단계

프레임워크를 바꾸지 않고도 제품처럼 보이게 만들 수 있다. 성능 작업을 시각 작업보다
앞에 둔다.

### 1단계. Present 절약 (성능, 시각 변화 없음)

- 유휴 표시 창: dirty flag. 마우스, 키보드, DPI, 트레이 명령, 상태 문자열 변경 시에만
  `Render()`.
- 녹화 중 숨김: `PumpRealtimePipeline()`만 돌리고 `ImGui_ImplDX11_RenderDrawData`와
  `Present`를 호출하지 않는다.
- 녹화 중 표시: UI를 10Hz로 낮춘다. 개발 계획의 15fps 상한을 코드 기본값으로 만든다.
- `DXGI_SWAP_EFFECT_FLIP_DISCARD`로 전환하고 occluded 상태를 구분해 Present를 건너뛴다.

수용 기준: 메인 창을 숨긴 채 게임 성능 프로필로 녹화할 때, UI 프로세스의 3D 엔진
사용이 캡처 Copy/Video Encode에 비해 무시할 수준이어야 한다. 같은 장면에서 창을 연
60Hz UI와 숨긴 경로의 게임 1% low를 비교한다.

### 2단계. 제품 시각 체계

- `StyleColorsDark`를 OpenCapture 토큰으로 교체한다. 색, 패딩, 라운딩, 구분선.
- FreeType 글꼴과 아이콘 폰트. 기존 DPI·사용자 배율 모델은 유지하고 스타일에 반복
  곱하지 않는 규칙도 유지한다.
- 헤더를 상태 필(준비됨/녹화 중/일시정지)과 큰 1차 동작 버튼 중심으로 재배치한다.
- Settings는 접이식 카드, 고급 진단은 기본 접힘.

수용 기준: 기본 테마를 모르는 사용자가 디버그 빌드가 아니라 설치한 도구로 인식할 것.
기능 회귀 없이 기존 탭·명령 구조 유지.

### 3단계. 녹화 중 UX를 상용 기본값에 맞춤

- 녹화 시작 시 메인 창을 숨기고 트레이로 보내는 옵션을 기본 추천으로 둔다.
  회사 PC 기본값인 “닫으면 종료”는 유지하고, 이 동작은 사용자가 백그라운드 상주를
  켠 경우에만 쓴다.
- 보이는 창이 필요하면 전체 탭 대신 얇은 상태 스트립(시간, 드롭, 중지)만 그린다.
- 미리보기는 명시적으로 켠 준비 화면에만 둔다.

### 4단계. 나중 — 필요할 때만

- 스크린 리더/UI Automation이 배포 조건이 되면 설정 창만 WinUI 3 unpackaged.
- 메인 실시간 경로, WGC, 인코더, 영역 선택 오버레이는 C++/D3D11에 남긴다.

## 명시적으로 하지 않을 것

- 메인 UI를 WinUI, Qt, Slint, WPF, Avalonia, Flutter로 재작성
- WebView2 또는 Electron 셸
- 게임 프로세스에 ImGui/오버레이를 주입
- 녹화 중 60fps 라이브 미리보기
- 녹화 중 Mica, Acrylic, 블러, 레이아웃 애니메이션
- ImGui 도킹/멀티 뷰포트로 창을 여러 개 띄워 Present를 늘리는 것
- UI 미리보기를 위해 캡처 프레임을 매 프레임 CPU로 읽는 경로

## 수용 기준 요약

제품처럼 보이면서도 게임 성능을 지키는 UI의 성공 조건은 외관 스크린샷이 아니라
아래 측정이다.

1. 트레이 또는 숨김 녹화에서 UI Present가 사실상 0.
2. 보이는 유휴 설정 창에서 입력 없을 때 GPU 3D 사용이 주사율 Present 대비 크게 감소.
3. 보이는 녹화 창은 15fps 이하. 미리보기 기본 꺼짐.
4. 캡처·인코딩 모듈은 계속 Dear ImGui에 의존하지 않음.
5. 기존 단축키, 트레이, 테두리, DPI 배율, 회사 PC 종료 기본값을 유지.

1~3을 만족한 뒤에야 테마와 타이포그래피가 상용 인상을 만든다. 순서를 바꾸면
예쁜 60Hz 설정 창이 게임 프레임을 가져가는 시제품이 된다.

## 2026-09-02 구현 상태

코드에 반영한 항목:

- `DispatchUi` / `PresentUi` 분리. 유휴 창은 메시지·호버가 있을 때만 Present
- 녹화 중 표시 창 10Hz, 숨김/최소화 녹화는 ImGui Present 생략
- `DXGI_SWAP_EFFECT_FLIP_DISCARD`, occluded 시 Present 생략
- OpenCapture 다크 테마, 카드 레이아웃, 헤더 상태 필, 프로필 카드
- 한국어/영어 테이블과 런타임 전환. 기본값은 Windows UI 언어
- MSVC `/utf-8`로 UTF-8 문자열 테이블을 그대로 컴파일
- Segoe UI + Malgun Gothic 병합. 배포본에 TTF를 넣지 않음
- 제목+본문 툴팁(지연 호버, 키보드 포커스)
- Settings 고급/진단 기본 접힘. 모양 설정은 슬라이더를 놓으면 즉시 저장

아직 실기 측정이 남은 항목:

- 숨김 녹화 vs 60Hz 설정 창의 게임 1% low 비교
- 4K 150%/200%에서 한글 글꼴 선명도

