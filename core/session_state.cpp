#include "core/session_state.h"

#include <utility>

namespace opencapture {

std::int64_t QpcDeltaToFramePts(std::int64_t qpcDelta, std::int64_t qpcFrequency,
                                int framesPerSecond) noexcept {
    if (qpcDelta <= 0 || qpcFrequency <= 0 || framesPerSecond <= 0) return 0;
    return (qpcDelta * framesPerSecond + qpcFrequency / 2) / qpcFrequency;
}

SessionPhase SessionState::Phase() const noexcept {
    std::scoped_lock lock(mutex_);
    return phase_;
}

std::string SessionState::Error() const {
    std::scoped_lock lock(mutex_);
    return error_;
}

bool SessionState::BeginStart() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Idle) return false;
    error_.clear();
    phase_ = SessionPhase::Starting;
    return true;
}

bool SessionState::MarkRecording() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Starting) return false;
    phase_ = SessionPhase::Recording;
    return true;
}

bool SessionState::Pause() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Recording) return false;
    phase_ = SessionPhase::Paused;
    return true;
}

bool SessionState::Resume() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Paused) return false;
    phase_ = SessionPhase::Recording;
    return true;
}

bool SessionState::BeginStop() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Starting && phase_ != SessionPhase::Recording && phase_ != SessionPhase::Paused) return false;
    phase_ = SessionPhase::Stopping;
    return true;
}

bool SessionState::MarkStopped() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Stopping) return false;
    phase_ = SessionPhase::Idle;
    return true;
}

void SessionState::Fail(std::string message) {
    std::scoped_lock lock(mutex_);
    error_ = std::move(message);
    phase_ = SessionPhase::Failed;
}

bool SessionState::Reset() {
    std::scoped_lock lock(mutex_);
    if (phase_ != SessionPhase::Failed) return false;
    error_.clear();
    phase_ = SessionPhase::Idle;
    return true;
}

} // namespace opencapture
