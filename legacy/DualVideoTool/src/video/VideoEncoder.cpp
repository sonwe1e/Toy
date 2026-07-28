#include "video/VideoEncoder.h"
#include "utils/FFmpegUtils.h"

#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
}

VideoEncoder::VideoEncoder(QObject* parent) : QObject(parent) {}

VideoEncoder::~VideoEncoder() {
    close();
}

bool VideoEncoder::open(const QString& path, int width, int height, double fps) {
    close();

    std::string pathStr = path.toStdString();

    int ret = avformat_alloc_output_context2(&fmtCtx_, nullptr, nullptr, pathStr.c_str());
    if (ret < 0) {
        emit error("无法创建输出格式上下文");
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        emit error("未找到 H.264 编码器");
        close();
        return false;
    }

    stream_ = avformat_new_stream(fmtCtx_, nullptr);
    codecCtx_ = avcodec_alloc_context3(codec);

    AVRational frameRate = av_d2q(fps > 0.0 ? fps : 30.0, 100000);
    codecCtx_->width = width;
    codecCtx_->height = height;
    codecCtx_->time_base = av_inv_q(frameRate);
    codecCtx_->framerate = frameRate;
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx_->max_b_frames = 0;
    codecCtx_->gop_size = static_cast<int>(frameRate.num / std::max(1, frameRate.den));

    if (fmtCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        emit error(QString("无法打开编码器: %1")
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        close();
        return false;
    }

    avcodec_parameters_from_context(stream_->codecpar, codecCtx_);
    stream_->time_base = codecCtx_->time_base;

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx_->pb, pathStr.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            emit error("无法打开输出文件");
            close();
            return false;
        }
    }

    ret = avformat_write_header(fmtCtx_, nullptr);
    if (ret < 0) {
        emit error("无法写入文件头");
        close();
        return false;
    }

    swsCtx_ = sws_getContext(
        width, height, AV_PIX_FMT_RGB24,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    open_ = true;
    frameIndex_ = 0;
    return true;
}

bool VideoEncoder::writeFrame(const QImage& frame) {
    if (!open_) return false;

    AVFrame* avFrame = av_frame_alloc();
    if (!avFrame) {
        emit error("无法分配视频帧");
        return false;
    }

    avFrame->width = codecCtx_->width;
    avFrame->height = codecCtx_->height;
    avFrame->format = AV_PIX_FMT_YUV420P;
    int ret = av_frame_get_buffer(avFrame, 0);
    if (ret < 0) {
        emit error(QString("无法分配编码缓冲区: %1")
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        av_frame_free(&avFrame);
        return false;
    }

    // Convert QImage (RGB24) to YUV420P
    QImage rgb = frame.convertToFormat(QImage::Format_RGB888);
    const uint8_t* srcData[1] = { rgb.bits() };
    int srcLinesize[1] = { static_cast<int>(rgb.bytesPerLine()) };

    sws_scale(swsCtx_, srcData, srcLinesize, 0, frame.height(),
              avFrame->data, avFrame->linesize);

    avFrame->pts = frameIndex_++;

    ret = avcodec_send_frame(codecCtx_, avFrame);
    av_frame_free(&avFrame);

    if (ret < 0) {
        emit error(QString("无法发送编码帧: %1")
                   .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
        return false;
    }

    return drainPackets(false);
}

void VideoEncoder::close() {
    if (codecCtx_ && open_) {
        avcodec_send_frame(codecCtx_, nullptr);
        drainPackets(true);
        av_write_trailer(fmtCtx_);
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
    if (fmtCtx_) {
        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE) && fmtCtx_->pb) {
            avio_closep(&fmtCtx_->pb);
        }
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
    }
    stream_ = nullptr;
    open_ = false;
}

bool VideoEncoder::isOpen() const {
    return open_;
}

bool VideoEncoder::drainPackets(bool flushing) {
    Q_UNUSED(flushing)
    if (!codecCtx_ || !fmtCtx_ || !stream_) return false;

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        emit error("无法分配编码包");
        return false;
    }

    bool ok = true;
    while (true) {
        int ret = avcodec_receive_packet(codecCtx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            emit error(QString("无法接收编码包: %1")
                       .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
            ok = false;
            break;
        }

        av_packet_rescale_ts(pkt, codecCtx_->time_base, stream_->time_base);
        pkt->stream_index = stream_->index;
        ret = av_interleaved_write_frame(fmtCtx_, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            emit error(QString("无法写入编码包: %1")
                       .arg(QString::fromStdString(FFmpegUtils::errorString(ret))));
            ok = false;
            break;
        }
    }

    av_packet_free(&pkt);
    return ok;
}
