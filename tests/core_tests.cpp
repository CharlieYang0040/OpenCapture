#include "core/bounded_queue.h"
#include "core/capture_target.h"
#include "core/hotkey.h"
#include "core/recording_options.h"
#include "core/screenshot_options.h"
#include "core/session_state.h"
#include "core/ui_scale.h"
#include "ui/i18n.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}

void TestBoundedQueue() {
    opencapture::BoundedQueue<int> queue(2);
    Check(!queue.PushDropOldest(1), "first push does not drop");
    Check(!queue.PushDropOldest(2), "second push does not drop");
    Check(queue.PushDropOldest(3), "full queue drops oldest");
    Check(queue.Size() == 2, "queue stays bounded");
    Check(queue.TryPop() == 2, "oldest retained item is two");
    Check(queue.TryPop() == 3, "newest item is three");
    Check(!queue.TryPop().has_value(), "empty pop is non-blocking");
    queue.PushDropOldest(4);
    queue.Clear();
    Check(queue.Size() == 0, "queue clear releases pending items");
}

void TestSessionState() {
    opencapture::SessionState state;
    Check(state.Phase() == opencapture::SessionPhase::Idle, "session starts idle");
    Check(!state.Pause(), "cannot pause idle session");
    Check(state.BeginStart(), "idle session can start");
    Check(state.MarkRecording(), "starting session becomes recording");
    Check(state.Pause(), "recording can pause");
    Check(state.Resume(), "paused session can resume");
    Check(state.BeginStop(), "recording can stop");
    Check(state.MarkStopped(), "stopping session becomes idle");
    state.Fail("device lost");
    Check(state.Phase() == opencapture::SessionPhase::Failed, "failure is recorded");
    Check(state.Error() == "device lost", "failure message is retained");
    Check(state.Reset(), "failed session can reset");

    Check(state.BeginStart(), "reset session can start again");
    Check(state.BeginStop(), "starting session can be cancelled");
    Check(state.MarkStopped(), "cancelled starting session becomes idle");
}

void TestQpcTimestampConversion() {
    Check(opencapture::QpcDeltaToFramePts(10'000'000, 10'000'000, 60) == 60,
          "one QPC second becomes sixty frame ticks");
    Check(opencapture::QpcDeltaToFramePts(5'000'000, 10'000'000, 60) == 30,
          "half a QPC second becomes thirty frame ticks");
    Check(opencapture::QpcDeltaToFramePts(-1, 10'000'000, 60) == 0,
          "negative QPC deltas clamp to zero");
    Check(opencapture::ActiveQpcDelta(30'000'000, 10'000'000) == 20'000'000,
          "paused QPC duration is removed from the active timeline");
    Check(opencapture::ActiveQpcDelta(5, 10) == 0,
          "paused QPC duration cannot make the active timeline negative");

    const auto first = opencapture::SelectCurrentFramePts(0, 10'000'000, 60, -1);
    Check(first.emit && first.presentationTimestamp == 0 && first.skippedTicks == 0,
          "first video frame starts at zero without a skipped tick");
    const auto late = opencapture::SelectCurrentFramePts(10'000'000, 10'000'000, 60, 0);
    Check(late.emit && late.presentationTimestamp == 60 && late.skippedTicks == 59,
          "late video work jumps to the current timestamp instead of emitting a catch-up burst");
    const auto duplicate = opencapture::SelectCurrentFramePts(10'000'000, 10'000'000, 60, 60);
    Check(!duplicate.emit, "a second frame in the same output tick is ignored");
}

void TestCaptureTarget() {
    opencapture::CaptureTarget region;
    region.type = opencapture::CaptureTargetType::Region;
    region.region = RECT{10, 20, 110, 220};
    Check(region.IsValid(), "positive region is valid");
    Check(region.Description() == L"Region 10,20 - 110,220", "region has diagnostic description");

    opencapture::CaptureTarget emptyWindow;
    emptyWindow.type = opencapture::CaptureTargetType::Window;
    Check(!emptyWindow.IsValid(), "null window is invalid");
}

