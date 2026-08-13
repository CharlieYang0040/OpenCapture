#pragma once

#include <mutex>
#include <cstdint>
#include <string>

namespace opencapture {

[[nodiscard]] std::int64_t QpcDeltaToFramePts(std::int64_t qpcDelta,
                                               std::int64_t qpcFrequency,
                                               int framesPerSecond) noexcept;
[[nodiscard]] std::int64_t ActiveQpcDelta(std::int64_t totalDelta,
                                          std::int64_t pausedDuration) noexcept;

struct FramePtsDecision {
    bool emit{};
    std::int64_t presentationTimestamp{};
    std::uint64_t skippedTicks{};
};

// Selects at most one output timestamp for the current clock position. A late
// encoder jumps to the current point instead of synchronously encoding every
// missed tick, which would make a transient stall feed back into a longer one.
[[nodiscard]] FramePtsDecision SelectCurrentFramePts(std::int64_t qpcDelta,
                                                     std::int64_t qpcFrequency,
                                                     int framesPerSecond,
                                                     std::int64_t lastPts) noexcept;

enum class SessionPhase {
    Idle,
    Starting,
    Recording,
    Paused,
    Stopping,
    Failed,
};

class SessionState final {
public:
    [[nodiscard]] SessionPhase Phase() const noexcept;
    [[nodiscard]] std::string Error() const;

    bool BeginStart();
    bool MarkRecording();
    bool Pause();
    bool Resume();
    bool BeginStop();
    bool MarkStopped();
    void Fail(std::string message);
    bool Reset();

private:
    mutable std::mutex mutex_;
    SessionPhase phase_{SessionPhase::Idle};
    std::string error_;
};

} // namespace opencapture
