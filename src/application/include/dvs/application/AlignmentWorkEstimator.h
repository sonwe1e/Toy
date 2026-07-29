#pragma once

#include "dvs/application/Ports.h"

namespace dvs::application {

[[nodiscard]] AlignmentWorkEstimate
estimateAlignmentWork(const AlignmentEstimateRequest& request) noexcept;

[[nodiscard]] AlignmentWorkEstimate
estimateAlignmentWork(const SequenceAlignmentRequest& request) noexcept;

} // namespace dvs::application