void TestRegionPresetScaling() {
    opencapture::CaptureRegionPreset preset;
    preset.anchorType = opencapture::RegionAnchorType::WindowClient;
    preset.region = RECT{100, 50, 900, 500};
    preset.referenceClientSize = SIZE{1000, 600};
    const RECT scaled = opencapture::ScaleRegionToClient(preset, SIZE{2000, 1200});
    Check(scaled.left == 200 && scaled.top == 100, "window preset origin scales");
    Check(scaled.right == 1800 && scaled.bottom == 1000, "window preset size scales");

    preset.anchorType = opencapture::RegionAnchorType::VirtualDesktop;
    const RECT absolute = opencapture::ScaleRegionToClient(preset, SIZE{2000, 1200});
    Check(absolute.left == 100 && absolute.right == 900, "desktop preset remains absolute");
}

void TestLocalRegionConversion() {
    const RECT source{-1920, 0, 0, 1080};
    const RECT local = opencapture::ToLocalClampedRegion(RECT{-1800, 100, -200, 900}, source);
    Check(local.left == 120 && local.top == 100, "desktop region becomes monitor-local");
    Check(local.right == 1720 && local.bottom == 900, "local region keeps size");

    const RECT clipped = opencapture::ToLocalClampedRegion(RECT{-2000, -50, 200, 1200}, source);
    Check(clipped.left == 0 && clipped.top == 0, "region clips at monitor origin");
    Check(clipped.right == 1920 && clipped.bottom == 1080, "region clips at monitor extent");
}

void TestRegionSelectionBounds() {
    const RECT monitor{-1920, 0, 0, 1080};
    const POINT clamped = opencapture::ClampPointToRect(POINT{-2000, 1200}, monitor);
    Check(clamped.x == -1920 && clamped.y == 1080,
          "selection pointer stays inside its starting monitor");

    const RECT selection{-1800, 100, -800, 700};
    const RECT movedLeft = opencapture::MoveRectWithinBounds(selection, -500, 0, monitor);
    Check(movedLeft.left == -1920 && movedLeft.right == -920,
          "keyboard movement clamps at the left monitor edge");
    const RECT movedBottom = opencapture::MoveRectWithinBounds(selection, 0, 1000, monitor);
    Check(movedBottom.top == 480 && movedBottom.bottom == 1080,
          "keyboard movement clamps at the bottom monitor edge");
}

void TestOutputSizeNormalization() {
    const SIZE source{1921, 1081};
    const SIZE unchanged = opencapture::NormalizeOutputSize({}, source, false);
    Check(unchanged.cx == 1921 && unchanged.cy == 1081, "BGRA keeps source dimensions");
    const SIZE nv12 = opencapture::NormalizeOutputSize({}, source, true);
    Check(nv12.cx == 1920 && nv12.cy == 1080, "NV12 rounds dimensions down to even values");
    const SIZE scaled = opencapture::NormalizeOutputSize(SIZE{1281, 721}, source, true);
    Check(scaled.cx == 1280 && scaled.cy == 720, "scaled video dimensions are even");
    const SIZE fitted = opencapture::FitOutputHeight(SIZE{1920, 1080}, 720, true);
    Check(fitted.cx == 1280 && fitted.cy == 720, "GIF height limit preserves aspect ratio");
    const SIZE portrait = opencapture::FitOutputHeight(SIZE{1080, 1920}, 480, true);
    Check(portrait.cx == 270 && portrait.cy == 480, "portrait GIF scaling preserves aspect ratio");
    const SIZE noUpscale = opencapture::FitOutputHeight(SIZE{640, 360}, 720, true);
    Check(noUpscale.cx == 640 && noUpscale.cy == 360, "GIF scaling never enlarges a small source");
    Check(opencapture::GifDurationLimit(SIZE{1280, 720}, 12) == 30.0,
          "default 720p GIF can use the full thirty second limit");
    const double largeLimit = opencapture::GifDurationLimit(SIZE{1920, 1080}, 30);
    Check(largeLimit > 8.0 && largeLimit < 8.1,
          "1080p30 GIF is shortened by the safe pixel budget");
}

