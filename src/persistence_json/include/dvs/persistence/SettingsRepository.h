#pragma once

#include "dvs/application/Ports.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace dvs::persistence {

// Passing a non-empty settingsFile is an explicit test seam. Production callers use the Windows
// LocalAppData path supplied by WindowsPaths and never write settings beside the executable.
class SettingsRepository final : public application::ISettingsRepository {
public:
    explicit SettingsRepository(std::filesystem::path settingsFile = {},
                                std::size_t queueCapacity = 16);
    ~SettingsRepository() override;

    SettingsRepository(const SettingsRepository&) = delete;
    SettingsRepository& operator=(const SettingsRepository&) = delete;
    SettingsRepository(SettingsRepository&&) = delete;
    SettingsRepository& operator=(SettingsRepository&&) = delete;

    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsLoadRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsSaveRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override;
    void cancel(const application::RequestContext& context) noexcept override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::persistence
