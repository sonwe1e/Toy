#pragma once

#include <queue>

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>

namespace dvs::persistence::internal {

enum class IoSubmitResult {
    kAccepted,
    kBusy,
    kClosed,
};

// Private execution primitive shared by persistence adapters. close() drains already accepted
// tasks before joining so each task can publish its own canceled terminal event.
class SerialIoActor final {
public:
    explicit SerialIoActor(const std::size_t queueCapacity)
        : queueCapacity_(queueCapacity == 0U ? 1U : queueCapacity), worker_([this] { run(); }) {}

    ~SerialIoActor() {
        close();
    }

    SerialIoActor(const SerialIoActor&) = delete;
    SerialIoActor& operator=(const SerialIoActor&) = delete;
    SerialIoActor(SerialIoActor&&) = delete;
    SerialIoActor& operator=(SerialIoActor&&) = delete;

    [[nodiscard]] IoSubmitResult submit(std::function<void()> task) {
        std::scoped_lock lock(mutex_);
        if (closed_) {
            return IoSubmitResult::kClosed;
        }
        if (tasks_.size() >= queueCapacity_) {
            return IoSubmitResult::kBusy;
        }
        tasks_.push(std::move(task));
        condition_.notify_one();
        return IoSubmitResult::kAccepted;
    }

    void close() noexcept {
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run() noexcept {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return closed_ || !tasks_.empty(); });
                if (tasks_.empty()) {
                    if (closed_) {
                        return;
                    }
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            try {
                task();
            } catch (...) {
                // Task adapters translate expected I/O and serialization failures themselves.
                // This guard prevents a single unexpected exception from killing the I/O actor.
            }
        }
    }

    std::size_t queueCapacity_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> tasks_;
    bool closed_ = false;
    std::thread worker_;
};

} // namespace dvs::persistence::internal
