#include "SoftwareDecoder.h"

#include "dvs/platform/FrameResourceFactory.h"
#include "dvs/platform/SourceIdentityService.h"
#include "dvs/platform/WindowsPaths.h"

#include "AvRaii.h"
#include "FrameTimelineIndex.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace dvs::media::internal {
namespace {

struct InterruptState final {
    const std::atomic<bool>* requested = nullptr;
    const std::atomic<bool>* externalRequested = nullptr;
};

struct TimelineIndexCancellationState final {
    const std::atomic<bool>* request = nullptr;
    const std::atomic<bool>* interrupted = nullptr;
    const std::atomic<bool>* external = nullptr;
};

[[nodiscard]] int interruptCallback(void* opaque) noexcept {
    const auto* const state = static_cast<const InterruptState*>(opaque);
    if (state == nullptr) {
        return 0;
    }
    const bool requested =
        state->requested != nullptr && state->requested->load(std::memory_order_acquire);
    const bool externallyRequested = state->externalRequested != nullptr &&
                                     state->externalRequested->load(std::memory_order_acquire);
    return requested || externallyRequested ? 1 : 0;
}

[[nodiscard]] bool timelineIndexCancellationRequested(const void* const opaque) noexcept {
    const auto* const state = static_cast<const TimelineIndexCancellationState*>(opaque);
    if (state == nullptr) {
        return false;
    }
    const auto requested = [](const std::atomic<bool>* const flag) {
        return flag != nullptr && flag->load(std::memory_order_acquire);
    };
    return requested(state->request) || requested(state->interrupted) || requested(state->external);
}

[[nodiscard]] domain::MediaError decodeError(const domain::MediaErrorCode code,
                                             const domain::SourceId sourceId,
                                             std::string technicalDetail,
                                             const bool recoverable = false) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kMediaDecode,
                                  sourceId,
                                  recoverable,
                                  std::move(technicalDetail));
}

[[nodiscard]] std::string ffmpegError(const int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg returned error " + std::to_string(errorCode) + ".";
    }
    return std::string{buffer};
}

