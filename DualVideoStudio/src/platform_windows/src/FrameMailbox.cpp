#include "dvs/platform/FrameMailbox.h"

#include "GpuFramePair.h"
#include "GpuFrameResource.h"

#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

namespace dvs::platform {
namespace {

struct PlaybackScopeKey final {
    domain::SessionEpoch sessionEpoch;
    domain::PlaybackGeneration playbackGeneration;
};

[[nodiscard]] PlaybackScopeKey
scopeKey(const application::PlaybackRequestContext& context) noexcept {
    return PlaybackScopeKey{
        .sessionEpoch = context.request.sessionEpoch,
        .playbackGeneration = context.playbackGeneration,
    };
}

[[nodiscard]] int compareScopes(const PlaybackScopeKey& left,
                                const PlaybackScopeKey& right) noexcept {
    if (left.sessionEpoch < right.sessionEpoch) {
        return -1;
    }
    if (left.sessionEpoch > right.sessionEpoch) {
        return 1;
    }
    if (left.playbackGeneration < right.playbackGeneration) {
        return -1;
    }
    if (left.playbackGeneration > right.playbackGeneration) {
        return 1;
    }
    return 0;
}

} // namespace

class FrameMailbox::Impl final {
public:
    explicit Impl(const domain::DeviceGeneration activeDeviceGeneration) noexcept
        : activeDeviceGeneration_(activeDeviceGeneration) {}

    [[nodiscard]] FrameMailboxPublishResult
    publish(const std::shared_ptr<const GpuFramePair>& pair) noexcept {
        if (!pair) {
            return FrameMailboxPublishResult::InvalidPair;
        }

        std::shared_ptr<const GpuFramePair> displaced;
        {
            const std::lock_guard lock(mutex_);
            if (closed_.load(std::memory_order_relaxed)) {
                return FrameMailboxPublishResult::Closed;
            }
            if (pair->context().deviceGeneration != activeDeviceGeneration_ ||
                pair->frameA()->deviceGeneration() != activeDeviceGeneration_ ||
                pair->frameB()->deviceGeneration() != activeDeviceGeneration_) {
                return FrameMailboxPublishResult::DeviceGenerationMismatch;
            }
            if (!acceptSession(pair->context().playback.request.sessionId)) {
                return FrameMailboxPublishResult::PlaybackScopeRejected;
            }

            const PlaybackScopeKey incomingScope = scopeKey(pair->context().playback);
            if (playbackHighWater_.has_value()) {
                const int order = compareScopes(incomingScope, *playbackHighWater_);
                if (order < 0 || (order == 0 && playbackScopeTombstoned_)) {
                    return FrameMailboxPublishResult::PlaybackScopeRejected;
                }
            }
            if (publicationSerial_ == std::numeric_limits<std::uint64_t>::max()) {
                return FrameMailboxPublishResult::ResourceExhausted;
            }

            displaced = std::move(pair_);
            pair_ = pair;
            playbackHighWater_ = incomingScope;
            playbackScopeTombstoned_ = false;
            ++publicationSerial_;
        }

        // Native resources and FrameBudget reservations are never released while the state mutex
        // is held. An in-flight render publication can continue pinning the displaced pair.
        displaced.reset();
        return FrameMailboxPublishResult::Published;
    }

    [[nodiscard]] FrameMailboxReadResult
    tryLatest(const domain::DeviceGeneration expectedDeviceGeneration) const noexcept {
        if (closed_.load(std::memory_order_acquire)) {
            return FrameMailboxReadResult{.status = FrameMailboxReadStatus::Closed};
        }

        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return FrameMailboxReadResult{.status = FrameMailboxReadStatus::Contended};
        }
        if (closed_.load(std::memory_order_relaxed)) {
            return FrameMailboxReadResult{.status = FrameMailboxReadStatus::Closed};
        }
        if (activeDeviceGeneration_ != expectedDeviceGeneration) {
            return FrameMailboxReadResult{
                .status = FrameMailboxReadStatus::DeviceGenerationMismatch,
            };
        }
        if (!pair_) {
            return FrameMailboxReadResult{.status = FrameMailboxReadStatus::Empty};
        }

        return FrameMailboxReadResult{
            .status = FrameMailboxReadStatus::Available,
            .publication =
                FrameMailboxPublication{
                    .pair = pair_,
                    .publicationSerial = publicationSerial_,
                },
        };
    }

