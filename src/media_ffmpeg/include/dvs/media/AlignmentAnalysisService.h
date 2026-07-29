#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <memory>

namespace dvs::platform {
class FrameBudget;
}

namespace dvs::media {

// Runs alignment jobs on a decode provider that is physically separate from playback. The
// service owns one low-priority worker and a bounded admission queue.
class AlignmentAnalysisService final : public application::IAlignmentAnalysisService {
public:
    explicit AlignmentAnalysisService(platform::FrameBudget& frameBudget,
                                      std::size_t queueCapacity = 2U);
    ~AlignmentAnalysisService() override;

    AlignmentAnalysisService(const AlignmentAnalysisService&) = delete;
    AlignmentAnalysisService& operator=(const AlignmentAnalysisService&) = delete;
    AlignmentAnalysisService(AlignmentAnalysisService&&) = delete;
    AlignmentAnalysisService& operator=(AlignmentAnalysisService&&) = delete;

    [[nodiscard]] application::PortSubmitResult
    submit(const application::AlignmentEstimateRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::SequenceAlignmentRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(application::AlignmentAnalysisJobId jobId) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media
