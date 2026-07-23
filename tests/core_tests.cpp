#include "core/bounded_queue.h"
#include "core/capture_target.h"
#include "core/session_state.h"

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

void TestOutputSizeNormalization() {
    const SIZE source{1921, 1081};
    const SIZE unchanged = opencapture::NormalizeOutputSize({}, source, false);
    Check(unchanged.cx == 1921 && unchanged.cy == 1081, "BGRA keeps source dimensions");
    const SIZE nv12 = opencapture::NormalizeOutputSize({}, source, true);
    Check(nv12.cx == 1920 && nv12.cy == 1080, "NV12 rounds dimensions down to even values");
    const SIZE scaled = opencapture::NormalizeOutputSize(SIZE{1281, 721}, source, true);
    Check(scaled.cx == 1280 && scaled.cy == 720, "scaled video dimensions are even");
}

} // namespace

int main() {
    TestBoundedQueue();
    TestSessionState();
    TestQpcTimestampConversion();
    TestCaptureTarget();
    TestRegionPresetScaling();
    TestLocalRegionConversion();
    TestOutputSizeNormalization();
    if (failures == 0) std::cout << "All OpenCapture core tests passed.\n";
    return failures == 0 ? 0 : 1;
}
