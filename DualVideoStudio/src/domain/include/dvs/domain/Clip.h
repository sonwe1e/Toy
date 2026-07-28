#pragma once

#include "dvs/domain/Identifiers.h"
#include "dvs/domain/Result.h"

#include <cstdint>
#include <string>

namespace dvs::domain {

struct FrameSpan final {
    FrameId first;
    std::int64_t endExclusive = 0;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::int64_t frameCount() const noexcept;
};

class FrameRange final {
public:
    [[nodiscard]] static Result<FrameRange> inclusive(FrameId first, FrameId last);

    [[nodiscard]] constexpr FrameId first() const noexcept {
        return first_;
    }

    [[nodiscard]] constexpr FrameId last() const noexcept {
        return last_;
    }

    [[nodiscard]] Result<FrameSpan> toHalfOpen() const;
    [[nodiscard]] bool isWithin(std::int64_t frameCount) const noexcept;
    [[nodiscard]] constexpr bool operator==(const FrameRange&) const noexcept = default;

private:
    constexpr FrameRange(const FrameId first, const FrameId last) noexcept
        : first_(first), last_(last) {}

    FrameId first_;
    FrameId last_;
};

struct Clip final {
    ClipId id;
    std::string name;
    std::string note;
    FrameRange range;
};

} // namespace dvs::domain
