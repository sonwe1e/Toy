#pragma once

#include "dvs/domain/Project.h"
#include "dvs/domain/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace dvs::persistence {

// Schema-1 conversion only. File reads, atomic publication, and source revalidation stay in the
// repository/platform layers; this class deliberately has no filesystem side effects or JSON
// library types in its public boundary.
class ProjectJson final {
public:
    [[nodiscard]] static domain::Result<std::string>
    encodeText(const domain::Project& project, const std::filesystem::path& projectPath);

    [[nodiscard]] static domain::Result<domain::Project>
    decodeText(std::string_view documentText, const std::filesystem::path& projectPath);
};

} // namespace dvs::persistence
