#pragma once

#include "dvs/application/Ports.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/Result.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace dvs::media {

// FFmpeg-facing source inspection adapter. Its public surface is framework-neutral: callers see
// only normalized domain descriptors and application events, never AV* types or FFmpeg fields.
class MediaProbe final : public application::IMediaProbe {
public:
    explicit MediaProbe(std::size_t queueCapacity = 8U);
    ~MediaProbe() override;

    MediaProbe(const MediaProbe&) = delete;
    MediaProbe& operator=(const MediaProbe&) = delete;
    MediaProbe(MediaProbe&&) = delete;
    MediaProbe& operator=(MediaProbe&&) = delete;

    // A synchronous inspection is useful to fixture and integration tests. UI code should submit
    // through the port so FFmpeg I/O is kept off coordinator and GUI threads.
    [[nodiscard]] static domain::Result<domain::MediaDescriptor>
    inspect(const std::filesystem::path& sourcePath, domain::SourceId sourceId);

    [[nodiscard]] application::PortSubmitResult
    submit(const application::MediaProbeRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(const application::RequestContext& context) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::media
