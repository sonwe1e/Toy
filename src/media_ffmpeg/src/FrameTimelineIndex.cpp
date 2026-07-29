#include "FrameTimelineIndex.h"

#include "AvRaii.h"
#include "PresentationIndexCache.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dvs::media::internal {
namespace {

struct InterruptState final {
    TimelineCancellation cancellation;
};

[[nodiscard]] int interruptCallback(void* const opaque) noexcept {
    const auto* const state = static_cast<const InterruptState*>(opaque);
    return state != nullptr && state->cancellation.requested() ? 1 : 0;
}

[[nodiscard]] std::string ffmpegError(const int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg returned error " + std::to_string(errorCode) + ".";
    }
    return std::string{buffer};
}

[[nodiscard]] domain::MediaError timelineError(const domain::SourceId sourceId,
                                               const domain::MediaOperation operation,
                                               std::string technicalDetail) {
    return domain::makeMediaError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                  operation,
                                  sourceId,
                                  true,
                                  std::move(technicalDetail));
}

[[nodiscard]] domain::MediaError cancellationError(const TimestampIndexRequest& request) {
    const domain::MediaErrorCode code = request.operation == domain::MediaOperation::kMediaProbe
                                            ? domain::MediaErrorCode::kMediaProbeFailed
                                            : domain::MediaErrorCode::kMediaDecodeFailed;
    return domain::makeMediaError(code,
                                  request.operation,
                                  request.sourceId,
                                  true,
                                  "Presentation timestamp indexing was canceled.");
}

[[nodiscard]] domain::MediaError cancellationError(const CfrVerificationRequest& request) {
    const domain::MediaErrorCode code = request.operation == domain::MediaOperation::kMediaProbe
                                            ? domain::MediaErrorCode::kMediaProbeFailed
                                            : domain::MediaErrorCode::kMediaDecodeFailed;
    return domain::makeMediaError(code,
                                  request.operation,
                                  request.sourceId,
                                  true,
                                  "Constant-frame-rate verification was canceled.");
}

[[nodiscard]] domain::MediaError cfrError(const CfrVerificationRequest& request,
                                          std::string technicalDetail) {
    return domain::makeMediaError(domain::MediaErrorCode::kInvalidCfrTiming,
                                  request.operation,
                                  request.sourceId,
                                  true,
                                  std::move(technicalDetail));
}

[[nodiscard]] bool isPositive(const TimelineRational value) noexcept {
    return value.numerator > 0 && value.denominator > 0;
}

[[nodiscard]] TimelineRational normalize(const TimelineRational value) noexcept {
    const int divisor = std::gcd(value.numerator, value.denominator);
    return TimelineRational{
        .numerator = value.numerator / divisor,
        .denominator = value.denominator / divisor,
    };
}

[[nodiscard]] std::vector<FrameRateCandidate>
normalizeCandidates(const std::vector<FrameRateCandidate>& candidates) {
    std::vector<FrameRateCandidate> normalized;
    normalized.reserve(candidates.size());
    for (const FrameRateCandidate& candidate : candidates) {
        if (!isPositive(candidate.rate)) {
            continue;
        }

        const TimelineRational rate = normalize(candidate.rate);
        const auto duplicate = std::find_if(
            normalized.begin(), normalized.end(), [rate](const FrameRateCandidate& existing) {
                return existing.rate == rate;
            });
        if (duplicate != normalized.end()) {
            duplicate->guessed = duplicate->guessed || candidate.guessed;
            continue;
        }
        normalized.push_back(FrameRateCandidate{.rate = rate, .guessed = candidate.guessed});
    }
    return normalized;
}

[[nodiscard]] std::optional<std::int64_t> timestampOffset(const std::int64_t timestamp,
                                                          const std::int64_t anchor) noexcept {
    if (timestamp < anchor) {
        return std::nullopt;
    }
    if (anchor < 0 && timestamp > std::numeric_limits<std::int64_t>::max() + anchor) {
        return std::nullopt;
    }
    return timestamp - anchor;
}

struct PassingCandidate final {
    FrameRateCandidate candidate;
    long double totalResidual = 0.0L;
};

[[nodiscard]] std::optional<PassingCandidate> verifyCandidate(const CfrVerificationRequest& request,
                                                              const FrameRateCandidate candidate,
                                                              const AVRational timeBase) {
    const AVRational frameDuration{
        .num = candidate.rate.denominator,
        .den = candidate.rate.numerator,
    };
    const std::int64_t anchor = request.presentationTimestamps.front();
    long double totalResidual = 0.0L;

    for (std::size_t index = 1U; index < request.presentationTimestamps.size(); ++index) {
        if (request.cancellation.requested() ||
            index > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }

        const auto actualOffset = timestampOffset(request.presentationTimestamps[index], anchor);
        if (!actualOffset.has_value()) {
            return std::nullopt;
        }

        const auto frameIndex = static_cast<std::int64_t>(index);
        const std::int64_t floorTick =
            av_rescale_q_rnd(frameIndex, frameDuration, timeBase, AV_ROUND_DOWN);
        const std::int64_t ceilTick =
            av_rescale_q_rnd(frameIndex, frameDuration, timeBase, AV_ROUND_UP);
        if (*actualOffset != floorTick && *actualOffset != ceilTick) {
            return std::nullopt;
        }

        const long double exactTick = static_cast<long double>(frameIndex) *
                                      static_cast<long double>(candidate.rate.denominator) *
                                      static_cast<long double>(timeBase.den) /
                                      (static_cast<long double>(candidate.rate.numerator) *
                                       static_cast<long double>(timeBase.num));
        totalResidual += std::fabs(static_cast<long double>(*actualOffset) - exactTick);
    }
    return PassingCandidate{.candidate = candidate, .totalResidual = totalResidual};
}