    [[nodiscard]] FrameMailboxDrawStatus
    validateForDraw(const FrameMailboxPublication& publication,
                    const domain::DeviceGeneration expectedDeviceGeneration) const noexcept {
        if (closed_.load(std::memory_order_acquire)) {
            return FrameMailboxDrawStatus::Closed;
        }

        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return FrameMailboxDrawStatus::Contended;
        }
        if (closed_.load(std::memory_order_relaxed)) {
            return FrameMailboxDrawStatus::Closed;
        }
        if (activeDeviceGeneration_ != expectedDeviceGeneration) {
            return FrameMailboxDrawStatus::DeviceGenerationMismatch;
        }
        if (!publication.pair || publication.publicationSerial != publicationSerial_ ||
            pair_ != publication.pair) {
            return FrameMailboxDrawStatus::Superseded;
        }
        return FrameMailboxDrawStatus::Current;
    }

    [[nodiscard]] bool clear(const application::PlaybackRequestContext& expectedScope) noexcept {
        std::shared_ptr<const GpuFramePair> displaced;
        {
            const std::lock_guard lock(mutex_);
            if (closed_.load(std::memory_order_relaxed) ||
                !acceptSession(expectedScope.request.sessionId)) {
                return false;
            }

            const PlaybackScopeKey clearingScope = scopeKey(expectedScope);
            if (playbackHighWater_.has_value()) {
                const int order = compareScopes(clearingScope, *playbackHighWater_);
                if (order < 0 || (order == 0 && playbackScopeTombstoned_ && !pair_)) {
                    return false;
                }
            }

            playbackHighWater_ = clearingScope;
            playbackScopeTombstoned_ = true;
            displaced = std::move(pair_);
        }
        displaced.reset();
        return true;
    }

    [[nodiscard]] bool
    advanceDeviceGeneration(const domain::DeviceGeneration deviceGeneration) noexcept {
        std::shared_ptr<const GpuFramePair> displaced;
        {
            const std::lock_guard lock(mutex_);
            if (closed_.load(std::memory_order_relaxed) ||
                deviceGeneration <= activeDeviceGeneration_) {
                return false;
            }
            activeDeviceGeneration_ = deviceGeneration;
            displaced = std::move(pair_);
        }
        displaced.reset();
        return true;
    }

    void shutdown() noexcept {
        std::shared_ptr<const GpuFramePair> displaced;
        {
            const std::lock_guard lock(mutex_);
            if (closed_.load(std::memory_order_relaxed)) {
                return;
            }
            closed_.store(true, std::memory_order_release);
            playbackScopeTombstoned_ = true;
            displaced = std::move(pair_);
        }
        displaced.reset();
    }

    [[nodiscard]] bool isClosed() const noexcept {
        return closed_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] bool acceptSession(const domain::SessionId sessionId) noexcept {
        if (!sessionId_.has_value()) {
            sessionId_ = sessionId;
            return true;
        }
        return *sessionId_ == sessionId;
    }

    mutable std::mutex mutex_;
    domain::DeviceGeneration activeDeviceGeneration_;
    std::optional<domain::SessionId> sessionId_;
    std::optional<PlaybackScopeKey> playbackHighWater_;
    bool playbackScopeTombstoned_ = false;
    std::uint64_t publicationSerial_ = 0U;
    std::shared_ptr<const GpuFramePair> pair_;
    std::atomic<bool> closed_{false};
};

FrameMailbox::FrameMailbox(const domain::DeviceGeneration activeDeviceGeneration)
    : impl_(std::make_unique<Impl>(activeDeviceGeneration)) {}

FrameMailbox::~FrameMailbox() {
    shutdown();
}

FrameMailboxPublishResult
FrameMailbox::publish(const std::shared_ptr<const GpuFramePair>& pair) noexcept {
    return impl_->publish(pair);
}

FrameMailboxReadResult
FrameMailbox::tryLatest(const domain::DeviceGeneration expectedDeviceGeneration) const noexcept {
    return impl_->tryLatest(expectedDeviceGeneration);
}

FrameMailboxDrawStatus FrameMailbox::validateForDraw(
    const FrameMailboxPublication& publication,
    const domain::DeviceGeneration expectedDeviceGeneration) const noexcept {
    return impl_->validateForDraw(publication, expectedDeviceGeneration);
}

bool FrameMailbox::clear(const application::PlaybackRequestContext& expectedScope) noexcept {
    return impl_->clear(expectedScope);
}

bool FrameMailbox::advanceDeviceGeneration(
    const domain::DeviceGeneration deviceGeneration) noexcept {
    return impl_->advanceDeviceGeneration(deviceGeneration);
}

void FrameMailbox::shutdown() noexcept {
    impl_->shutdown();
}

bool FrameMailbox::isClosed() const noexcept {
    return impl_->isClosed();
}

} // namespace dvs::platform
