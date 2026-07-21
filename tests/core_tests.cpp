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

} // namespace

int main() {
    TestBoundedQueue();
    TestSessionState();
    TestCaptureTarget();
    if (failures == 0) std::cout << "All OpenCapture core tests passed.\n";
    return failures == 0 ? 0 : 1;
}

