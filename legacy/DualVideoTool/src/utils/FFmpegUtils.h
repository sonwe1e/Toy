#pragma once

#include <QImage>
#include <string>

extern "C" {
#include <libavutil/error.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace FFmpegUtils {

// Convert FFmpeg error code to human-readable string
std::string errorString(int errorNum);

// Register all FFmpeg codecs/formats (call once at startup)
void initFFmpeg();

// Convert AVPixelFormat to QImage::Format (best effort)
QImage::Format pixelFormatToQImage(int avPixelFormat);

} // namespace FFmpegUtils
