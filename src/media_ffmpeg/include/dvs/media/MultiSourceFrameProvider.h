#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace dvs::platform {
class FrameBudget;
}

namespace dvs::media {

// Multi-source review adapter backed by independent software decoder contexts. It accepts bounded
// exact, sequential, and prefetch work, then emits only complete frame sets with one entry per
// loaded source.
class MultiSourceFrameProvider final : public application::IFrameProvider {
public:
    explicit MultiSourceFrameProvider(platform::FrameBudget& frameBudget,
                                      std::size_t requestCapacity = 16U,
                                      bool lowPriority = false);
    ~MultiSourceFrameProvider() override;

    MultiSourceFrameProvider(const MultiSourceFrameProvider&) = delete;
    MultiSourceFrameProvider& operator=(const MultiSourceFrameProvider&) = delete;
    MultiSourceFrameProvider(MultiSourceFrameProvider&&) = delete;
    MultiSourceFrameProvider& operator=(MultiSourceFrameProvider&&) = delete;

    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderOpenRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::AlignmentEstimateRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::SequenceAlignmentRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderCloseRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(const application::PlaybackRequestContext& context) noexcept override;

    // Component-test diagnostic for the persistent per-source worker invariant.
    [[nodiscard]] std::vector<std::thread::id> decodeWorkerIdsForTesting() const;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media