[[nodiscard]] bool isSupportedDecodedFormat(const AVFrame& frame) noexcept {
    const auto pixelFormat = static_cast<AVPixelFormat>(frame.format);
    const AVPixFmtDescriptor* const descriptor = av_pix_fmt_desc_get(pixelFormat);
    if (descriptor == nullptr || descriptor->nb_components != 3 || descriptor->log2_chroma_w != 1 ||
        descriptor->log2_chroma_h != 1) {
        return false;
    }
    for (int component = 0; component < descriptor->nb_components; ++component) {
        if (descriptor->comp[component].depth != 8) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
multiplyFits(const std::size_t left, const std::size_t right, std::size_t* const result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    *result = left * right;
    return true;
}

} // namespace

class SoftwareDecoder::Impl final {
public:
    Impl(const domain::SourceId sourceIdValue,
         domain::MediaDescriptor descriptorValue,
         platform::FrameBudget& frameBudget,
         const std::atomic<bool>* const externalInterrupt)
        : sourceId(sourceIdValue), descriptor(std::move(descriptorValue)), factory(frameBudget),
          interruptState{.requested = &interrupted, .externalRequested = externalInterrupt} {}

    domain::SourceId sourceId;
    domain::MediaDescriptor descriptor;
    platform::FrameResourceFactory factory;
    std::atomic<bool> interrupted = false;
    InterruptState interruptState;
    AvFormatContextPtr format;
    AvCodecContextPtr codec;
    AvPacketPtr packet;
    AvFramePtr frame;
    int streamIndex = -1;
    AVRational timeBase{};
    std::int64_t startTimestamp = 0;
    std::optional<std::vector<std::int64_t>> presentationTimestamps;
    std::optional<domain::FrameId> lastReturnedFrame;
    std::uint64_t exactSeekCount = 0;
    bool packetPending = false;
    bool inputEnded = false;
    bool flushSubmitted = false;
    bool sequentialReady = false;
    bool opened = false;
};

SoftwareDecoder::SoftwareDecoder(const domain::SourceId sourceId,
                                 domain::MediaDescriptor descriptor,
                                 platform::FrameBudget& frameBudget,
                                 const std::atomic<bool>* const externalInterrupt)
    : impl_(std::make_unique<Impl>(
          sourceId, std::move(descriptor), frameBudget, externalInterrupt)) {}

SoftwareDecoder::~SoftwareDecoder() = default;

domain::Status SoftwareDecoder::open(const std::atomic<bool>& cancellationRequested) {
    close();
    impl_->interrupted.store(cancellationRequested.load(std::memory_order_acquire),
                             std::memory_order_release);
    if (cancellationRequested.load(std::memory_order_acquire)) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                                   impl_->sourceId,
                                                   "Source decoder open was canceled.",
                                                   true));
    }
    if (!impl_->descriptor.sourceIdentity.has_value() ||
        !impl_->descriptor.sourceIdentity->isComplete()) {
        return domain::Status::failure(decodeError(
            domain::MediaErrorCode::kInvalidMediaDescriptor,
            impl_->sourceId,
            "A direct decoder requires a complete source identity captured by media probing."));
    }

    const domain::SourceId sourceId = impl_->sourceId;
    const auto identity =
        platform::SourceIdentityService::verify(impl_->descriptor.normalizedPath,
                                                *impl_->descriptor.sourceIdentity,
                                                sourceId,
                                                domain::MediaOperation::kMediaDecode);
    if (!identity) {
        return identity;
    }

    const auto normalizedPath =
        platform::WindowsPaths::absolutePath(impl_->descriptor.normalizedPath);
    if (!normalizedPath) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaOpenFailed,
                                                   sourceId,
                                                   "Could not normalize the source path: " +
                                                       normalizedPath.error().technicalDetail));
    }
    const std::u8string pathUtf8 = normalizedPath.value().u8string();
    const std::string inputUrl{reinterpret_cast<const char*>(pathUtf8.data()), pathUtf8.size()};

    AVFormatContext* rawFormat = avformat_alloc_context();
    if (rawFormat == nullptr) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaOpenFailed,
                                                   sourceId,
                                                   "FFmpeg could not allocate a format context."));
    }
    rawFormat->interrupt_callback.callback = interruptCallback;
    rawFormat->interrupt_callback.opaque = &impl_->interruptState;

    const int openResult = avformat_open_input(&rawFormat, inputUrl.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        if (rawFormat != nullptr) {
            avformat_close_input(&rawFormat);
        }
        return domain::Status::failure(
            decodeError(domain::MediaErrorCode::kMediaOpenFailed,
                        sourceId,
                        "FFmpeg could not open the source: " + ffmpegError(openResult),
                        true));
    }
    AvFormatContextPtr openedFormat{rawFormat};

    const int streamInfoResult = avformat_find_stream_info(openedFormat.get(), nullptr);
    if (streamInfoResult < 0) {
        return domain::Status::failure(decodeError(
            domain::MediaErrorCode::kMediaDecodeFailed,
            sourceId,
            "FFmpeg could not read source stream information: " + ffmpegError(streamInfoResult),
            true));
    }

    const int selectedStream =
        av_find_best_stream(openedFormat.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (selectedStream < 0 ||
        static_cast<unsigned int>(selectedStream) >= openedFormat->nb_streams) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                                   sourceId,
                                                   "The source has no readable video stream.",
                                                   true));
    }
    const AVStream* const stream = openedFormat->streams[selectedStream];
    if (stream == nullptr || stream->codecpar == nullptr || stream->codecpar->width <= 0 ||
        stream->codecpar->height <= 0) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                                   sourceId,
                                                   "The selected video stream is incomplete."));
    }
    if (static_cast<std::uint32_t>(stream->codecpar->width) != impl_->descriptor.extent.width ||
        static_cast<std::uint32_t>(stream->codecpar->height) != impl_->descriptor.extent.height) {
        return domain::Status::failure(
            decodeError(domain::MediaErrorCode::kSourceFingerprintMismatch,
                        sourceId,
                        "The source geometry changed after media probing.",
                        true));
    }

    const AVCodec* const decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr) {
        return domain::Status::failure(
            decodeError(domain::MediaErrorCode::kUnsupportedCodec,
                        sourceId,
                        "FFmpeg has no decoder for the probed source codec.",
                        true));
    }
    AVCodecContext* rawCodec = avcodec_alloc_context3(decoder);
    if (rawCodec == nullptr) {
        return domain::Status::failure(decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                                   sourceId,
                                                   "FFmpeg could not allocate a codec context."));
    }
    AvCodecContextPtr openedCodec{rawCodec};
    const int parameterResult = avcodec_parameters_to_context(openedCodec.get(), stream->codecpar);
    if (parameterResult < 0) {
        return domain::Status::failure(decodeError(
            domain::MediaErrorCode::kMediaDecodeFailed,
            sourceId,
            "FFmpeg could not transfer source codec parameters: " + ffmpegError(parameterResult)));
    }
    const int codecOpenResult = avcodec_open2(openedCodec.get(), decoder, nullptr);
    if (codecOpenResult < 0) {
        return domain::Status::failure(
            decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                        sourceId,
                        "FFmpeg could not open the source decoder: " + ffmpegError(codecOpenResult),
                        true));
    }

    impl_->format = std::move(openedFormat);
    impl_->codec = std::move(openedCodec);
    impl_->streamIndex = selectedStream;
    impl_->timeBase = stream->time_base;
    impl_->startTimestamp = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    impl_->opened = true;

    TimelineIndexCancellationState indexCancellation{
        .request = &cancellationRequested,
        .interrupted = &impl_->interrupted,
        .external = impl_->interruptState.externalRequested,
    };
    auto timestamps = buildPresentationTimestampIndex(TimestampIndexRequest{
        .sourcePath = impl_->descriptor.normalizedPath,
        .sourceId = sourceId,
        .operation = domain::MediaOperation::kMediaDecode,
        .expectedFrameCount = impl_->descriptor.frameCount.value,
        .cancellation =
            TimelineCancellation{
                .isRequested = timelineIndexCancellationRequested,
                .context = &indexCancellation,
            },
    });
    if (!timestamps) {
        close();
        return domain::Status::failure(timestamps.error());
    }
    impl_->presentationTimestamps = std::move(timestamps).value();
    return domain::Status::success();
}

