#include "dvs/application/PlaybackTrace.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace dvs::application {

void PlaybackTraceBuffer::setSink(ITraceSink* sink) noexcept {
    std::lock_guard lock(mutex_);
    sink_ = sink;
}

ITraceSink* PlaybackTraceBuffer::sink() const noexcept {
    std::lock_guard lock(mutex_);
    return sink_;
}

bool PlaybackTraceBuffer::record(const TraceEvent event) noexcept {
    std::lock_guard lock(mutex_);
    // Full when the producer leads the consumer by `capacity`; drop rather than block. One slot
    // is left unused so a full buffer reads as (head - tail == capacity) instead of the ambiguous
    // (head == tail) that an empty buffer also produces.
    if (head_ - tail_ >= kCapacity) {
        ++overflow_;
        return false;
    }
    buffer_[indexFor(head_)] = event;
    ++head_;
    return true;
}

std::size_t PlaybackTraceBuffer::drain(TraceEvent* out, const std::size_t max) noexcept {
    std::lock_guard lock(mutex_);
    const std::uint64_t available = head_ - tail_;
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(available, max));
    for (std::size_t i = 0U; i < count; ++i) {
        out[i] = buffer_[indexFor(tail_ + static_cast<std::uint64_t>(i))];
    }
    tail_ += static_cast<std::uint64_t>(count);
    return count;
}

std::size_t PlaybackTraceBuffer::drainToSink() noexcept {
    std::lock_guard lock(mutex_);
    // Forward the whole readable window to the sink on this (consumer/export) thread. The sink
    // performs any I/O here, never on a producer/worker thread. Events are copied out from under
    // the lock only conceptually — the sink is invoked while holding the lock, which is safe
    // because drainToSink is the sole consumer and the sink is not re-entrant into the buffer.
    const std::uint64_t available = head_ - tail_;
    for (std::uint64_t i = 0U; i < available; ++i) {
        if (sink_ != nullptr) {
            sink_->append(buffer_[indexFor(tail_ + i)]);
        }
    }
    if (sink_ != nullptr && overflow_ > 0U) {
        sink_->recordOverflow(overflow_);
    }
    tail_ = head_;
    return static_cast<std::size_t>(available);
}

std::uint64_t PlaybackTraceBuffer::overflowCount() const noexcept {
    std::lock_guard lock(mutex_);
    return overflow_;
}

void PlaybackTraceBuffer::reset() noexcept {
    std::lock_guard lock(mutex_);
    head_ = 0U;
    tail_ = 0U;
    overflow_ = 0U;
    sink_ = nullptr;
}

PlaybackTrace& PlaybackTrace::instance() noexcept {
    static PlaybackTrace trace;
    return trace;
}

void PlaybackTrace::installSink(ITraceSink* sink) noexcept {
    buffer_.setSink(sink);
}

bool PlaybackTrace::enabled() const noexcept {
    return now_ != nullptr;
}

void PlaybackTrace::enable(std::uint64_t (*nowMicroseconds)() noexcept) noexcept {
    now_ = nowMicroseconds;
}

void PlaybackTrace::disable() noexcept {
    now_ = nullptr;
}

void PlaybackTrace::record(const TraceEventKind kind,
                           const TraceIdentity& identity,
                           const std::uint64_t payload) {
    if (now_ == nullptr) {
        return;
    }
    TraceEvent event{
        .identity = identity,
        .kind = kind,
        .timestampMicroseconds = now_(),
        .payload = payload,
    };
    static_cast<void>(buffer_.record(event));
}

std::size_t PlaybackTrace::drain(TraceEvent* out, const std::size_t max) noexcept {
    return buffer_.drain(out, max);
}

std::size_t PlaybackTrace::drainToSink() noexcept {
    return buffer_.drainToSink();
}

std::uint64_t PlaybackTrace::overflowCount() const noexcept {
    return buffer_.overflowCount();
}

void PlaybackTrace::reset() noexcept {
    buffer_.reset();
    now_ = nullptr;
}

std::uint64_t traceNowMicroseconds() noexcept {
    // steady_clock is monotonic and not subject to system-time adjustments, so deltas between
    // trace records measure true elapsed wall-clock on the worker. The epoch is arbitrary.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

} // namespace dvs::application
