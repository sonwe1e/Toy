#pragma once

#include "dvs/domain/MediaDescriptor.h"

#include <cstdint>

namespace dvs::domain {

class ValidatedSourcePair final {
public:
    [[nodiscard]] const MediaDescriptor& sourceA() const noexcept;
    [[nodiscard]] const MediaDescriptor& sourceB() const noexcept;
    [[nodiscard]] const std::optional<RationalRate>& canonicalRate() const noexcept;
    [[nodiscard]] std::int64_t canonicalFrameCount() const noexcept;
    [[nodiscard]] bool hasEstimatedFrameCount() const noexcept;

private:
    friend class SourcePairValidator;

    ValidatedSourcePair(MediaDescriptor sourceA, MediaDescriptor sourceB);

    MediaDescriptor sourceA_;
    MediaDescriptor sourceB_;
};

class SourcePairValidator final {
public:
    [[nodiscard]] static Result<ValidatedSourcePair> validate(const MediaDescriptor& sourceA,
                                                              const MediaDescriptor& sourceB);
};

} // namespace dvs::domain
