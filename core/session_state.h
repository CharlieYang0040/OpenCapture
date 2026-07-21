#pragma once

#include <mutex>
#include <string>

namespace opencapture {

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

