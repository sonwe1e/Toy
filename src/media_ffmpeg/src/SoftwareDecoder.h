#pragma once

#include "dvs/application/FrameHandle.h"
#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/Result.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace dvs::platform {
class FrameBudget;
}

namespace dvs::media::internal {

struct DecodedFrame final {
    application::FrameHandle handle;
    domain::MediaTime presentationTime;
};

// One decoder owns one demuxer and codec context. It is intentionally used only by the provider
// actor that created it; no source pair ever shares seek or packet state.
class SoftwareDecoder final {
public:
    SoftwareDecoder(domain::SourceId sourceId,
                    domain::MediaDescriptor descriptor,
                    platform::FrameBudget& frameBudget,
                    const std::atomic<bool>* externalInterrupt = nullptr);
    ~SoftwareDecoder();

    SoftwareDecoder(const SoftwareDecoder&) = delete;
    SoftwareDecoder& operator=(const SoftwareDecoder&) = delete;
    SoftwareDecoder(SoftwareDecoder&&) = delete;
    SoftwareDecoder& operator=(SoftwareDecoder&&) = delete;

    [[nodiscard]] domain::Status open(const std::atomic<bool>& cancellationRequested);
    [[nodiscard]] domain::Result<DecodedFrame>
    decodeExact(domain::FrameId frameId, const std::atomic<bool>& cancellationRequested);
    [[nodiscard]] domain::Result<DecodedFrame>
    decodeSequential(domain::FrameId frameId, const std::atomic<bool>& cancellationRequested);

    // Internal diagnostic used by component tests to keep the sequential path honest.
    [[nodiscard]] std::uint64_t exactSeekCount() const noexcept;

    void requestInterrupt() noexcept;
    void close() noexcept;

private:
    [[nodiscard]] domain::Result<DecodedFrame>
    decodeInternal(domain::FrameId frameId,
                   const std::atomic<bool>& cancellationRequested,
                   bool continueSequentially,
                   bool allowTimelineRecovery);

    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media::internal
