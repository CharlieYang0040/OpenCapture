#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace opencapture {

enum class Language { English, Korean };

#define OPENCAPTURE_STRINGS(X) \
    X(app_name, "OpenCapture", "OpenCapture") \
    X(language_ko_short, "한", "한") \
    X(language_en_short, "EN", "EN") \
    X(language_label, "Language", "언어") \
    X(language_english, "English", "English") \
    X(language_korean, "한국어", "한국어") \
    X(tooltip_language_title, "Interface language", "인터페이스 언어") \
    X(tooltip_language_body, \
        "Switch the OpenCapture interface between English and Korean. Window titles and saved preset names keep the original text.", \
        "OpenCapture 화면을 영어와 한국어로 바꿉니다. 창 제목과 저장한 프리셋 이름은 원래 글자를 유지합니다.") \
    X(status_ready, "Ready", "준비됨") \
    X(status_starting, "Starting...", "시작 중...") \
    X(status_recording, "Recording", "녹화 중") \
    X(status_paused, "Paused", "일시정지") \
    X(status_error, "Error", "오류") \
    X(status_gif, "GIF recording", "GIF 녹화") \
    X(status_converting, "Converting GIF...", "GIF 변환 중...") \
    X(header_target, "Target", "대상") \
    X(header_output, "Output", "출력") \
    X(header_change_target, "Change target", "대상 변경") \
    X(header_quick_capture, "Quick Capture", "빠른 캡처") \
    X(tooltip_quick_capture_title, "One-time region screenshot", "일회성 영역 스크린샷") \
    X(tooltip_quick_capture_body, \
        "Select a temporary area and capture it without changing the saved window, region, or monitor target. Uses the screenshot shortcut result below.", \
        "저장된 창·영역·모니터 대상을 바꾸지 않고 임시 영역만 잡아 캡처합니다. 아래의 스크린샷 단축키 결과 설정을 따릅니다.") \
    X(tooltip_change_target_title, "Choose capture source", "캡처 대상 선택") \
    X(tooltip_change_target_body, \
        "Screenshots, video, and GIF all use this same target. Pick a window, a rectangle, or a whole monitor.", \
        "스크린샷, 비디오, GIF가 모두 이 대상을 사용합니다. 창, 사각형 영역, 모니터 전체 중에서 고릅니다.") \
    X(pause, "Pause", "일시정지") \
    X(resume, "Resume", "재개") \
    X(stop_video, "Stop video", "비디오 중지") \
    X(stop_gif, "Stop GIF", "GIF 중지") \
    X(tooltip_pause_title, "Pause recording", "녹화 일시정지") \
    X(tooltip_pause_body, \
        "Stops writing video and audio into the current file without finalizing it. Resume continues the same recording. Paused time is removed from the output timeline.", \
        "파일을 닫지 않고 영상과 소리 기록만 멈춥니다. 재개하면 같은 파일에 이어집니다. 일시정지한 시간은 결과 타임라인에서 빠집니다.") \
    X(tooltip_resume_title, "Resume recording", "녹화 재개") \
    X(tooltip_resume_body, \
        "Continue the current recording in the same output file.", \
        "같은 출력 파일에 현재 녹화를 이어서 기록합니다.") \
    X(tooltip_stop_video_title, "Finish video file", "비디오 파일 완료") \
    X(tooltip_stop_video_body, \
        "Finalize the current video so a media player can open it. A safe .part.mkv file is renamed only after the recording closes cleanly.", \
        "미디어 플레이어가 열 수 있도록 현재 비디오를 마무리합니다. 안전 .part.mkv 파일은 정상 종료 후에만 최종 이름으로 바뀝니다.") \
    X(tooltip_stop_gif_title, "Stop and create GIF", "중지 후 GIF 만들기") \
    X(tooltip_stop_gif_body, \
        "Stop recording, keep the safe MKV source, and create a palette-optimized GIF in the background.", \
        "녹화를 멈추고 안전 MKV 소스를 유지한 뒤, 팔레트 최적화 GIF를 백그라운드에서 만듭니다.") \
    X(tab_capture, "Capture", "캡처") \
    X(tab_video, "Video", "비디오") \
    X(tab_gif, "GIF", "GIF") \
    X(tab_settings, "Settings", "설정") \
    X(capture_step_target, "1. Choose what to capture", "1. 캡처할 대상을 고르세요") \
    X(target_window, "Window", "창") \
    X(target_region, "Region", "영역") \
    X(target_monitor, "Monitor", "모니터") \
    X(tooltip_window_title, "Capture one window", "창 하나 캡처") \
    X(tooltip_window_body, \
        "Records a visible top-level window. The outline follows the window if it moves, resizes, or changes DPI.", \
        "화면에 보이는 일반 창을 캡처합니다. 창을 옮기거나 크기를 바꾸거나 DPI가 바뀌어도 테두리가 따라갑니다.") \
    X(tooltip_region_title, "Capture a rectangle", "사각형 영역 캡처") \
    X(tooltip_region_body, \
        "Records only the selected desktop rectangle. The area stays outlined. Dragging across monitors is limited to the starting monitor so the result is not clipped.", \
        "선택한 화면 사각형만 캡처합니다. 영역은 테두리로 표시됩니다. 모니터를 가로지르는 선택은 시작 모니터 안으로 제한되어 잘린 결과가 나오지 않습니다.") \
    X(tooltip_monitor_title, "Capture a whole monitor", "모니터 전체 캡처") \
    X(tooltip_monitor_body, \
        "Records one entire display, including the taskbar unless you crop with a region instead.", \
        "작업 표시줄을 포함한 모니터 전체를 캡처합니다. 일부를 자르려면 영역 캡처를 사용하세요.") \
    X(select_capture_target, "Select capture target", "캡처 대상 선택") \
    X(refresh, "Refresh", "새로고침") \
    X(tooltip_select_target_title, "Open the target picker", "대상 선택 화면 열기") \
    X(tooltip_select_target_body, \
        "For a window or monitor, pick from the current list. For a region, the main window hides and you drag a rectangle, then press Enter.", \
        "창이나 모니터는 목록에서 고릅니다. 영역은 메인 창을 숨긴 뒤 사각형을 드래그하고 Enter로 확정합니다.") \
    X(tooltip_refresh_title, "Refresh window and monitor lists", "창·모니터 목록 새로고침") \
    X(tooltip_refresh_body, \
        "Rebuilds the list of currently visible windows and attached displays.", \
        "지금 보이는 창과 연결된 디스플레이 목록을 다시 만듭니다.") \
    X(selected_prefix, "Selected", "선택됨") \
    X(region_presets, "Saved regions", "저장한 영역") \
    X(choose_saved_region, "Choose a saved region", "저장한 영역 선택") \
    X(preset_window_tag, "Window", "창") \
    X(preset_desktop_tag, "Desktop", "바탕화면") \
    X(apply_preset, "Apply", "적용") \
    X(save_current, "Save current", "현재 영역 저장") \
    X(delete_preset, "Delete", "삭제") \
    X(rename, "Rename", "이름 변경") \
    X(duplicate, "Duplicate", "복제") \
    X(new_name, "New name", "새 이름") \
    X(name, "Name", "이름") \
    X(save, "Save", "저장") \
    X(cancel, "Cancel", "취소") \
    X(desktop_coordinates, "Desktop coordinates", "바탕화면 좌표") \
    X(window_relative, "Window-relative", "창 기준") \
    X(anchor_window, "Anchor window", "기준 창") \
    X(no_window_available, "No window available", "사용 가능한 창 없음") \
    X(choose_visible_window, "Choose a visible top-level window", "보이는 최상위 창을 고르세요") \
    X(choose_a_monitor, "Choose a monitor", "모니터를 고르세요") \
    X(primary_monitor, "Primary", "주 모니터") \
    X(popup_capture_target, "Capture target", "캡처 대상") \
    X(popup_save_region, "Save region preset", "영역 프리셋 저장") \
    X(popup_rename_region, "Rename region preset", "영역 프리셋 이름 변경") \
    X(tooltip_apply_preset_title, "Use this saved region now", "이 저장 영역을 바로 사용") \
    X(tooltip_apply_preset_body, \
        "Sets the current capture target to the selected saved region without changing other presets.", \
        "다른 프리셋은 그대로 두고, 선택한 저장 영역을 현재 캡처 대상으로 만듭니다.") \
    X(tooltip_save_preset_title, "Save the current region", "현재 영역 저장") \
    X(tooltip_save_preset_body, \
        "Keep this rectangle for browser or app captures you repeat. Desktop coordinates stay on the screen; window-relative follows a window's client area.", \
        "반복하는 브라우저·앱 캡처를 위해 이 사각형을 저장합니다. 바탕화면 좌표는 화면에 고정되고, 창 기준은 창 클라이언트 영역을 따라갑니다.") \
    X(tooltip_delete_preset_title, "Delete saved region", "저장 영역 삭제") \
    X(tooltip_delete_preset_body, \
        "Removes the selected preset. Captured PNG, video, and GIF files are not deleted.", \
        "선택한 프리셋만 지웁니다. 이미 만든 PNG, 비디오, GIF 파일은 삭제되지 않습니다.") \
    X(capture_step_screenshot, "2. Take a screenshot", "2. 스크린샷을 찍으세요") \
    X(screenshot_intro, "Capture one still image from the selected target.", "선택한 대상에서 정지 이미지를 한 장 캡처합니다.") \
    X(shortcut_result, "Screenshot shortcut result", "스크린샷 단축키 결과") \
    X(clipboard_only, "Clipboard only", "클립보드만") \
    X(save_png_file, "Save PNG file", "PNG 파일 저장") \
    X(save_png_clipboard, "Save PNG + clipboard", "PNG 저장 + 클립보드") \
    X(copy_to_clipboard, "Copy to clipboard", "클립보드로 복사") \
    X(save_png_button, "Save PNG file", "PNG 파일 저장") \
    X(save_png_copy, "Save PNG + copy", "PNG 저장 + 복사") \
    X(tooltip_shortcut_result_title, "What the screenshot shortcut does", "스크린샷 단축키가 하는 일") \
    X(tooltip_shortcut_result_body, \
        "Used by the selected-target screenshot shortcut and Quick Capture. The three buttons below always perform their labeled result, regardless of this setting.", \
        "선택한 대상 스크린샷 단축키와 빠른 캡처에 적용됩니다. 아래 세 버튼은 이 설정과 관계없이 버튼에 적힌 결과만 실행합니다.") \
    X(tooltip_copy_title, "Clipboard only", "클립보드만") \
    X(tooltip_copy_body, \
        "Copies the screenshot to the clipboard. No PNG file is created.", \
        "스크린샷을 클립보드에만 복사합니다. PNG 파일은 만들지 않습니다.") \
    X(tooltip_png_title, "Save a PNG", "PNG 저장") \
    X(tooltip_png_body, \
        "Writes a PNG into the shared output folder and leaves the clipboard unchanged.", \
        "공용 출력 폴더에 PNG를 저장하고 클립보드는 바꾸지 않습니다.") \
    X(tooltip_png_copy_title, "Save and copy", "저장과 복사") \
    X(tooltip_png_copy_body, \
        "Saves a PNG in the output folder and also copies the image to the clipboard.", \
        "출력 폴더에 PNG를 저장하고 이미지도 클립보드에 복사합니다.") \
    X(video_profiles, "Recording profile", "녹화 프로필") \
    X(profile_game, "Game performance", "게임 성능") \
    X(profile_balanced, "Balanced", "균형") \
    X(profile_small, "Small file", "용량 절약") \
    X(profile_quality, "Quality first", "품질 우선") \
    X(profile_custom, "Custom", "직접 설정") \
    X(profile_game_detail, "1080p60 H.264 · lowest game impact · widest playback support", \
        "1080p60 H.264 · 게임 부하 최소 · 재생 호환 최우선") \
    X(profile_balanced_detail, "1080p60 Auto codec · moderate GPU work", \
        "1080p60 Auto 코덱 · GPU 부하 보통") \
    X(profile_small_detail, "1080p30 Auto codec · smallest preset · no heavy lookahead", \
        "1080p30 Auto 코덱 · 가장 작은 기본값 · 무거운 lookahead 없음") \
    X(profile_quality_detail, "Source 60 fps Auto codec · extra encoder analysis · needs spare GPU capacity", \
        "원본 60fps Auto 코덱 · 인코더 분석 추가 · GPU 여유가 필요할 때") \
    X(profile_custom_detail, "Direct settings are active. Higher effort can reduce game performance.", \
        "직접 설정이 활성화되었습니다. 인코더 강도를 높이면 게임 성능이 떨어질 수 있습니다.") \
    X(tooltip_profile_title, "Choose one recording goal", "녹화 목적을 하나 고르세요") \
    X(tooltip_profile_body, \
        "A profile sets FPS, codec, resolution, and encoder effort together. Game performance is the safest default while playing. Custom unlocks the advanced encoding controls.", \
        "프로필은 FPS, 코덱, 해상도, 인코더 강도를 함께 맞춥니다. 게임 중에는 게임 성능이 가장 안전합니다. 직접 설정은 고급 인코딩 항목을 엽니다.") \
    X(resolution, "Resolution", "해상도") \
    X(resolution_source, "Source resolution", "원본 해상도") \
    X(resolution_1080, "Maximum 1080p", "최대 1080p") \
    X(resolution_720, "Maximum 720p", "최대 720p") \
    X(codec, "Codec", "코덱") \
    X(codec_auto, "Auto (AV1 > HEVC > H.264)", "Auto (AV1 > HEVC > H.264)") \
    X(codec_h264, "H.264", "H.264") \
    X(codec_hevc, "HEVC", "HEVC") \
    X(codec_av1, "AV1", "AV1") \
    X(tooltip_codec_title, "Video codec", "비디오 코덱") \
    X(tooltip_codec_body, \
        "Auto tries AV1, then HEVC, then H.264 on the active GPU. An explicit codec falls back only when 'Allow fallback' is enabled. HEVC and AV1 need a player or site that supports them.", \
        "Auto는 활성 GPU에서 AV1, HEVC, H.264 순으로 시도합니다. 코덱을 직접 고르면 '호환 코덱으로 폴백'이 켜져 있을 때만 다른 코덱으로 넘어갑니다. HEVC와 AV1은 지원하는 재생기나 사이트가 필요합니다.") \
    X(codec_unavailable, "This codec is not available on the active GPU. Enable fallback or choose another codec.", \
        "이 코덱은 현재 GPU에서 사용할 수 없습니다. 폴백을 켜거나 다른 코덱을 고르세요.") \
    X(format, "Format", "형식") \
    X(format_mkv, "MKV (recommended)", "MKV (권장)") \
    X(format_mp4, "MP4 after recording", "녹화 후 MP4") \
    X(tooltip_format_title, "Container format", "컨테이너 형식") \
    X(tooltip_format_body, \
        "MKV is written safely while recording. Choose MP4 after recording to remux without re-encoding. The MKV is removed only after the MP4 succeeds.", \
        "녹화 중에는 MKV를 안전하게 기록합니다. 녹화 후 MP4는 재인코딩 없이 변환합니다. MKV는 MP4가 성공한 뒤에만 삭제됩니다.") \
    X(mp4_removes_mkv, "The safe MKV is removed only after the MP4 is completed successfully.", \
        "안전 MKV는 MP4가 성공적으로 끝난 뒤에만 삭제됩니다.") \
    X(fps, "FPS", "FPS") \
    X(tooltip_fps_title, "Frames per second", "초당 프레임") \
    X(tooltip_fps_body, \
        "Higher FPS makes motion smoother but increases encoder load, GPU pressure, and file size. 60 is the usual gameplay default.", \
        "FPS를 높이면 움직임은 부드러워지지만 인코더 부하, GPU 압력, 파일 크기가 커집니다. 게임 녹화는 보통 60입니다.") \
    X(advanced_encoding, "Advanced encoding", "고급 인코딩") \
    X(encoder_backend, "Encoder backend", "인코더") \
    X(encoder_auto, "Auto (recommended)", "Auto (권장)") \
    X(tooltip_encoder_title, "Hardware encoder", "하드웨어 인코더") \
    X(tooltip_encoder_body, \
        "Auto selects the best available hardware encoder on this GPU. A manual choice is mainly for compatibility testing.", \
        "Auto는 이 GPU에서 쓸 수 있는 가장 적합한 하드웨어 인코더를 고릅니다. 수동 선택은 호환성 확인용입니다.") \
    X(encoder_effort, "Encoder effort", "인코더 강도") \
    X(effort_realtime, "Realtime (lowest game impact)", "실시간 (게임 영향 최소)") \
    X(effort_balanced, "Balanced", "균형") \
    X(effort_efficient, "Efficient (more encoder work)", "고효율 (인코더 작업 증가)") \
    X(effort_quality, "Quality (highest encoder work)", "품질 (인코더 작업 최대)") \
    X(tooltip_effort_title, "How hard the encoder works", "인코더가 얼마나 일을 할지") \
    X(tooltip_effort_body, \
        "Realtime protects game responsiveness. Efficient uses a stronger preset without lookahead. Quality uses extra analysis, a short lookahead, B-frames, and two-pass work. Quality can hitch GPU-bound games.", \
        "실시간은 게임 반응을 지킵니다. 고효율은 lookahead 없이 더 강한 프리셋입니다. 품질은 추가 분석, 짧은 lookahead, B-frame, 2-pass를 씁니다. 품질 모드는 GPU가 바쁜 게임을 끊기게 할 수 있습니다.") \
    X(custom_bitrate, "Use a custom target bitrate", "대상 비트레이트 직접 지정") \
    X(target_bitrate, "Target bitrate", "대상 비트레이트") \
    X(allow_fallback, "Allow fallback to a compatible codec", "호환 코덱으로 폴백 허용") \
    X(color_pipeline, "Color pipeline: SDR BT.709 8-bit (HDR/10-bit recording is not enabled)", \
        "색 공간: SDR BT.709 8비트 (HDR/10비트 녹화는 아직 없습니다)") \
    X(tooltip_hdr_title, "HDR is not recorded yet", "HDR은 아직 녹화되지 않습니다") \
    X(tooltip_hdr_body, \
        "HDR capture needs a separate float-to-P010/Main10 path and HDR metadata. The current BGRA8-to-NV12 path records SDR BT.709.", \
        "HDR 캡처는 별도의 float→P010/Main10 경로와 HDR 메타데이터가 필요합니다. 지금은 BGRA8→NV12 SDR BT.709만 기록합니다.") \
    X(estimate_prefix, "Estimate", "예상") \
    X(estimate_suffix, "Actual size varies with screen content and the selected encoder.", \
        "실제 용량은 화면 내용과 선택한 인코더에 따라 달라집니다.") \
    X(gpu_pressure_low, "Predicted GPU pressure: Low | best choice while gaming", \
        "예측 GPU 압력: 낮음 | 게임 중 가장 적합") \
    X(gpu_pressure_moderate, "Predicted GPU pressure: Moderate | normally suitable for gameplay", \
        "예측 GPU 압력: 보통 | 일반적인 게임에 적합") \
    X(gpu_pressure_high, "Predicted GPU pressure: High | reduce effort if the game is GPU-bound", \
        "예측 GPU 압력: 높음 | 게임이 GPU에 막히면 강도를 낮추세요") \
    X(gpu_pressure_very_high, "Predicted GPU pressure: Very high | use only with spare GPU capacity", \
        "예측 GPU 압력: 매우 높음 | GPU 여유가 있을 때만 사용") \
    X(tooltip_pressure_title, "Predicted GPU pressure", "예측 GPU 압력") \
    X(tooltip_pressure_body, \
        "This is guidance from encoder effort, codec, FPS, and output height. It is not a measured game benchmark. Compare average FPS and 1% lows with and without recording if you need proof on your PC.", \
        "인코더 강도, 코덱, FPS, 출력 높이로 계산한 안내입니다. 실제 게임 벤치마크가 아닙니다. 확인이 필요하면 같은 장면에서 녹화 전후 평균 FPS와 1% low를 비교하세요.") \
    X(system_audio, "System audio", "시스템 소리") \
    X(microphone, "Microphone", "마이크") \
    X(cursor_always, "Cursor (always included)", "커서 (항상 포함)") \
    X(tooltip_system_audio_title, "Record application sound", "앱 소리 녹음") \
    X(tooltip_system_audio_body, \
        "Includes sound played by Windows applications in video recordings. GIF never includes audio.", \
        "Windows 앱이 재생하는 소리를 비디오에 넣습니다. GIF에는 오디오가 없습니다.") \
    X(tooltip_microphone_title, "Record the default microphone", "기본 마이크 녹음") \
    X(tooltip_microphone_body, \
        "Includes the default microphone in video recordings. Per-device volume controls are not in this version. GIF never includes audio.", \
        "기본 마이크를 비디오에 넣습니다. 장치별 볼륨은 이 버전에 없습니다. GIF에는 오디오가 없습니다.") \
    X(tooltip_cursor_title, "Mouse pointer", "마우스 포인터") \
    X(tooltip_cursor_body, \
        "Windows Graphics Capture currently includes the pointer. On/off control will be enabled only after the capture engine supports and verifies it.", \
        "Windows Graphics Capture는 현재 포인터를 포함합니다. 켜고 끄는 옵션은 캡처 엔진이 지원하고 검증된 뒤에만 제공합니다.") \
    X(restore_video_defaults, "Restore Default", "기본값 복원") \
    X(tooltip_restore_video_title, "Reset video settings", "비디오 설정 초기화") \
    X(tooltip_restore_video_body, \
        "Restores the 1080p60 H.264 game-performance profile, MKV, system audio on, and microphone off.", \
        "1080p60 H.264 게임 성능 프로필, MKV, 시스템 소리 켜짐, 마이크 꺼짐으로 되돌립니다.") \
    X(start_video, "Start video recording", "비디오 녹화 시작") \
    X(tooltip_start_video_title, "Start recording", "녹화 시작") \
    X(tooltip_start_video_body, \
        "Starts recording the selected target with the profile, audio, and format above. Hide or minimize this window to keep UI GPU use near zero while you play.", \
        "위에서 고른 프로필, 오디오, 형식으로 선택 대상을 녹화합니다. 게임 중 UI GPU 사용을 줄이려면 이 창을 숨기거나 최소화하세요.") \
    X(tooltip_start_video_busy_title, "Wait for conversion", "변환이 끝날 때까지 대기") \
    X(tooltip_start_video_busy_body, \
        "Wait for the current GIF conversion to finish or cancel it first.", \
        "진행 중인 GIF 변환이 끝나거나 취소된 뒤에 시작할 수 있습니다.") \
    X(convert_to_mp4, "Convert to MP4", "MP4로 변환") \
    X(tooltip_remux_title, "Remux without re-encoding", "재인코딩 없이 변환") \
    X(tooltip_remux_body, \
        "Copies the last MKV into MP4 without encoding again, then removes the MKV after success.", \
        "마지막 MKV를 다시 인코딩하지 않고 MP4로 복사한 뒤, 성공하면 MKV를 삭제합니다.") \
    X(gif_intro, "Lower resolution, FPS, and color count create much smaller GIF files. Recording stops automatically at 30 seconds or the safe pixel budget.", \
        "해상도, FPS, 색상 수를 낮출수록 GIF가 작아집니다. 30초 또는 안전 픽셀 예산에 도달하면 녹화가 자동으로 멈춥니다.") \
    X(gif_fps, "GIF FPS", "GIF FPS") \
    X(gif_resolution, "GIF resolution", "GIF 해상도") \
    X(gif_colors, "GIF colors", "GIF 색상") \
    X(tooltip_gif_fps_title, "GIF frame rate", "GIF 프레임 속도") \
    X(tooltip_gif_fps_body, \
        "Lower FPS greatly reduces GIF size. 12 fps is the recommended default for sharing. GIF never includes audio.", \
        "FPS를 낮추면 GIF 용량이 크게 줄어듭니다. 공유용 기본값은 12fps입니다. GIF에는 소리가 없습니다.") \
    X(tooltip_gif_res_title, "GIF height limit", "GIF 높이 제한") \
    X(tooltip_gif_res_body, \
        "Limits output height while preserving aspect ratio. 720p or lower is much smaller than 1080p.", \
        "가로세로 비율을 유지한 채 출력 높이를 제한합니다. 720p 이하가 1080p보다 훨씬 작습니다.") \
    X(tooltip_gif_colors_title, "Palette size", "팔레트 크기") \
    X(tooltip_gif_colors_body, \
        "Fewer palette colors reduce file size but may introduce visible banding in gradients.", \
        "팔레트 색을 줄이면 파일은 작아지지만 그라데이션에 띠가 보일 수 있습니다.") \
    X(gif_warning_large, "Large GIF warning: use 720p / 12 fps or lower for sharing.", \
        "큰 GIF 경고: 공유용으로는 720p / 12fps 이하를 사용하세요.") \
    X(restore_gif_defaults, "Restore Default", "기본값 복원") \
    X(tooltip_restore_gif_title, "Reset GIF settings", "GIF 설정 초기화") \
    X(tooltip_restore_gif_body, "Restore 12 fps, 720p, and 256 colors.", "12fps, 720p, 256색으로 되돌립니다.") \
    X(start_gif, "Start GIF recording", "GIF 녹화 시작") \
    X(tooltip_start_gif_title, "Record a silent GIF", "소리 없는 GIF 녹화") \
    X(tooltip_start_gif_body, \
        "Records without audio, then creates an optimized GIF. Recording stops at 30 seconds or the pixel budget. The source MKV is kept if conversion fails.", \
        "오디오 없이 녹화한 뒤 최적화된 GIF를 만듭니다. 30초 또는 픽셀 예산에서 멈춥니다. 변환이 실패하면 소스 MKV를 남깁니다.") \
    X(creating_gif, "Creating GIF...", "GIF 만드는 중...") \
    X(cancel_gif, "Cancel GIF conversion", "GIF 변환 취소") \
    X(cancelling, "Cancelling...", "취소 중...") \
    X(settings_language, "Language", "언어") \
    X(settings_output, "Output folder", "출력 폴더") \
    X(settings_shortcuts, "Shortcuts", "단축키") \
    X(settings_appearance, "Appearance", "모양") \
    X(settings_background, "Background", "백그라운드") \
    X(settings_advanced, "Advanced", "고급") \
    X(output_intro, "Screenshots, video recordings, and GIF files are saved in the same folder.", \
        "스크린샷, 비디오, GIF는 같은 폴더에 저장됩니다.") \
    X(folder_prefix, "Folder", "폴더") \
    X(change_output_folder, "Change output folder...", "출력 폴더 변경...") \
    X(choose_output_folder, "Choose OpenCapture output folder", "OpenCapture 출력 폴더 선택") \
    X(tooltip_output_title, "Shared output folder", "공용 출력 폴더") \
    X(tooltip_output_body, \
        "PNG, video, GIF, and conversion outputs all go here. Change it while idle. Recording or conversion locks the folder path.", \
        "PNG, 비디오, GIF, 변환 결과가 모두 여기로 갑니다. 유휴 상태에서만 바꿀 수 있습니다. 녹화나 변환 중에는 경로가 잠깁니다.") \
    X(tooltip_output_busy_title, "Folder is locked", "폴더가 잠겨 있습니다") \
    X(tooltip_output_busy_body, \
        "Stop recording or wait for media conversion before changing the output folder.", \
        "출력 폴더를 바꾸려면 녹화를 멈추거나 변환이 끝날 때까지 기다리세요.") \
    X(recording_recovery, "Recording recovery", "녹화 복구") \
    X(recovery_intro, "Validate an incomplete MKV and finalize it without overwriting an existing recording.", \
        "끝나지 않은 MKV를 검사한 뒤, 기존 파일을 덮어쓰지 않는 최종 이름으로 완성합니다.") \
    X(recover, "Recover", "복구") \
    X(hotkey_intro, "Click Set shortcut, then press the key combination you want. Shortcuts require Ctrl, Alt, or Shift.", \
        "단축키 지정을 누른 다음 원하는 키 조합을 누르세요. Ctrl, Alt, Shift 중 하나가 필요합니다.") \
    X(hotkey_capture, "Capture selected target", "선택 대상 캡처") \
    X(hotkey_video, "Start / stop video", "비디오 시작/중지") \
    X(hotkey_gif, "Start / stop GIF", "GIF 시작/중지") \
    X(hotkey_quick, "Quick Capture", "빠른 캡처") \
    X(set_shortcut, "Set shortcut", "단축키 지정") \
    X(listening, "Listening...", "입력 대기...") \
    X(press_keys, "Press a key combination... Esc cancels.", "키 조합을 누르세요... Esc는 취소입니다.") \
    X(clear, "Clear", "지우기") \
    X(restore_default, "Restore Default", "기본값 복원") \
    X(restore_default_shortcuts, "Restore default shortcuts", "기본 단축키 복원") \
    X(tooltip_set_shortcut_title, "Assign a shortcut", "단축키 지정") \
    X(tooltip_set_shortcut_body, \
        "Capture the next key combination, including letters, numbers, and function keys. A modifier (Ctrl, Alt, or Shift) is required.", \
        "다음 키 조합을 받습니다. 문자, 숫자, 기능 키를 쓸 수 있으며 Ctrl, Alt, Shift 중 하나가 필요합니다.") \
    X(tooltip_cancel_shortcut_title, "Cancel shortcut capture", "단축키 입력 취소") \
    X(tooltip_cancel_shortcut_body, \
        "Press Esc or click again to cancel without changing the shortcut.", \
        "Esc를 누르거나 다시 클릭하면 단축키를 바꾸지 않고 취소합니다.") \
    X(tooltip_clear_shortcut_title, "Clear this shortcut", "이 단축키 지우기") \
    X(tooltip_clear_shortcut_body, \
        "Remove this global shortcut until you assign a new one. Other shortcuts stay unchanged.", \
        "새 조합을 지정할 때까지 이 전역 단축키를 제거합니다. 다른 단축키는 그대로입니다.") \
    X(tooltip_reset_one_shortcut_title, "Restore this shortcut", "이 단축키 기본값") \
    X(tooltip_reset_one_shortcut_body, \
        "Restore this action to its original OpenCapture shortcut.", \
        "이 동작만 원래 OpenCapture 단축키로 되돌립니다.") \
    X(tooltip_reset_shortcuts_title, "Restore all shortcuts", "모든 단축키 기본값") \
    X(tooltip_reset_shortcuts_body, \
        "Restore Ctrl+Shift+F9 screenshot, F10 video, F11 GIF, and Ctrl+Shift+F8 Quick Capture.", \
        "Ctrl+Shift+F9 스크린샷, F10 비디오, F11 GIF, Ctrl+Shift+F8 빠른 캡처로 되돌립니다.") \
    X(windows_scaling, "Windows scaling", "Windows 배율") \
    X(effective_ui, "Effective UI", "실제 UI") \
    X(additional_ui_scale, "Additional UI scale", "추가 UI 배율") \
    X(tooltip_ui_scale_title, "Extra UI size", "추가 UI 크기") \
    X(tooltip_ui_scale_body, \
        "Multiplies the Windows monitor scaling. 100% follows Windows exactly. Saved immediately when you release the slider.", \
        "Windows 모니터 배율에 곱합니다. 100%는 Windows 설정을 그대로 따릅니다. 슬라이더에서 손을 떼면 바로 저장됩니다.") \
    X(show_target_border, "Show target border", "대상 테두리 표시") \
    X(border_thickness, "Border thickness", "테두리 굵기") \
    X(border_opacity, "Border opacity", "테두리 불투명도") \
    X(tooltip_border_title, "Always-on-top target outline", "항상 위 대상 윤곽") \
    X(tooltip_border_body, \
        "A click-through outline around the selected window, region, or monitor. Blue is idle, yellow is capturing, orange is paused. The outline is excluded from the captured result when Windows allows it.", \
        "선택한 창·영역·모니터 주변의 클릭 통과 윤곽입니다. 파랑은 대기, 노랑은 캡처, 주황은 일시정지입니다. Windows가 허용하면 캡처 결과에서 제외됩니다.") \
    X(tooltip_thickness_title, "Outline width", "윤곽 두께") \
    X(tooltip_thickness_body, \
        "Use a thin border when you want the guide to stay unobtrusive. Saved when you release the slider.", \
        "안내선이 덜 거슬리게 하려면 얇게 하세요. 슬라이더에서 손을 떼면 저장됩니다.") \
    X(tooltip_opacity_title, "Outline opacity", "윤곽 불투명도") \
    X(tooltip_opacity_body, \
        "Lower opacity makes the border less distracting while keeping the target visible.", \
        "불투명도를 낮추면 대상은 보이되 테두리가 덜 눈에 띕니다.") \
    X(selection_dimming, "Selection outside dimming", "선택 바깥 어둡기") \
    X(tooltip_dimming_title, "Region picker dimming", "영역 선택 어둡기") \
    X(tooltip_dimming_body, \
        "Dims only the area outside a region selection. The selected area stays clear. 0% turns dimming off.", \
        "영역 선택 시 바깥만 어둡게 합니다. 선택한 부분은 밝게 유지됩니다. 0%면 어둡게 하지 않습니다.") \
    X(keep_running, "Keep running when the window is closed", "창을 닫아도 계속 실행") \
    X(tooltip_tray_title, "Notification area background mode", "알림 영역 상주") \
    X(tooltip_tray_body, \
        "Keeps global shortcuts available from the notification area after closing the main window. New users leave this off so closing the window fully exits, which is safer on company PCs. Exit OpenCapture from the tray menu to quit completely.", \
        "메인 창을 닫은 뒤에도 알림 영역에서 전역 단축키를 쓸 수 있습니다. 신규 사용자는 끄고 두는 것이 회사 PC에 더 안전하며, 창을 닫으면 완전히 종료됩니다. 완전 종료는 트레이 메뉴의 OpenCapture 종료를 사용하세요.") \
    X(restore_all, "Restore Default", "기본값 복원") \
    X(tooltip_restore_all_title, "Reset every setting", "모든 설정 초기화") \
    X(tooltip_restore_all_body, \
        "Restore shortcuts, video, GIF, screenshot, display, language follows Windows, border, region selection, and background settings.", \
        "단축키, 비디오, GIF, 스크린샷, 디스플레이, 언어(Windows 따름), 테두리, 영역 선택, 백그라운드 설정을 되돌립니다.") \
    X(test_wgc, "Test WGC capture", "WGC 캡처 테스트") \
    X(stop_capture_test, "Stop capture test", "캡처 테스트 중지") \
    X(recording_pipeline_starting, "Recording pipeline: starting", "녹화 파이프라인: 시작 중") \
    X(recording_pipeline_active, "Recording pipeline: active", "녹화 파이프라인: 동작 중") \
    X(frames_label, "Frames", "프레임") \
    X(queued_label, "queued", "대기") \
    X(dropped_label, "dropped", "드롭") \
    X(gpu_label, "GPU", "GPU") \
    X(ffmpeg_label, "FFmpeg", "FFmpeg") \
    X(encoder_label, "Encoder", "인코더") \
    X(font_ok, "Korean and English fonts loaded from Windows.", "Windows에서 한글·영문 글꼴을 불러왔습니다.") \
    X(font_korean_missing, "Korean UI font was not found (malgun.ttf). Hangul in window titles may not display.", \
        "한글 UI 글꼴(malgun.ttf)을 찾지 못했습니다. 창 제목의 한글이 깨질 수 있습니다.") \
    X(footer_ready, "GPU capture, synchronized audio, and recoverable recording are ready.", \
        "GPU 캡처, 동기화된 오디오, 복구 가능한 녹화가 준비되었습니다.") \
    X(strip_encoded, "Encoded", "인코딩") \
    X(strip_source, "Source", "소스") \
    X(strip_skipped, "skipped ticks", "건너뛴 틱") \
    X(strip_capture_drops, "capture drops", "캡처 드롭") \
    X(strip_realtime_drops, "realtime drops", "실시간 드롭") \
    X(strip_encoder_queue, "Encoder queue peak", "인코더 큐 최대") \
    X(strip_encoder_drops, "drops", "드롭") \
    X(strip_submit, "max submit", "최대 제출") \
    X(strip_encoder_pressure, "Encoder pressure", "인코더 압력") \
    X(strip_mux_peak, "Mux queue peak", "먹스 큐 최대") \
    X(strip_active_encoder, "Active encoder", "활성 인코더") \
    X(tooltip_skipped_title, "Skipped timeline ticks", "건너뛴 타임라인 틱") \
    X(tooltip_skipped_body, \
        "Skipped ticks are timeline slots the recorder did not encode while it was late. They expose real capture or encoder stalls instead of hiding them with duplicate-frame bursts.", \
        "건너뛴 틱은 녹화기가 늦어서 인코딩하지 않은 타임라인 칸입니다. 같은 프레임을 여러 번 넣어 가리는 대신, 실제 캡처·인코더 지연을 보여 줍니다.") \
    X(tooltip_drops_title, "Capture and realtime drops", "캡처·실시간 드롭") \
    X(tooltip_drops_body, \
        "Realtime drops discard stale queued frames instead of spending more GPU time catching up. Capture drops or a large source gap point to capture or GPU scheduling pressure.", \
        "실시간 드롭은 따라잡느라 GPU를 더 쓰지 않고 오래된 대기 프레임을 버립니다. 캡처 드롭이나 큰 소스 간격은 캡처 또는 GPU 스케줄 압력을 뜻합니다.") \
    X(tooltip_encoder_queue_title, "Encoder worker queue", "인코더 작업 큐") \
    X(tooltip_encoder_queue_body, \
        "The bounded worker drops the oldest waiting frame instead of blocking capture. Submit time above the frame budget predicts encoder or GPU contention.", \
        "제한된 작업자는 캡처를 막지 않고 가장 오래된 대기 프레임을 버립니다. 제출 시간이 프레임 예산을 넘으면 인코더나 GPU 경합을 의심하세요.") \
    X(tooltip_mux_title, "Mux queue", "먹스 큐") \
    X(tooltip_mux_body, \
        "A growing mux queue points to slow storage rather than encoder pressure.", \
        "먹스 큐가 커지면 인코더보다 저장 장치가 느린 경우가 많습니다.") \
    X(audio_prefix, "Audio", "오디오") \
    X(px_suffix, "px", "px") \
    X(percent_suffix, "%", "%") \
    X(mbps_suffix, "Mbps", "Mbps") \
    X(gb_per_hour, "GB/hour", "GB/시간") \
    X(tray_open, "Open OpenCapture", "OpenCapture 열기") \
    X(tray_quick, "Quick Capture", "빠른 캡처") \
    X(tray_stop, "Stop current recording", "현재 녹화 중지") \
    X(tray_exit, "Exit OpenCapture", "OpenCapture 종료") \
    X(status_scale_saved, "UI scale saved. Windows monitor scaling remains automatic.", \
        "UI 배율을 저장했습니다. Windows 모니터 배율은 계속 자동입니다.") \
    X(status_scale_failed, "UI scale could not be saved.", "UI 배율을 저장하지 못했습니다.") \
    X(status_dpi_changed, "Monitor DPI changed; UI scale updated automatically.", \
        "모니터 DPI가 바뀌어 UI 배율을 자동으로 맞췄습니다.") \
    X(status_language_saved, "Interface language saved.", "인터페이스 언어를 저장했습니다.") \
    X(status_language_failed, "Interface language could not be saved.", "인터페이스 언어를 저장하지 못했습니다.") \
    X(status_tray_keep, "Closing the window now keeps OpenCapture in the notification area.", \
        "이제 창을 닫아도 OpenCapture가 알림 영역에 남습니다.") \
    X(status_tray_exit, "Closing the window now exits OpenCapture.", \
        "이제 창을 닫으면 OpenCapture가 종료됩니다.") \
    X(status_tray_save_failed, "Background setting could not be saved.", "백그라운드 설정을 저장하지 못했습니다.") \
    X(status_tray_default_keep, "OpenCapture remains available in the notification area after closing.", \
        "창을 닫아도 알림 영역에서 OpenCapture를 계속 사용할 수 있습니다.") \
    X(status_tray_default_exit, "Closing the window exits OpenCapture. Enable background mode to keep it in the notification area.", \
        "창을 닫으면 OpenCapture가 종료됩니다. 알림 영역에 남기려면 백그라운드 모드를 켜세요.") \
    X(status_tray_unavailable, "Notification area icon unavailable; closing the window exits the app.", \
        "알림 영역 아이콘을 쓸 수 없어 창을 닫으면 앱이 종료됩니다.") \
    X(status_tray_running, "OpenCapture is running in the notification area.", \
        "OpenCapture가 알림 영역에서 실행 중입니다.") \
    X(status_tray_restored, "Notification area icon restored after Explorer restarted.", \
        "탐색기가 다시 시작된 뒤 알림 영역 아이콘을 복구했습니다.") \
    X(status_tray_restore_failed, "Notification area icon could not be restored; closing the window exits the app.", \
        "알림 영역 아이콘을 복구하지 못했습니다. 창을 닫으면 앱이 종료됩니다.") \
    X(status_screenshot_setting_saved, "Screenshot shortcut result saved.", "스크린샷 단축키 결과를 저장했습니다.") \
    X(status_screenshot_setting_failed, "Screenshot shortcut result could not be saved.", \
        "스크린샷 단축키 결과를 저장하지 못했습니다.") \
    X(status_screenshot_restored, "Screenshot shortcut result restored to clipboard.", \
        "스크린샷 단축키 결과를 클립보드로 되돌렸습니다.") \
    X(gif_colors_64, "64 colors", "64색") \
    X(gif_colors_128, "128 colors", "128색") \
    X(gif_colors_192, "192 colors", "192색") \
    X(gif_colors_256, "256 colors", "256색") \
    X(reset_appearance, "Reset appearance", "모양 기본값") \
    X(tooltip_reset_appearance_title, "Reset appearance", "모양 초기화") \
    X(tooltip_reset_appearance_body, \
        "Restore 100% extra UI scale, a visible 3 px 85% border, and 30% region dimming.", \
        "추가 UI 배율 100%, 보이는 3픽셀 85% 테두리, 영역 어둡기 30%로 되돌립니다.")

enum class Msg {
#define OPENCAPTURE_STRING_ENUM(id, en, ko) id,
    OPENCAPTURE_STRINGS(OPENCAPTURE_STRING_ENUM)
#undef OPENCAPTURE_STRING_ENUM
    Count
};

[[nodiscard]] Language DetectOsLanguage() noexcept;
[[nodiscard]] Language ParseLanguage(std::string_view text, Language fallback) noexcept;
[[nodiscard]] const char* LanguageSettingValue(Language language) noexcept;
[[nodiscard]] const char* Tr(Msg id) noexcept;
[[nodiscard]] std::wstring TrW(Msg id);
void SetLanguage(Language language) noexcept;
[[nodiscard]] Language CurrentLanguage() noexcept;
[[nodiscard]] std::size_t StringTableCount() noexcept;
[[nodiscard]] bool StringTablesComplete() noexcept;
[[nodiscard]] std::string JoinStatus(Msg prefix, std::string_view extra);

} // namespace opencapture