[[nodiscard]] domain::MediaError indexIoError(const TimestampIndexRequest& request,
                                              const domain::MediaErrorCode code,
                                              std::string technicalDetail) {
    return domain::makeMediaError(
        code, request.operation, request.sourceId, true, std::move(technicalDetail));
}

} // namespace

domain::Result<std::vector<std::int64_t>>
validatePresentationTimestamps(std::vector<std::int64_t> packetTimestamps,
                               const bool missingTimestampSeen,
                               const std::optional<std::int64_t> expectedFrameCount,
                               const domain::SourceId sourceId,
                               const domain::MediaOperation operation) {
    if (missingTimestampSeen) {
        return domain::Result<std::vector<std::int64_t>>::failure(timelineError(
            sourceId, operation, "At least one video packet has no presentation timestamp."));
    }
    if (packetTimestamps.empty()) {
        return domain::Result<std::vector<std::int64_t>>::failure(
            timelineError(sourceId, operation, "The video stream has no timestamped frames."));
    }
    if (packetTimestamps.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return domain::Result<std::vector<std::int64_t>>::failure(
            timelineError(sourceId,
                          operation,
                          "The presentation timestamp index exceeds the supported frame count."));
    }

    std::sort(packetTimestamps.begin(), packetTimestamps.end());
    if (std::adjacent_find(packetTimestamps.begin(), packetTimestamps.end()) !=
        packetTimestamps.end()) {
        return domain::Result<std::vector<std::int64_t>>::failure(timelineError(
            sourceId, operation, "Video packets contain duplicate presentation timestamps."));
    }

    if (expectedFrameCount.has_value() &&
        (*expectedFrameCount <= 0 ||
         static_cast<std::int64_t>(packetTimestamps.size()) != *expectedFrameCount)) {
        return domain::Result<std::vector<std::int64_t>>::failure(
            timelineError(sourceId,
                          operation,
                          "The indexed display-order frame count contradicts the reported count."));
    }
    return domain::Result<std::vector<std::int64_t>>::success(std::move(packetTimestamps));
}

[[nodiscard]] domain::Result<std::vector<std::int64_t>>
scanPresentationTimestampIndex(const TimestampIndexRequest& request) {
    if (request.cancellation.requested()) {
        return domain::Result<std::vector<std::int64_t>>::failure(cancellationError(request));
    }

    const std::u8string pathUtf8 = request.sourcePath.u8string();
    const std::string inputUrl{reinterpret_cast<const char*>(pathUtf8.data()), pathUtf8.size()};
    InterruptState interruptState{.cancellation = request.cancellation};

    AVFormatContext* rawFormat = avformat_alloc_context();
    if (rawFormat == nullptr) {
        return domain::Result<std::vector<std::int64_t>>::failure(
            indexIoError(request,
                         domain::MediaErrorCode::kMediaOpenFailed,
                         "FFmpeg could not allocate a format context for timeline indexing."));
    }
    rawFormat->interrupt_callback.callback = interruptCallback;
    rawFormat->interrupt_callback.opaque = &interruptState;

    const int openResult = avformat_open_input(&rawFormat, inputUrl.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        if (rawFormat != nullptr) {
            avformat_close_input(&rawFormat);
        }
        if (request.cancellation.requested()) {
            return domain::Result<std::vector<std::int64_t>>::failure(cancellationError(request));
        }
        return domain::Result<std::vector<std::int64_t>>::failure(indexIoError(
            request,
            domain::MediaErrorCode::kMediaOpenFailed,
            "FFmpeg could not open the source for timeline indexing: " + ffmpegError(openResult)));
    }
    AvFormatContextPtr format{rawFormat};

    const int streamInfoResult = avformat_find_stream_info(format.get(), nullptr);
    if (streamInfoResult < 0) {
        if (request.cancellation.requested()) {
            return domain::Result<std::vector<std::int64_t>>::failure(cancellationError(request));
        }
        const domain::MediaErrorCode code = request.operation == domain::MediaOperation::kMediaProbe
                                                ? domain::MediaErrorCode::kMediaProbeFailed
                                                : domain::MediaErrorCode::kMediaDecodeFailed;
        return domain::Result<std::vector<std::int64_t>>::failure(
            indexIoError(request,
                         code,
                         "FFmpeg could not inspect the source for timeline indexing: " +
                             ffmpegError(streamInfoResult)));
    }

    const int streamIndex =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0 || static_cast<unsigned int>(streamIndex) >= format->nb_streams) {
        return domain::Result<std::vector<std::int64_t>>::failure(timelineError(
            request.sourceId, request.operation, "The source has no video stream to index."));
    }

    AvPacketPtr packet{av_packet_alloc()};
    if (packet == nullptr) {
        const domain::MediaErrorCode code = request.operation == domain::MediaOperation::kMediaProbe
                                                ? domain::MediaErrorCode::kMediaProbeFailed
                                                : domain::MediaErrorCode::kMediaDecodeFailed;
        return domain::Result<std::vector<std::int64_t>>::failure(
            indexIoError(request, code, "FFmpeg could not allocate a timeline indexing packet."));
    }

    std::vector<std::int64_t> packetTimestamps;
    bool missingTimestampSeen = false;
    for (;;) {
        if (request.cancellation.requested()) {
            return domain::Result<std::vector<std::int64_t>>::failure(cancellationError(request));
        }
        const int readResult = av_read_frame(format.get(), packet.get());
        if (readResult == AVERROR_EOF) {
            break;
        }
        if (readResult < 0) {
            if (request.cancellation.requested()) {
                return domain::Result<std::vector<std::int64_t>>::failure(
                    cancellationError(request));
            }
            const domain::MediaErrorCode code =
                request.operation == domain::MediaOperation::kMediaProbe
                    ? domain::MediaErrorCode::kMediaProbeFailed
                    : domain::MediaErrorCode::kMediaDecodeFailed;
            return domain::Result<std::vector<std::int64_t>>::failure(indexIoError(
                request,
                code,
                "FFmpeg could not read packets for timeline indexing: " + ffmpegError(readResult)));
        }

        if (packet->stream_index == streamIndex) {
            if (packet->pts == AV_NOPTS_VALUE) {
                missingTimestampSeen = true;
            } else {
                packetTimestamps.push_back(packet->pts);
            }
        }
        av_packet_unref(packet.get());
    }

    return validatePresentationTimestamps(std::move(packetTimestamps),
                                          missingTimestampSeen,
                                          request.expectedFrameCount,
                                          request.sourceId,
                                          request.operation);
}

