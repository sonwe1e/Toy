#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <memory>

namespace dvs::persistence {

// A single-worker, bounded I/O adapter. It does not decide dirty state or save debounce; callers
// supply the project revision and destination in every save request.
class ProjectRepository final : public application::IProjectRepository {
public:
    explicit ProjectRepository(std::size_t queueCapacity = 16);
    ~ProjectRepository() override;

    ProjectRepository(const ProjectRepository&) = delete;
    ProjectRepository& operator=(const ProjectRepository&) = delete;
    ProjectRepository(ProjectRepository&&) = delete;
    ProjectRepository& operator=(ProjectRepository&&) = delete;

    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectLoadRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectRelinkRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::ProjectSaveRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(const application::RequestContext& context) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::persistence
