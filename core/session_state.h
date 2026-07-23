#pragma once

#include <mutex>
#include <cstdint>
#include <string>

namespace opencapture {

[[nodiscard]] std::int64_t QpcDeltaToFramePts(std::int64_t qpcDelta,
                                               std::int64_t qpcFrequency,
                                               int framesPerSecond) noexcept;

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
