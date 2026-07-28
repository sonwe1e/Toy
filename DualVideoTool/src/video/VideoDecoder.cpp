#include "video/VideoDecoder.h"
#include "utils/FFmpegUtils.h"

#include <QImage>
#include <cmath>
#include <algorithm>

VideoDecoder::VideoDecoder(QObject* parent)
    : QObject(parent), cache_(240) {}

VideoDecoder::~VideoDecoder() {
    close();
}

bool VideoDecoder::open(const QString& path) {
    close();

    std::string pathStr = path.toStdString();
    int ret = avformat_open_input(&fmtCtx_, pathStr.c_str(), nullptr, nullptr);
    if (ret < 0) {
        emit error(QString("无法打开视频文件: %1\n%2")
                   .arg(path)
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        return false;
    }

    ret = avformat_find_stream_info(fmtCtx_, nullptr);
    if (ret < 0) {
        emit error(QString("无法读取流信息: %1")
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        close();
        return false;
    }

    videoStreamIdx_ = -1;
    for (unsigned i = 0; i < fmtCtx_->nb_streams; i++) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx_ = static_cast<int>(i);
            break;
        }
    }
    if (videoStreamIdx_ < 0) {
        emit error("未找到视频流");
        close();
        return false;
    }

    AVStream* stream = fmtCtx_->streams[videoStreamIdx_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        emit error("未找到解码器");
        close();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx_, stream->codecpar);

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        emit error(QString("无法打开解码器: %1")
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        close();
        return false;
    }

    if (!readMetadata()) {
        close();
        return false;
    }

    metadata_.path = pathStr;
    open_ = true;
    return true;
}

void VideoDecoder::close() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
    }
    videoStreamIdx_ = -1;
    lastDecodedFrame_ = -1;
    cache_.clear();
    metadata_ = {};
    open_ = false;
}

bool VideoDecoder::isOpen() const {
    return open_;
}

VideoMetadata VideoDecoder::metadata() const {
    return metadata_;
}

FrameCache& VideoDecoder::cache() {
    return cache_;
}

const FrameCache& VideoDecoder::cache() const {
    return cache_;
}

bool VideoDecoder::readMetadata() {
    if (videoStreamIdx_ < 0 || !fmtCtx_) return false;

    AVStream* stream = fmtCtx_->streams[videoStreamIdx_];

    metadata_.width = codecCtx_->width;
    metadata_.height = codecCtx_->height;
    metadata_.codecName = avcodec_get_name(codecCtx_->codec_id);
    metadata_.pixelFormat = av_get_pix_fmt_name(codecCtx_->pix_fmt);

    AVRational r = av_guess_frame_rate(fmtCtx_, stream, nullptr);
    if (r.num > 0 && r.den > 0) {
        metadata_.fps = av_q2d(r);
    } else {
        metadata_.fps = 30.0;
    }

    if (stream->nb_frames > 0) {
        metadata_.frameCount = stream->nb_frames;
    } else if (stream->duration > 0 && stream->time_base.den > 0) {
        metadata_.frameCount = static_cast<int64_t>(
            stream->duration * av_q2d(stream->time_base) * metadata_.fps);
    } else if (fmtCtx_->duration > 0) {
        metadata_.frameCount = static_cast<int64_t>(
            fmtCtx_->duration / (double)AV_TIME_BASE * metadata_.fps);
    }

    if (stream->duration > 0 && stream->time_base.den > 0) {
        metadata_.duration = stream->duration * av_q2d(stream->time_base);
    } else if (fmtCtx_->duration > 0) {
        metadata_.duration = fmtCtx_->duration / (double)AV_TIME_BASE;
    }

    return metadata_.frameCount > 0;
}

std::optional<VideoFrame> VideoDecoder::decodeFrame(int64_t frameIndex) {
    if (!open_ || frameIndex < 0) return std::nullopt;

    // Check cache first
    auto cached = cache_.get(frameIndex);
    if (cached.has_value()) {
        lastDecodedFrame_ = frameIndex;
        return cached;
    }

    // Decide seek strategy
    bool needSeek = true;
    if (lastDecodedFrame_ >= 0) {
        int64_t delta = frameIndex - lastDecodedFrame_;
        if (delta >= 0 && delta <= 3) {
            needSeek = false;
        }
    }

    if (needSeek) {
        if (!seekToFrame(frameIndex)) {
            return std::nullopt;
        }
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    if (!frame || !pkt) {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        return std::nullopt;
    }

    std::optional<VideoFrame> result;
    while (true) {
        if (!decodeNextFrame(frame, pkt)) break;

        int64_t currentIdx = frame->best_effort_timestamp;
        AVStream* stream = fmtCtx_->streams[videoStreamIdx_];
        if (stream->time_base.den > 0 && metadata_.fps > 0) {
            double ptsSeconds = currentIdx * av_q2d(stream->time_base);
            currentIdx = static_cast<int64_t>(std::round(ptsSeconds * metadata_.fps));
        }

        if (currentIdx >= 0) {
            VideoFrame vf = convertFrame(frame, currentIdx);
            if (vf.isValid()) {
                cache_.insert(currentIdx, vf);
                if (currentIdx == frameIndex) {
                    result = vf;
                }
            }
        }

        if (result.has_value()) break;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    if (result.has_value()) {
        lastDecodedFrame_ = frameIndex;
    }
    return result;
}

bool VideoDecoder::seekToFrame(int64_t frameIndex) {
    if (!fmtCtx_ || videoStreamIdx_ < 0) return false;

    AVStream* stream = fmtCtx_->streams[videoStreamIdx_];
    double seconds = frameIndex / metadata_.fps;
    int64_t targetPts = static_cast<int64_t>(seconds / av_q2d(stream->time_base));

    int ret = av_seek_frame(fmtCtx_, videoStreamIdx_, targetPts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        ret = av_seek_frame(fmtCtx_, videoStreamIdx_, targetPts,
                            AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE);
        if (ret < 0) return false;
    }

    avcodec_flush_buffers(codecCtx_);
    return true;
}

bool VideoDecoder::decodeNextFrame(AVFrame* frame, AVPacket* pkt) {
    while (true) {
        int ret = av_read_frame(fmtCtx_, pkt);
        if (ret < 0) {
            avcodec_send_packet(codecCtx_, nullptr);
            ret = avcodec_receive_frame(codecCtx_, frame);
            return ret >= 0;
        }

        if (pkt->stream_index != videoStreamIdx_) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(codecCtx_, frame);
        if (ret == 0) return true;
        if (ret == AVERROR(EAGAIN)) continue;
        return false;
    }
}

VideoFrame VideoDecoder::convertFrame(AVFrame* frame, int64_t frameIndex) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return {};

    int dstW = frame->width;
    int dstH = frame->height;

    if (!swsCtx_) {
        swsCtx_ = sws_getContext(
            dstW, dstH, static_cast<AVPixelFormat>(frame->format),
            dstW, dstH, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!swsCtx_) return {};
    }

    QImage img(dstW, dstH, QImage::Format_RGB888);
    uint8_t* dstData[1] = { img.bits() };
    int dstLinesize[1] = { static_cast<int>(img.bytesPerLine()) };

    sws_scale(swsCtx_, frame->data, frame->linesize, 0, dstH, dstData, dstLinesize);

    VideoFrame vf;
    vf.image = img;
    vf.pts = frame->best_effort_timestamp;
    vf.frameIndex = frameIndex;
    return vf;
}