domain::Result<DecodedFrame>
SoftwareDecoder::decodeExact(const domain::FrameId frameId,
                             const std::atomic<bool>& cancellationRequested) {
    auto result = decodeInternal(frameId, cancellationRequested, false);
    if (!result) {
        impl_->sequentialReady = false;
    }
    return result;
}

domain::Result<DecodedFrame>
SoftwareDecoder::decodeSequential(const domain::FrameId frameId,
                                  const std::atomic<bool>& cancellationRequested) {
    const bool continueSequentially = impl_->sequentialReady &&
                                      impl_->lastReturnedFrame.has_value() && frameId.isValid() &&
                                      frameId.value() > impl_->lastReturnedFrame->value();
    auto result = decodeInternal(frameId, cancellationRequested, continueSequentially);
    if (!result) {
        impl_->sequentialReady = false;
    }
    return result;
}

domain::Result<DecodedFrame>
SoftwareDecoder::decodeInternal(const domain::FrameId frameId,
                                const std::atomic<bool>& cancellationRequested,
                                const bool continueSequentially) {
    const domain::SourceId sourceId = impl_->sourceId;
    impl_->interrupted.store(cancellationRequested.load(std::memory_order_acquire),
                             std::memory_order_release);
    if (cancellationRequested.load(std::memory_order_acquire)) {
        return domain::Result<DecodedFrame>::failure(
            decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                        sourceId,
                        "Frame decoding was canceled.",
                        true));
    }
    if (!impl_->opened || impl_->format == nullptr || impl_->codec == nullptr) {
        return domain::Result<DecodedFrame>::failure(decodeError(
            domain::MediaErrorCode::kMediaDecodeFailed, sourceId, "Frame decoder is not open."));
    }
    if (!frameId.isValid() || frameId.value() >= impl_->descriptor.frameCount.value) {
        return domain::Result<DecodedFrame>::failure(
            decodeError(domain::MediaErrorCode::kInvalidFrameId,
                        sourceId,
                        "Requested frame is outside the source timeline."));
    }
    if (impl_->timeBase.num <= 0 || impl_->timeBase.den <= 0) {
        return domain::Result<DecodedFrame>::failure(
            decodeError(domain::MediaErrorCode::kInvalidRate,
                        sourceId,
                        "The decoded source has a stream time base outside FFmpeg limits."));
    }
    if (!impl_->presentationTimestamps.has_value()) {
        return domain::Result<DecodedFrame>::failure(
            decodeError(domain::MediaErrorCode::kFrameTimelineInvalid,
                        sourceId,
                        "The decoder was opened without a native presentation timestamp index.",
                        true));
    }
    const std::int64_t targetTimestamp =
        (*impl_->presentationTimestamps)[static_cast<std::size_t>(frameId.value())];
    if (!continueSequentially) {
        const int seekResult = av_seek_frame(
            impl_->format.get(), impl_->streamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) {
            return domain::Result<DecodedFrame>::failure(decodeError(
                domain::MediaErrorCode::kMediaDecodeFailed,
                sourceId,
                "FFmpeg could not seek to the target keyframe: " + ffmpegError(seekResult),
                true));
        }
        ++impl_->exactSeekCount;
        avcodec_flush_buffers(impl_->codec.get());
        if (impl_->packet != nullptr) {
            av_packet_unref(impl_->packet.get());
        }
        if (impl_->frame != nullptr) {
            av_frame_unref(impl_->frame.get());
        }
        impl_->packetPending = false;
        impl_->inputEnded = false;
        impl_->flushSubmitted = false;
        impl_->lastReturnedFrame.reset();
        impl_->sequentialReady = false;
    }

    if (impl_->packet == nullptr) {
        impl_->packet.reset(av_packet_alloc());
    }
    if (impl_->frame == nullptr) {
        impl_->frame.reset(av_frame_alloc());
    }
    if (impl_->packet == nullptr || impl_->frame == nullptr) {
        return domain::Result<DecodedFrame>::failure(
            decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                        sourceId,
                        "FFmpeg could not allocate decode buffers."));
    }

    for (;;) {
        if (cancellationRequested.load(std::memory_order_acquire) ||
            impl_->interrupted.load(std::memory_order_acquire)) {
            return domain::Result<DecodedFrame>::failure(
                decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                            sourceId,
                            "Frame decoding was interrupted.",
                            true));
        }

        const int receiveResult = avcodec_receive_frame(impl_->codec.get(), impl_->frame.get());
        if (receiveResult == 0) {
            const std::int64_t timestamp = impl_->frame->best_effort_timestamp != AV_NOPTS_VALUE
                                               ? impl_->frame->best_effort_timestamp
                                               : impl_->frame->pts;
            if (timestamp == AV_NOPTS_VALUE) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                sourceId,
                                "A decoded frame has no presentation timestamp.",
                                true));
            }
            if (timestamp < targetTimestamp) {
                av_frame_unref(impl_->frame.get());
                continue;
            }
            if (timestamp > targetTimestamp) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                sourceId,
                                "The indexed timestamp did not identify a decoded frame.",
                                true));
            }
            if (!isSupportedDecodedFormat(*impl_->frame)) {
                return domain::Result<DecodedFrame>::failure(decodeError(
                    domain::MediaErrorCode::kUnsupportedPixelFormat,
                    sourceId,
                    "The decoder produced a pixel format outside the v1 8-bit 4:2:0 contract.",
                    true));
            }

            if (impl_->frame->width <= 0 || impl_->frame->height <= 0 ||
                static_cast<std::uint32_t>(impl_->frame->width) != impl_->descriptor.extent.width ||
                static_cast<std::uint32_t>(impl_->frame->height) !=
                    impl_->descriptor.extent.height) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kSourceFingerprintMismatch,
                                sourceId,
                                "Decoded frame geometry changed after media probing.",
                                true));
            }
            const std::uint32_t width = static_cast<std::uint32_t>(impl_->frame->width);
            const std::uint32_t height = static_cast<std::uint32_t>(impl_->frame->height);
            const std::uint32_t stride = (width + 1U) & ~1U;
            if (width == 0U || height == 0U || stride > static_cast<std::uint32_t>(INT_MAX)) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                sourceId,
                                "Decoded frame dimensions cannot form a valid NV12 layout."));
            }
            std::size_t yBytes = 0;
            std::size_t uvBytes = 0;
            if (!multiplyFits(stride, height, &yBytes) ||
                !multiplyFits(stride, static_cast<std::size_t>((height + 1U) / 2U), &uvBytes) ||
                yBytes > std::numeric_limits<std::size_t>::max() - uvBytes) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kArithmeticOverflow,
                                sourceId,
                                "Decoded NV12 frame byte count overflowed."));
            }

            try {
                std::vector<std::uint8_t> nv12(yBytes + uvBytes);
                SwsContextPtr converter{
                    sws_getContext(impl_->frame->width,
                                   impl_->frame->height,
                                   static_cast<AVPixelFormat>(impl_->frame->format),
                                   impl_->frame->width,
                                   impl_->frame->height,
                                   AV_PIX_FMT_NV12,
                                   SWS_BILINEAR,
                                   nullptr,
                                   nullptr,
                                   nullptr)};
                if (converter == nullptr) {
                    return domain::Result<DecodedFrame>::failure(
                        decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                    sourceId,
                                    "FFmpeg could not create an NV12 conversion context."));
                }
                std::array<std::uint8_t*, 4> outputPlanes{
                    nv12.data(),
                    nv12.data() + static_cast<std::ptrdiff_t>(yBytes),
                    nullptr,
                    nullptr,
                };
                const std::array<int, 4> outputStrides{
                    static_cast<int>(stride),
                    static_cast<int>(stride),
                    0,
                    0,
                };
                const int convertedRows = sws_scale(converter.get(),
                                                    impl_->frame->data,
                                                    impl_->frame->linesize,
                                                    0,
                                                    impl_->frame->height,
                                                    outputPlanes.data(),
                                                    outputStrides.data());
                if (convertedRows != impl_->frame->height) {
                    return domain::Result<DecodedFrame>::failure(
                        decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                    sourceId,
                                    "FFmpeg could not convert the decoded frame to NV12."));
                }
                const platform::Nv12FrameLayout layout{
                    .width = width,
                    .height = height,
                    .yStride = stride,
                    .uvStride = stride,
                };
                auto handle = impl_->factory.createCpuNv12(
                    layout,
                    impl_->descriptor.colorMetadata,
                    std::span<const std::uint8_t>{nv12.data(), yBytes},
                    std::span<const std::uint8_t>{nv12.data() + static_cast<std::ptrdiff_t>(yBytes),
                                                  uvBytes});
                if (!handle) {
                    return domain::Result<DecodedFrame>::failure(decodeError(
                        domain::MediaErrorCode::kFrameBudgetExceeded,
                        sourceId,
                        "The shared frame budget could not reserve this decoded NV12 frame.",
                        true));
                }
                const std::int64_t presentationMicroseconds = av_rescale_q(
                    timestamp, impl_->timeBase, AVRational{.num = 1, .den = AV_TIME_BASE});
                auto result = domain::Result<DecodedFrame>::success(DecodedFrame{
                    .handle = std::move(*handle),
                    .presentationTime = domain::MediaTime{presentationMicroseconds},
                });
                av_frame_unref(impl_->frame.get());
                impl_->lastReturnedFrame = frameId;
                impl_->sequentialReady = true;
                return result;
            } catch (const std::exception& exception) {
                return domain::Result<DecodedFrame>::failure(
                    decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                                sourceId,
                                "Decoded NV12 allocation failed: " + std::string{exception.what()},
                                true));
            }
        }

        if (receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
            return domain::Result<DecodedFrame>::failure(decodeError(
                domain::MediaErrorCode::kMediaDecodeFailed,
                sourceId,
                "FFmpeg could not receive a decoded frame: " + ffmpegError(receiveResult),
                true));
        }
        if (receiveResult == AVERROR_EOF) {
            break;
        }

        if (!impl_->packetPending && !impl_->inputEnded) {
            for (;;) {
                const int readResult = av_read_frame(impl_->format.get(), impl_->packet.get());
                if (readResult == AVERROR_EOF) {
                    impl_->inputEnded = true;
                    break;
                }
                if (readResult < 0) {
                    return domain::Result<DecodedFrame>::failure(decodeError(
                        domain::MediaErrorCode::kMediaDecodeFailed,
                        sourceId,
                        "FFmpeg could not read a source packet: " + ffmpegError(readResult),
                        true));
                }
                if (impl_->packet->stream_index == impl_->streamIndex) {
                    impl_->packetPending = true;
                    break;
                }
                av_packet_unref(impl_->packet.get());
            }
        }

        if (impl_->packetPending) {
            const int sendResult = avcodec_send_packet(impl_->codec.get(), impl_->packet.get());
            if (sendResult == 0) {
                av_packet_unref(impl_->packet.get());
                impl_->packetPending = false;
                continue;
            }
            if (sendResult == AVERROR(EAGAIN)) {
                // Preserve the packet. The next receive attempt drains output before retrying it.
                continue;
            }
            return domain::Result<DecodedFrame>::failure(
                decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                            sourceId,
                            "FFmpeg could not submit a source packet: " + ffmpegError(sendResult),
                            true));
        }

        if (impl_->inputEnded && !impl_->flushSubmitted) {
            const int flushResult = avcodec_send_packet(impl_->codec.get(), nullptr);
            if (flushResult == 0 || flushResult == AVERROR_EOF) {
                impl_->flushSubmitted = true;
                continue;
            }
            if (flushResult == AVERROR(EAGAIN)) {
                continue;
            }
            return domain::Result<DecodedFrame>::failure(
                decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                            sourceId,
                            "FFmpeg could not flush the decoder: " + ffmpegError(flushResult),
                            true));
        }

        if (impl_->inputEnded && impl_->flushSubmitted) {
            return domain::Result<DecodedFrame>::failure(
                decodeError(domain::MediaErrorCode::kMediaDecodeFailed,
                            sourceId,
                            "The decoder requested input after its stream had been fully flushed.",
                            true));
        }
    }

    return domain::Result<DecodedFrame>::failure(
        decodeError(domain::MediaErrorCode::kFrameTimelineInvalid,
                    sourceId,
                    "The decoder reached end of stream before the indexed frame timestamp.",
                    true));
}

std::uint64_t SoftwareDecoder::exactSeekCount() const noexcept {
    return impl_->exactSeekCount;
}

void SoftwareDecoder::requestInterrupt() noexcept {
    impl_->interrupted.store(true, std::memory_order_release);
    impl_->sequentialReady = false;
}

void SoftwareDecoder::close() noexcept {
    impl_->interrupted.store(true, std::memory_order_release);
    impl_->frame.reset();
    impl_->packet.reset();
    impl_->codec.reset();
    impl_->format.reset();
    impl_->streamIndex = -1;
    impl_->timeBase = AVRational{};
    impl_->startTimestamp = 0;
    impl_->presentationTimestamps.reset();
    impl_->lastReturnedFrame.reset();
    impl_->exactSeekCount = 0;
    impl_->packetPending = false;
    impl_->inputEnded = false;
    impl_->flushSubmitted = false;
    impl_->sequentialReady = false;
    impl_->opened = false;
}

} // namespace dvs::media::internal
