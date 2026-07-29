#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libswscale/swscale.h>
}

#include <memory>

namespace dvs::media::internal {

struct AvFormatContextDeleter final {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;

struct AvCodecContextDeleter final {
    void operator()(AVCodecContext* context) const noexcept {
        if (context != nullptr) {
            avcodec_free_context(&context);
        }
    }
};

using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;

struct AvPacketDeleter final {
    void operator()(AVPacket* packet) const noexcept {
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
    }
};

using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

struct AvFrameDeleter final {
    void operator()(AVFrame* frame) const noexcept {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
    }
};

using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

struct AvBufferRefDeleter final {
    void operator()(AVBufferRef* reference) const noexcept {
        if (reference != nullptr) {
            av_buffer_unref(&reference);
        }
    }
};

using AvBufferRefPtr = std::unique_ptr<AVBufferRef, AvBufferRefDeleter>;

struct SwsContextDeleter final {
    void operator()(SwsContext* context) const noexcept {
        if (context != nullptr) {
            sws_freeContext(context);
        }
    }
};

using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

} // namespace dvs::media::internal
