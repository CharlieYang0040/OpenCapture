#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace opencapture {

// Non-blocking bounded queue for latency-sensitive video frames. When full,
// pushing a frame discards the oldest frame rather than stalling capture.
template <typename T>
class BoundedQueue final {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("queue capacity must be greater than zero");
    }

    bool PushDropOldest(T value) {
        std::scoped_lock lock(mutex_);
        const bool dropped = queue_.size() == capacity_;
        if (dropped) queue_.pop_front();
        queue_.push_back(std::move(value));
        return dropped;
    }

    [[nodiscard]] std::optional<T> TryPop() {
        std::scoped_lock lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<T> queue_;
};

} // namespace opencapture