domain::Result<std::shared_ptr<const std::vector<std::int64_t>>>
buildPresentationTimestampIndex(const TimestampIndexRequest& request) {
    if (request.cancellation.requested()) {
        return domain::Result<PresentationTimestampIndex>::failure(cancellationError(request));
    }
    if (std::optional<PresentationTimestampIndex> cached = PresentationIndexCache::load(request)) {
        return domain::Result<PresentationTimestampIndex>::success(std::move(*cached));
    }

    domain::Result<std::vector<std::int64_t>> scanned = scanPresentationTimestampIndex(request);
    if (!scanned) {
        return domain::Result<PresentationTimestampIndex>::failure(scanned.error());
    }
    PresentationTimestampIndex index =
        std::make_shared<const std::vector<std::int64_t>>(std::move(scanned).value());
    PresentationIndexCache::store(request, index);
    return domain::Result<PresentationTimestampIndex>::success(std::move(index));
}

domain::Result<VerifiedCfrTiming> verifyConstantFrameRate(const CfrVerificationRequest& request) {
    if (request.cancellation.requested()) {
        return domain::Result<VerifiedCfrTiming>::failure(cancellationError(request));
    }
    if (request.presentationTimestamps.empty()) {
        return domain::Result<VerifiedCfrTiming>::failure(
            cfrError(request, "CFR verification requires at least one presentation timestamp."));
    }
    if (!isPositive(request.timeBase)) {
        return domain::Result<VerifiedCfrTiming>::failure(
            cfrError(request, "The disputed stream has an invalid time base."));
    }

    const std::vector<FrameRateCandidate> candidates = normalizeCandidates(request.candidates);
    if (candidates.empty()) {
        return domain::Result<VerifiedCfrTiming>::failure(
            cfrError(request, "FFmpeg supplied no positive frame-rate candidate."));
    }

    const AVRational timeBase{
        .num = request.timeBase.numerator,
        .den = request.timeBase.denominator,
    };
    std::optional<PassingCandidate> best;
    for (const FrameRateCandidate candidate : candidates) {
        if (request.cancellation.requested()) {
            return domain::Result<VerifiedCfrTiming>::failure(cancellationError(request));
        }

        const auto passing = verifyCandidate(request, candidate, timeBase);
        if (request.cancellation.requested()) {
            return domain::Result<VerifiedCfrTiming>::failure(cancellationError(request));
        }
        if (!passing.has_value()) {
            continue;
        }
        if (!best.has_value() || (passing->candidate.guessed && !best->candidate.guessed) ||
            (passing->candidate.guessed == best->candidate.guessed &&
             passing->totalResidual < best->totalResidual)) {
            best = passing;
        }
    }

    if (!best.has_value()) {
        return domain::Result<VerifiedCfrTiming>::failure(cfrError(
            request, "No FFmpeg frame-rate candidate matches the display-order timestamps."));
    }
    return domain::Result<VerifiedCfrTiming>::success(
        VerifiedCfrTiming{.frameRate = best->candidate.rate});
}

} // namespace dvs::media::internal
