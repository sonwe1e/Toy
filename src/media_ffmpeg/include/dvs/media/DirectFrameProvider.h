#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <memory>

namespace dvs::platform {
class FrameBudget;
}

namespace dvs::media {

// Direct review adapter backed by two independent software decoder contexts. It accepts bounded
// exact, sequential, and prefetch work, then emits only complete A/B frame pairs.
class DirectFrameProvider final : public application::IFrameProvider {
public:
    explicit DirectFrameProvider(platform::FrameBudget& frameBudget,
                                 std::size_t requestCapacity = 16U);
    ~DirectFrameProvider() override;

    DirectFrameProvider(const DirectFrameProvider&) = delete;
    DirectFrameProvider& operator=(const DirectFrameProvider&) = delete;
    DirectFrameProvider(DirectFrameProvider&&) = delete;
    DirectFrameProvider& operator=(DirectFrameProvider&&) = delete;

    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderOpenRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderCloseRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(const application::PlaybackRequestContext& context) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media
