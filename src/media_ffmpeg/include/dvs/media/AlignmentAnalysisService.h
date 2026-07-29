#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <memory>

namespace dvs::media {

// Runs alignment jobs on dedicated luma-signature decoders. The service owns one low-priority
// worker, a bounded admission queue, and a process-local signature cache.
class AlignmentAnalysisService final : public application::IAlignmentAnalysisService {
public:
    explicit AlignmentAnalysisService(std::size_t queueCapacity = 2U);
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

    [[nodiscard]] std::uint64_t decodedSignatureCountForTesting() const noexcept;
    [[nodiscard]] std::uint64_t openSessionCountForTesting() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media
