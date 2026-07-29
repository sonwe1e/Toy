#pragma once

#include "dvs/application/Alignment.h"
#include "dvs/domain/Project.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace dvs::persistence {

class DerivedAlignmentCache final {
public:
    explicit DerivedAlignmentCache(std::filesystem::path cacheDirectory);

    [[nodiscard]] domain::Status
    store(std::string_view cacheKey,
          const domain::ValidatedComparisonSet& sources,
          std::span<const application::SequenceAlignmentResult> results,
          std::uint64_t revision) const;

    [[nodiscard]] domain::Result<std::vector<application::SequenceAlignmentResult>>
    load(std::string_view cacheKey, const domain::ValidatedComparisonSet& sources) const;

private:
    std::filesystem::path cacheDirectory_;
};

} // namespace dvs::persistence