void TestUiScale() {
    Check(opencapture::ClampUiScalePercent(50) == 75, "UI scale clamps low values");
    Check(opencapture::ClampUiScalePercent(250) == 200, "UI scale clamps high values");
    Check(opencapture::ComputeUiScale(96, 100) == 1.0F, "96 DPI keeps the base UI scale");
    Check(opencapture::ComputeUiScale(144, 100) == 1.5F, "144 DPI produces 150 percent UI");
    Check(opencapture::ComputeUiScale(192, 125) == 2.5F,
          "user UI scale multiplies the Windows monitor scale");
}

void TestHotkeyChords() {
    using opencapture::HotkeyChord;
    using opencapture::HotkeyLabel;
    using opencapture::IsAssignableHotkeyKey;
    using opencapture::IsModifierVirtualKey;
    using opencapture::IsValidHotkeyChord;
    using opencapture::kHotkeyModAlt;
    using opencapture::kHotkeyModControl;
    using opencapture::kHotkeyModShift;
    using opencapture::kHotkeyModWin;
    using opencapture::NormalizeHotkeyChord;
    using opencapture::VirtualKeyLabel;

    Check(IsModifierVirtualKey(0x11) && IsModifierVirtualKey(0xA2),
          "control keys are modifiers");
    Check(!IsAssignableHotkeyKey(0x11) && !IsAssignableHotkeyKey(0x14),
          "modifiers and lock keys are not assignable");
    Check(IsAssignableHotkeyKey(0x70) && IsAssignableHotkeyKey('A') &&
              IsAssignableHotkeyKey(0x20),
          "function, letter, and space keys are assignable");
    Check(IsValidHotkeyChord({}), "an unbound shortcut is valid");
    Check(!IsValidHotkeyChord({0, 0x78}), "a shortcut without modifiers is rejected");
    Check(IsValidHotkeyChord({kHotkeyModControl | kHotkeyModShift, 0x78}),
          "Ctrl+Shift+F9 is valid");
    Check(IsValidHotkeyChord({kHotkeyModAlt | kHotkeyModWin, 'S'}),
          "Alt+Win+S is valid because it includes a required modifier");
    Check(HotkeyLabel({}) == "Not set", "unbound shortcuts use a clear label");
    Check(HotkeyLabel({kHotkeyModControl | kHotkeyModShift, 0x78}) == "Ctrl+Shift+F9",
          "saved function-key shortcuts keep their previous labels");
    Check(HotkeyLabel({kHotkeyModControl | kHotkeyModAlt, 'Q'}) == "Ctrl+Alt+Q",
          "letter shortcuts can be captured outside the old F-key list");
    Check(HotkeyLabel({kHotkeyModShift, 0x2C}) == "Shift+Print Screen",
          "Print Screen can be used with a modifier");
    Check(VirtualKeyLabel(0x25) == "Left", "arrow keys have readable names");
    const auto normalized = NormalizeHotkeyChord({0x4002, 'G'});
    Check(normalized.modifiers == kHotkeyModControl && normalized.virtualKey == 'G',
          "repeat and unknown modifier bits are stripped");
}

