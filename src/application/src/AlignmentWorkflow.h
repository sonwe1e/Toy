#pragma once

#include "dvs/application/Alignment.h"

namespace dvs::application::detail {

[[nodiscard]] SequenceAlignmentSummary
summarizeSequenceAlignment(const SequenceAlignmentResult& result);

} // namespace dvs::application::detail
