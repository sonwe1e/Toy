#pragma once

#include "dvs/domain/Identifiers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace dvs::application {

// The identity scope attached to every trace event. Mirrors the plan's OperationIdentity
// (03_目标架构设计.md §4) so a trace can prove command exactly-once, ACK-before-commit, and
// no stale commit: any revision mismatch between an async result and current state means the
// result is stale and must not commit. Topology/timeline revisions are tracked from Phase 0 so
// the trace can already detect stale commits across topology/timeline changes; they become
// load-bearing for Phase 1+ identity but are populated here.
struct TraceIdentity final {
    domain::SessionId session{0};
    domain::SessionEpoch epoch{0};
    domain::TopologyRevision topology{0};
    domain::TimelineRevision timeline{0};
    domain::AlignmentRevision alignment{0};
    domain::PlaybackGeneration generation{0};
    domain::DeviceGeneration device{0};
    domain::RequestId request{0};
    std::optional<domain::CommandId> command{};

    [[nodiscard]] bool operator==(const TraceIdentity&) const = default;
};

enum class TraceEventKind : std::uint8_t {
    CommandAccepted,
    CommandRejected,
    ProviderSubmitted,
    ProviderCanceled,
    FrameSetReady,
    ProviderTerminal,
    RenderPublished,
    PresentationAcknowledged,
    SnapshotCommitted,
    CommandTerminal,
    DecoderSeek,
    DecoderReopen,
    CacheHit,
    DeviceGenerationChanged,
};

// A single fixed-size trace event. Kept small and trivially copyable so it can live in a
// lock-free ring buffer with no heap allocation and no variable-length payloads on the hot path.
// `payload` carries event-specific data (e.g. a frame id, a request kind) and is interpreted
// per `kind`; see trace-schema.md for the encoding.
struct TraceEvent final {
    TraceIdentity identity;
    TraceEventKind kind{};
    std::uint64_t timestampMicroseconds{0};
    std::uint64_t payload{0};

    [[nodiscard]] bool operator==(const TraceEvent&) const = default;
};

// A platform-owned sink drains trace events for export. The application layer never performs
// disk I/O for tracing; it only hands completed events to a sink on a non-worker thread
// (see TraceSink.h in platform_windows). A null sink means tracing is disabled.
class ITraceSink {
public:
    virtual ~ITraceSink() = default;
    virtual void append(const TraceEvent& event) = 0;
    virtual void recordOverflow(std::uint64_t lostCount) = 0;
};

// Bounded multi-producer/single-consumer ring buffer. Events are produced by several threads
// (the coordinator worker and, for media-layer events, each per-source decode worker) and
// consumed later by a single export thread. The buffer is protected by a mutex: the critical
// section is a single array write (nanoseconds), so producer contention is negligible and the
// coordinator worker never blocks on I/O — record() never touches the sink. A dropped event
// (buffer full) is preferred over blocking, and the lost count is reported so export can flag an
// incomplete trace. This is a diagnostic facility that is disabled by default, so correctness
// and clarity are favoured over a lock-free design.
class PlaybackTraceBuffer final {
public:
    static constexpr std::size_t kCapacity = 16384U;
    static_assert((kCapacity & (kCapacity - 1U)) == 0U, "capacity must be a power of two");

    void setSink(ITraceSink* sink) noexcept;
    [[nodiscard]] ITraceSink* sink() const noexcept;

    // Records an event from any thread. Returns false (and counts it as lost) when the buffer
    // is full so producers never block. Never performs I/O.
    [[nodiscard]] bool record(TraceEvent event) noexcept;

    // Drains up to `max` events into `out`, returning the count written. Single-consumer: must
    // only be called from the export thread. Events are removed from the buffer as they are read.
    std::size_t drain(TraceEvent* out, std::size_t max) noexcept;

    // Convenience for the export path: drains events and forwards each to the sink on the
    // consumer thread (never the worker). Returns count dequeued.
    std::size_t drainToSink() noexcept;

    [[nodiscard]] std::uint64_t overflowCount() const noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t indexFor(std::uint64_t position) noexcept {
        return static_cast<std::size_t>(position & (kCapacity - 1U));
    }

    mutable std::mutex mutex_;
    ITraceSink* sink_ = nullptr;
    std::array<TraceEvent, kCapacity> buffer_;
    std::uint64_t head_ = 0U;
    std::uint64_t tail_ = 0U;
    std::uint64_t overflow_ = 0U;
};

// Process-global trace buffer. Constructed once and accessed via instance(); tests may replace
// it with reset() between cases. Thread-safe for the single-producer/single-consumer contract
// declared on PlaybackTraceBuffer.
class PlaybackTrace final {
public:
    static PlaybackTrace& instance() noexcept;

    void installSink(ITraceSink* sink) noexcept;
    [[nodiscard]] bool enabled() const noexcept;

    // Enables recording with the given monotonic time source (microseconds since an arbitrary
    // epoch). Disabling drops all subsequent records and is the default, so tracing has zero
    // steady-state cost unless a gate explicitly enables it.
    void enable(std::uint64_t (*nowMicroseconds)() noexcept) noexcept;
    void disable() noexcept;

    void record(TraceEventKind kind, const TraceIdentity& identity, std::uint64_t payload = 0U);

    std::size_t drain(TraceEvent* out, std::size_t max) noexcept;
    std::size_t drainToSink() noexcept;
    [[nodiscard]] std::uint64_t overflowCount() const noexcept;
    void reset() noexcept;

private:
    PlaybackTrace() = default;

    std::uint64_t (*now_)() noexcept = nullptr;
    PlaybackTraceBuffer buffer_;
};

// Convenience: resolve the current monotonic microsecond timestamp for a trace record. Defined
// in the application layer using std::chrono so the domain stays clock-free.
[[nodiscard]] std::uint64_t traceNowMicroseconds() noexcept;

} // namespace dvs::application