void TestRecordingPreferences() {
    using opencapture::ClampRecordingPreferences;
    using opencapture::DefaultRecordingPreferences;
    using opencapture::GifColorIndex;
    using opencapture::GifFpsIndex;
    using opencapture::GifHeightIndex;
    using opencapture::ParseRecordingPreferences;
    using opencapture::RecordingPreferences;
    using opencapture::SerializeRecordingPreferences;

    const auto defaults = DefaultRecordingPreferences();
    Check(defaults.framesPerSecond == 60 && defaults.quality == 1 && defaults.systemAudio &&
              defaults.codec == opencapture::VideoCodecPreference::H264 &&
              defaults.resolution == opencapture::VideoResolutionLimit::Height1080 &&
              defaults.efficiency == opencapture::EncoderEfficiencyMode::Realtime &&
              defaults.allowCodecFallback,
          "recording defaults protect game performance at 1080p60");
    Check(defaults.gifFramesPerSecond == 12 && defaults.gifHeight == 720 &&
              defaults.gifColors == 256,
          "GIF defaults match the previous UI values");

    RecordingPreferences custom = defaults;
    custom.framesPerSecond = 90;
    custom.quality = 2;
    custom.remuxToMp4 = true;
    custom.systemAudio = false;
    custom.microphone = true;
    custom.encoderName = "h264_nvenc";
    custom.gifFramesPerSecond = 15;
    custom.gifHeight = 480;
    custom.gifColors = 128;
    custom.profile = opencapture::RecordingProfile::Custom;
    custom.codec = opencapture::VideoCodecPreference::Hevc;
    custom.resolution = opencapture::VideoResolutionLimit::Height720;
    custom.efficiency = opencapture::EncoderEfficiencyMode::Efficient;
    custom.customBitRateMbps = 4;
    custom.allowCodecFallback = false;
    custom.useCustomBitRate = true;
    const auto serializedCustom = SerializeRecordingPreferences(custom);
    Check(serializedCustom.starts_with("profile_schema=4\n"),
          "recording settings identify the integrated profile schema");
    const auto restored = ParseRecordingPreferences(serializedCustom);
    Check(restored == custom, "recording preferences round-trip through the settings file");

    RecordingPreferences invalid{};
    invalid.framesPerSecond = 8; invalid.quality = 9; invalid.encoderName = "bad\nname\r";
    invalid.gifFramesPerSecond = 14; invalid.gifHeight = 500; invalid.gifColors = 100;
    const auto clamped = ClampRecordingPreferences(invalid);
    Check(clamped.framesPerSecond == 15 && clamped.quality == 2,
          "video settings clamp to the supported range");
    Check(clamped.gifFramesPerSecond == 15 && clamped.gifHeight == 480 &&
              clamped.gifColors == 128,
          "GIF settings snap to the nearest allowed choice");
    Check(clamped.encoderName == "badname", "encoder names cannot contain newlines");
    Check(GifFpsIndex(12) == 2 && GifHeightIndex(720) == 2 && GifColorIndex(256) == 3,
          "GIF combo indexes match the previous default selections");

    const auto parsed = ParseRecordingPreferences(
        "# comment\n"
        "fps=48\n"
        "quality=0\n"
        "remux_to_mp4=1\n"
        "system_audio=0\n"
        "microphone=true\n"
        "encoder=h264_qsv\n"
        "gif_fps=20\n"
        "gif_height=1080\n"
        "gif_colors=64\n"
        "unknown=ignored\n");
    Check(parsed.framesPerSecond == 48 && parsed.quality == 0 && parsed.remuxToMp4 &&
              !parsed.systemAudio && parsed.microphone && parsed.encoderName == "h264_qsv",
          "known recording settings are restored and unknown keys are ignored");
    Check(parsed.gifFramesPerSecond == 20 && parsed.gifHeight == 1080 &&
              parsed.gifColors == 64,
          "GIF settings are restored from disk");
    Check(ParseRecordingPreferences("fps=not-a-number\nquality=1\n").framesPerSecond == 60,
          "invalid recording values keep the safe default");
    const auto legacyAnimation = ParseRecordingPreferences("profile_schema=3\ngif_fps=10\n");
    Check(legacyAnimation.animationFormat == opencapture::AnimationFormat::Gif,
          "legacy settings keep GIF output compatibility");
    const auto smallAnimation = opencapture::ApplyAnimationProfile(
        defaults, opencapture::AnimationProfile::Smallest);
    Check(smallAnimation.animationFormat == opencapture::AnimationFormat::Avif &&
              smallAnimation.gifHeight == 720 && smallAnimation.avifCrf == 34,
          "smallest animation profile selects AVIF with bounded defaults");
    const auto webpEstimate = opencapture::EstimateAnimationSize(
        opencapture::AnimationFormat::WebP, 1280, 720, 12, 10.0, 82, 256, 34);
    const auto gifEstimate = opencapture::EstimateAnimationSize(
        opencapture::AnimationFormat::Gif, 1280, 720, 12, 10.0, 82, 256, 34);
    Check(webpEstimate.minimumBytes > 0 && webpEstimate.maximumBytes < gifEstimate.maximumBytes,
          "animation estimate communicates WebP size advantage over GIF");

    const auto game = opencapture::ApplyRecordingProfile(custom, opencapture::RecordingProfile::Compatibility);
    Check(game.framesPerSecond == 60 &&
              game.codec == opencapture::VideoCodecPreference::H264 &&
              game.resolution == opencapture::VideoResolutionLimit::Height1080 &&
              game.efficiency == opencapture::EncoderEfficiencyMode::Realtime &&
              game.allowCodecFallback && !game.useCustomBitRate,
          "game profile applies a complete low-impact configuration");
    const auto balanced = opencapture::ApplyRecordingProfile(custom, opencapture::RecordingProfile::Balanced);
    Check(balanced.framesPerSecond == 60 &&
              balanced.codec == opencapture::VideoCodecPreference::Auto &&
              balanced.resolution == opencapture::VideoResolutionLimit::Height1080 &&
              balanced.efficiency == opencapture::EncoderEfficiencyMode::Balanced &&
              balanced.allowCodecFallback && !balanced.useCustomBitRate,
          "balanced profile applies automatic codec selection at 1080p60 with fallback");
    const auto compact = opencapture::ApplyRecordingProfile(custom, opencapture::RecordingProfile::Compact);
    Check(compact.codec == opencapture::VideoCodecPreference::Auto &&
              compact.resolution == opencapture::VideoResolutionLimit::Height1080 &&
              compact.efficiency == opencapture::EncoderEfficiencyMode::Balanced &&
              compact.allowCodecFallback && !compact.useCustomBitRate,
          "small-file profile selects an automatic codec, 1080p cap, balanced encoding, and fallback");
    const auto compactH264 = opencapture::RecommendedVideoBitRate(
        compact, 1920, 1080, opencapture::VideoCodecPreference::H264);
    const auto compactHevc = opencapture::RecommendedVideoBitRate(
        compact, 1920, 1080, opencapture::VideoCodecPreference::Hevc);
    Check(compact.framesPerSecond == 30 && compactHevc < compactH264 && compactH264 == 3'500'000,
          "small-file profile uses 30 fps and accounts for codec efficiency");
    const auto quality = opencapture::ApplyRecordingProfile(
        custom, opencapture::RecordingProfile::Quality);
    Check(quality.framesPerSecond == 60 &&
              quality.codec == opencapture::VideoCodecPreference::Auto &&
              quality.resolution == opencapture::VideoResolutionLimit::Source &&
              quality.efficiency == opencapture::EncoderEfficiencyMode::Quality &&
              quality.allowCodecFallback && !quality.useCustomBitRate,
          "quality-first profile keeps source resolution and enables explicit high-quality effort");
    Check(opencapture::RecommendedVideoBitRate(
              quality, 1920, 1080, opencapture::VideoCodecPreference::Hevc) == 14'400'000,
          "quality-first profile assigns a higher HEVC bitrate budget");
    Check(opencapture::PredictRecordingGpuPressure(game, 1080) ==
              opencapture::RecordingGpuPressure::Low &&
              opencapture::PredictRecordingGpuPressure(balanced, 1080) ==
              opencapture::RecordingGpuPressure::Moderate &&
              opencapture::PredictRecordingGpuPressure(quality, 1440) ==
              opencapture::RecordingGpuPressure::VeryHigh,
          "GPU pressure prediction separates game, balanced, and quality-first goals");
    const auto preservedCustom =
        opencapture::ApplyRecordingProfile(custom, opencapture::RecordingProfile::Custom);
    Check(preservedCustom.codec == custom.codec && preservedCustom.framesPerSecond == custom.framesPerSecond &&
              preservedCustom.useCustomBitRate,
          "custom profile preserves direct user choices");
    Check(opencapture::ResolutionHeightLimit(opencapture::VideoResolutionLimit::Height720) == 720,
          "video resolution limit resolves to an output height");
    Check(opencapture::EstimatedRecordingBytesPerHour(6'000'000, true) == 2'786'400'000ULL,
          "estimated size includes the default AAC stream");
}

