#pragma once

#include "dvs/application/RequestContext.h"
#include "dvs/domain/ComparisonSource.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dvs::application {

struct OpenComparisonSource final {
    std::filesystem::path path;
    domain::ComparisonRole role = domain::ComparisonRole::kPrediction;
    std::string displayName;
};

enum class OpenReviewIntent : std::uint8_t {
    NewReview,
    ReplaceSources,
    ChangeReference,
};

struct OpenComparisonCommand final {
    CommandContext context;
    std::vector<OpenComparisonSource> sources;
    OpenReviewIntent intent = OpenReviewIntent::NewReview;
    bool preserveDisplayedTime = false;
};

struct OpenDirectComparisonCommand final {
    CommandContext context;
    std::vector<domain::ComparisonSource> sources;
};

struct CloseSessionCommand final {
    CommandContext context;
};

} // namespace dvs::application
