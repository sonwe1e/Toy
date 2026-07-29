#pragma once

#include "dvs/application/Ports.h"

#include <chrono>
#include <cstdint>
#include <memory>

namespace dvs::platform {

class SystemSteadyClock final : public application::ISteadyClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override;
};

// A single-worker deadline scheduler. schedule() only updates its ordered queue and wakes the
// worker; it never waits for the deadline or invokes an event sink on the caller's thread.
class SteadyDeadlineScheduler final : public application::IDeadlineScheduler {
public:
    SteadyDeadlineScheduler();
    ~SteadyDeadlineScheduler() override;

    SteadyDeadlineScheduler(const SteadyDeadlineScheduler&) = delete;
    SteadyDeadlineScheduler& operator=(const SteadyDeadlineScheduler&) = delete;
    SteadyDeadlineScheduler(SteadyDeadlineScheduler&&) = delete;
    SteadyDeadlineScheduler& operator=(SteadyDeadlineScheduler&&) = delete;

    // An unclaimed schedule with the same timer ID is replaced atomically. Once the worker has
    // claimed a deadline for delivery, rescheduling that ID returns Busy until its sink post has
    // returned. A true cancellation removes an unclaimed deadline before any post can occur.
    [[nodiscard]] application::PortSubmitResult
    schedule(const application::DeadlineRequest& request,
             std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] bool cancel(std::uint64_t timerId) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::platform