void TestScreenshotDestinationSettings() {
    using opencapture::ParseScreenshotDestination;
    using opencapture::ScreenshotDestination;
    using opencapture::ScreenshotDestinationSettingValue;
    Check(ParseScreenshotDestination("clipboard") == ScreenshotDestination::Clipboard,
          "screenshot settings parse clipboard");
    Check(ParseScreenshotDestination("file") == ScreenshotDestination::File,
          "screenshot settings parse file");
    Check(ParseScreenshotDestination("file_and_clipboard") ==
              ScreenshotDestination::FileAndClipboard,
          "screenshot settings parse file and clipboard");
    Check(ParseScreenshotDestination("invalid") == ScreenshotDestination::Clipboard,
          "invalid screenshot setting uses the safe default");
    Check(ScreenshotDestinationSettingValue(ScreenshotDestination::FileAndClipboard) ==
              "file_and_clipboard",
          "screenshot settings serialize file and clipboard");
    Check(opencapture::ParseScreenshotProfile("avif_smallest") ==
              opencapture::ScreenshotProfile::AvifSmallest &&
              opencapture::ScreenshotProfileExtension(opencapture::ScreenshotProfile::JpegCompatible) == L".jpg" &&
              opencapture::ScreenshotProfileSettingValue(opencapture::ScreenshotProfile::WebpBalanced) ==
                  "webp_balanced",
          "screenshot profiles parse, serialize, and select their file extension");
}

