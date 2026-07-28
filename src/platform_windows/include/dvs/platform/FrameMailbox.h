#pragma once

#include "dvs/application/RequestContext.h"
#include "dvs/domain/Identifiers.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace dvs::platform {

class GpuFramePair;

enum class FrameMailboxPublishResult {
    Published,
    InvalidPair,
    DeviceGenerationMismatch,
    PlaybackScopeRejected,
    ResourceExhausted,
    Closed,
};

struct FrameMailboxPublication final {
    std::shared_ptr<const GpuFramePair> pair;
    std::uint64_t publicationSerial = 0U;
};

enum class FrameMailboxReadStatus {
    Available,
    Empty,
    DeviceGenerationMismatch,
    Contended,
    Closed,
};

struct FrameMailboxReadResult final {
    FrameMailboxReadStatus status = FrameMailboxReadStatus::Empty;
    std::optional<FrameMailboxPublication> publication;
};

enum class FrameMailboxDrawStatus {
    Current,
    Superseded,
    DeviceGenerationMismatch,
    Contended,
    Closed,
};

// A producer atomically replaces one complete GPU pair. The render consumer uses try_lock and
// reports contention instead of waiting for publication, generation changes, or shutdown.
class FrameMailbox final {
public:
    explicit FrameMailbox(domain::DeviceGeneration activeDeviceGeneration);
    ~FrameMailbox();

    FrameMailbox(const FrameMailbox&) = delete;
    FrameMailbox& operator=(const FrameMailbox&) = delete;
    FrameMailbox(FrameMailbox&&) = delete;
    FrameMailbox& operator=(FrameMailbox&&) = delete;

    [[nodiscard]] FrameMailboxPublishResult
    publish(const std::shared_ptr<const GpuFramePair>& pair) noexcept;

    // The returned shared ownership pins both sources for an in-flight draw. Call validateForDraw
    // immediately before issuing commands to reject a pair displaced meanwhile.
    [[nodiscard]] FrameMailboxReadResult
    tryLatest(domain::DeviceGeneration expectedDeviceGeneration) const noexcept;
    [[nodiscard]] FrameMailboxDrawStatus
    validateForDraw(const FrameMailboxPublication& publication,
                    domain::DeviceGeneration expectedDeviceGeneration) const noexcept;

    // A scoped clear cannot remove a newer request. Advancing the device generation atomically
    // clears every pair from the previous device and rejects non-monotonic generations.
    [[nodiscard]] bool clear(const application::PlaybackRequestContext& expectedScope) noexcept;
    [[nodiscard]] bool advanceDeviceGeneration(domain::DeviceGeneration deviceGeneration) noexcept;

    void shutdown() noexcept;
    [[nodiscard]] bool isClosed() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::platform
