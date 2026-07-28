#include "utils/FFmpegUtils.h"
#include <QImage>
#include <cstring>

namespace FFmpegUtils {

std::string errorString(int errorNum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errorNum, buf, sizeof(buf));
    return std::string(buf);
}

void initFFmpeg() {
    // FFmpeg 4.x+ auto-registers, but explicit call is harmless
    // avformat_network_init() if needed later
}

QImage::Format pixelFormatToQImage(int avPixelFormat) {
    switch (avPixelFormat) {
        case AV_PIX_FMT_RGB24:  return QImage::Format_RGB888;
        case AV_PIX_FMT_RGBA:   return QImage::Format_RGBA8888;
        case AV_PIX_FMT_BGR24:  return QImage::Format_BGR888;
        case AV_PIX_FMT_BGRA:   return QImage::Format_ARGB32;
        default:                 return QImage::Format_RGB888;
    }
}

} // namespace FFmpegUtils