void TestI18nTables() {
    using opencapture::JoinStatus;
    using opencapture::Language;
    using opencapture::LanguageSettingValue;
    using opencapture::Msg;
    using opencapture::ParseLanguage;
    using opencapture::SetLanguage;
    using opencapture::StringTableCount;
    using opencapture::StringTablesComplete;
    using opencapture::Tr;
    Check(StringTablesComplete(), "english and korean string tables are complete");
    Check(StringTableCount() == static_cast<std::size_t>(Msg::Count),
          "string table count matches Msg::Count");
    Check(ParseLanguage("ko", Language::English) == Language::Korean, "parse ko");
    Check(ParseLanguage("en", Language::Korean) == Language::English, "parse en");
    Check(ParseLanguage("nope", Language::Korean) == Language::Korean, "parse fallback");
    Check(std::string(LanguageSettingValue(Language::Korean)) == "ko", "serialize ko");
    SetLanguage(Language::English);
    const std::string englishReady = Tr(Msg::status_ready);
    SetLanguage(Language::Korean);
    const std::string koreanReady = Tr(Msg::status_ready);
    Check(englishReady == "Ready", "english ready label");
    Check(koreanReady == "준비됨", "korean ready label");
    Check(englishReady != koreanReady, "english and korean labels differ");
    Check(JoinStatus(Msg::header_output, "C:\\out").find("C:\\out") != std::string::npos,
          "status join keeps path");
    SetLanguage(Language::English);
}

} // namespace

int main() {
    TestBoundedQueue();
    TestSessionState();
    TestQpcTimestampConversion();
    TestCaptureTarget();
    TestRegionPresetScaling();
    TestLocalRegionConversion();
    TestRegionSelectionBounds();
    TestOutputSizeNormalization();
    TestUiScale();
    TestHotkeyChords();
    TestRecordingPreferences();
    TestScreenshotDestinationSettings();
    TestI18nTables();
    if (failures == 0) std::cout << "All OpenCapture core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
