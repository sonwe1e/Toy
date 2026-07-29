#include "dvs/platform/SteadyDeadlineScheduler.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

namespace dvs::platform {
namespace {

struct DeadlineKey final {
    std::chrono::steady_clock::time_point due;
    std::uint64_t sequence = 0;

    [[nodiscard]] bool operator<(const DeadlineKey& other) const noexcept {
        if (due < other.due) {
            return true;
        }
        if (other.due < due) {
            return false;
        }
        return sequence < other.sequence;
    }
};

struct ScheduledDeadline final {
    application::DeadlineRequest request;
    std::weak_ptr<application::IApplicationEventSink> events;
};

} // namespace

std::chrono::steady_clock::time_point SystemSteadyClock::now() const noexcept {
    return std::chrono::steady_clock::now();
}

class SteadyDeadlineScheduler::Impl final {
public:
    Impl() : worker_([this] { run(); }) {}

    ~Impl() {
        shutdown();
    }

    [[nodiscard]] application::PortSubmitResult
    schedule(const application::DeadlineRequest& request,
             const std::shared_ptr<application::IApplicationEventSink>& events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }

        try {
            {
                std::lock_guard lock(mutex_);
                if (stopping_) {
                    return application::PortSubmitResult::Closed;
                }
                if (dispatchingTimerId_.has_value() && *dispatchingTimerId_ == request.timerId) {
                    return application::PortSubmitResult::Busy;
                }
                if (nextSequence_ == std::numeric_limits<std::uint64_t>::max()) {
                    return application::PortSubmitResult::Busy;
                }

                const DeadlineKey key{
                    .due = request.due,
                    .sequence = nextSequence_++,
                };
                const auto [scheduled, inserted] = deadlines_.emplace(
                    key,
                    ScheduledDeadline{
                        .request = request,
                        .events = std::weak_ptr<application::IApplicationEventSink>{events},
                    });
                if (!inserted) {
                    return application::PortSubmitResult::Busy;
                }

                const auto existing = deadlinesByTimerId_.find(request.timerId);
                if (existing == deadlinesByTimerId_.end()) {
                    try {
                        static_cast<void>(deadlinesByTimerId_.emplace(request.timerId, scheduled));
                    } catch (...) {
                        deadlines_.erase(scheduled);
                        throw;
                    }
                } else {
                    const DeadlineIterator previous = existing->second;
                    existing->second = scheduled;
                    deadlines_.erase(previous);
                }
            }
            condition_.notify_all();
            return application::PortSubmitResult::Accepted;
        } catch (...) {
            return application::PortSubmitResult::Busy;
        }
    }

    [[nodiscard]] bool cancel(const std::uint64_t timerId) noexcept {
        {
            std::lock_guard lock(mutex_);
            const auto scheduled = deadlinesByTimerId_.find(timerId);
            if (scheduled == deadlinesByTimerId_.end()) {
                return false;
            }
            deadlines_.erase(scheduled->second);
            deadlinesByTimerId_.erase(scheduled);
        }
        condition_.notify_all();
        return true;
    }

private:
    using DeadlineMap = std::map<DeadlineKey, ScheduledDeadline>;
    using DeadlineIterator = DeadlineMap::iterator;

    static void postDeadline(const ScheduledDeadline& scheduled) noexcept {
        const std::shared_ptr<application::IApplicationEventSink> events = scheduled.events.lock();
        if (!events) {
            return;
        }

        static_cast<void>(events->postCritical(application::ApplicationEvent{
            application::DeadlineElapsed{
                .context = scheduled.request.context,
                .timerId = scheduled.request.timerId,
            },
        }));
    }

    void run() noexcept {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            if (deadlines_.empty()) {
                condition_.wait(lock, [this] { return stopping_ || !deadlines_.empty(); });
                continue;
            }

            const std::chrono::steady_clock::time_point due = deadlines_.begin()->first.due;
            if (due > std::chrono::steady_clock::now()) {
                static_cast<void>(condition_.wait_until(lock, due));
                continue;
            }

            const DeadlineIterator scheduled = deadlines_.begin();
            ScheduledDeadline deadline = std::move(scheduled->second);
            const std::uint64_t timerId = deadline.request.timerId;
            const auto active = deadlinesByTimerId_.find(timerId);
            if (active == deadlinesByTimerId_.end() || active->second != scheduled) {
                deadlines_.erase(scheduled);
                continue;
            }

            deadlinesByTimerId_.erase(active);
            deadlines_.erase(scheduled);
            dispatchingTimerId_ = timerId;

            lock.unlock();
            postDeadline(deadline);
            lock.lock();

            if (dispatchingTimerId_.has_value() && *dispatchingTimerId_ == timerId) {
                dispatchingTimerId_.reset();
            }
        }
    }

    void shutdown() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            deadlinesByTimerId_.clear();
            deadlines_.clear();
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    DeadlineMap deadlines_;
    std::unordered_map<std::uint64_t, DeadlineIterator> deadlinesByTimerId_;
    std::optional<std::uint64_t> dispatchingTimerId_;
    std::uint64_t nextSequence_ = 0;
    bool stopping_ = false;
    std::thread worker_;
};

SteadyDeadlineScheduler::SteadyDeadlineScheduler() : impl_(std::make_unique<Impl>()) {}

SteadyDeadlineScheduler::~SteadyDeadlineScheduler() = default;

application::PortSubmitResult
SteadyDeadlineScheduler::schedule(const application::DeadlineRequest& request,
                                  std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->schedule(request, events);
}

bool SteadyDeadlineScheduler::cancel(const std::uint64_t timerId) noexcept {
    return impl_->cancel(timerId);
}

} // namespace dvs::platform
