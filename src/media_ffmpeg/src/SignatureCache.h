#pragma once

#include "dvs/application/Alignment.h"
#include "dvs/domain/MediaDescriptor.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dvs::media::internal {

// Process-local analysis evidence cache. The key deliberately describes the source content and
// every normalization choice that can change a signature; request/session identity is excluded.
class SignatureCache final {
public:
    [[nodiscard]] std::optional<application::FrameLumaSignature>
    find(const domain::MediaDescriptor& descriptor, domain::FrameId frameId) const;

    [[nodiscard]] std::optional<std::vector<application::FrameLumaSignature>>
    findRange(const domain::MediaDescriptor& descriptor, std::int64_t frameCount) const;

    void store(const domain::MediaDescriptor& descriptor,
               application::FrameLumaSignature signature);
    void storeRange(const domain::MediaDescriptor& descriptor,
                    std::vector<application::FrameLumaSignature> signatures);

private:
    [[nodiscard]] static std::optional<std::string>
    makeKey(const domain::MediaDescriptor& descriptor);

    mutable std::mutex mutex_;
    std::map<std::string, std::map<std::int64_t, application::FrameLumaSignature>, std::less<>>
        entries_;
};

} // namespace dvs::media::internal
