#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <vector>

namespace dvs::app {

struct StartupRequest final {
    enum class Kind {
        Empty,
        OpenProject,
        PlaySingle,
        Compare,
    };

    Kind kind = Kind::Empty;
    std::vector<std::filesystem::path> sources;

    [[nodiscard]] bool operator==(const StartupRequest&) const = default;
};

struct StartupRequestParseResult final {
    std::optional<StartupRequest> request;
    QString error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return request.has_value();
    }
};

[[nodiscard]] StartupRequestParseResult parseStartupRequest(const QStringList& arguments);
[[nodiscard]] QByteArray encodeStartupRequest(const StartupRequest& request);
[[nodiscard]] StartupRequestParseResult decodeStartupRequest(const QByteArray& payload);

} // namespace dvs::app
