#include "SignatureCache.h"

extern "C" {
#include <libavcodec/version.h>
#include <libavutil/version.h>
}

#include <utility>

namespace dvs::media::internal {
namespace {

inline constexpr std::uint32_t kSignatureAlgorithmVersion = 2U;
inline constexpr std::uint32_t kNormalizationVersion = 2U;

} // namespace

std::optional<std::string> SignatureCache::makeKey(const domain::MediaDescriptor& descriptor) {
    if (!descriptor.sourceIdentity.has_value() || !descriptor.sourceIdentity->isComplete() ||
        !descriptor.extent.isValid()) {
        return std::nullopt;
    }
    return descriptor.sourceIdentity->fingerprintSha256 +
           "|rotation=" + std::to_string(descriptor.rotationDegrees) +
           "|sar=" + std::to_string(descriptor.sampleAspectRatio.numerator) + ":" +
           std::to_string(descriptor.sampleAspectRatio.denominator) + "|" +
           std::to_string(descriptor.extent.width) + "x" +
           std::to_string(descriptor.extent.height) +
           "|algorithm=" + std::to_string(kSignatureAlgorithmVersion) +
           "|normalization=" + std::to_string(kNormalizationVersion) +
           "|avcodec=" + std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." +
           std::to_string(LIBAVCODEC_VERSION_MINOR) +
           "|avutil=" + std::to_string(LIBAVUTIL_VERSION_MAJOR) + "." +
           std::to_string(LIBAVUTIL_VERSION_MINOR);
}

std::optional<application::FrameLumaSignature>
SignatureCache::find(const domain::MediaDescriptor& descriptor,
                     const domain::FrameId frameId) const {
    const std::optional<std::string> key = makeKey(descriptor);
    if (!key.has_value() || !frameId.isValid()) {
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    const auto source = entries_.find(*key);
    if (source == entries_.end()) {
        return std::nullopt;
    }
    const auto signature = source->second.find(frameId.value());
    return signature == source->second.end()
               ? std::nullopt
               : std::optional<application::FrameLumaSignature>{signature->second};
}

std::optional<std::vector<application::FrameLumaSignature>>
SignatureCache::findRange(const domain::MediaDescriptor& descriptor,
                          const std::int64_t frameCount) const {
    const std::optional<std::string> key = makeKey(descriptor);
    if (!key.has_value() || frameCount <= 0) {
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    const auto source = entries_.find(*key);
    if (source == entries_.end() || source->second.size() < static_cast<std::size_t>(frameCount)) {
        return std::nullopt;
    }
    std::vector<application::FrameLumaSignature> result;
    result.reserve(static_cast<std::size_t>(frameCount));
    for (std::int64_t frame = 0; frame < frameCount; ++frame) {
        const auto signature = source->second.find(frame);
        if (signature == source->second.end()) {
            return std::nullopt;
        }
        result.push_back(signature->second);
    }
    return result;
}

void SignatureCache::store(const domain::MediaDescriptor& descriptor,
                           application::FrameLumaSignature signature) {
    const std::optional<std::string> key = makeKey(descriptor);
    if (!key.has_value() || !signature.isValid()) {
        return;
    }
    std::scoped_lock lock(mutex_);
    entries_[*key].insert_or_assign(signature.frameId.value(), std::move(signature));
}

void SignatureCache::storeRange(const domain::MediaDescriptor& descriptor,
                                std::vector<application::FrameLumaSignature> signatures) {
    const std::optional<std::string> key = makeKey(descriptor);
    if (!key.has_value()) {
        return;
    }
    std::map<std::int64_t, application::FrameLumaSignature> validated;
    for (application::FrameLumaSignature& signature : signatures) {
        if (!signature.isValid()) {
            return;
        }
        validated.insert_or_assign(signature.frameId.value(), std::move(signature));
    }
    std::scoped_lock lock(mutex_);
    auto& cached = entries_[*key];
    cached.insert(std::make_move_iterator(validated.begin()),
                  std::make_move_iterator(validated.end()));
}

} // namespace dvs::media::internal
